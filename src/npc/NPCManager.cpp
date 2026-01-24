#include "NPCManager.h"
#include "BehaviorTree.h"
#include "NPCBehaviorTrees.h"
#include "physics/PhysicsSystem.h"
#include "animation/AnimatedCharacter.h"
#include "animation/SkinnedMeshRenderer.h"
#include "animation/Animation.h"
#include "animation/GLTFLoader.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <sstream>
#include <cmath>

NPCID NPCManager::spawn(const NPCSpawnInfo& info) {
    NPC npc;
    npc.id = nextId_++;
    npc.name = info.name;
    npc.transform.position = info.position;
    npc.spawnPosition = info.position;
    npc.hostility = info.hostility;
    npc.baseHostility = info.hostility;
    npc.baseSpeed = info.baseSpeed;
    npc.health = info.health;
    npc.maxHealth = info.health;
    npc.config = info.config;
    npc.patrolPath = info.patrolPath;

    // Create behavior tree based on hostility type
    npc.behaviorTree = NPCBehaviorTrees::CreateBehaviorTree(info.hostility);

    NPCID spawnedId = npc.id;
    npcs_.push_back(std::move(npc));

    SDL_Log("Spawned NPC '%s' (ID: %u) at (%.1f, %.1f, %.1f) with hostility %d [BehaviorTree]",
            info.name.c_str(), spawnedId,
            info.position.x, info.position.y, info.position.z,
            static_cast<int>(info.hostility));

    fireEvent(spawnedId, "spawned");
    return spawnedId;
}

void NPCManager::remove(NPCID id) {
    int index = findNPCIndex(id);
    if (index < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Attempted to remove non-existent NPC ID: %u", id);
        return;
    }

    fireEvent(id, "removed");
    npcs_.erase(npcs_.begin() + index);
    SDL_Log("Removed NPC ID: %u", id);
}

void NPCManager::update(float deltaTime, const glm::vec3& playerPosition, PhysicsWorld* physics) {
    for (auto& npc : npcs_) {
        if (!npc.isAlive()) {
            continue;
        }

        // Update perception first (this feeds data to the behavior tree)
        npc.perception.update(deltaTime, npc.transform.position, npc.transform.forward(),
                              playerPosition, npc.config, physics);

        // Update attack cooldown
        npc.attackCooldownTimer = std::max(0.0f, npc.attackCooldownTimer - deltaTime);

        // Update behavior tree
        if (npc.behaviorTree) {
            npc.behaviorTree->tick(&npc, playerPosition, physics, deltaTime);
        }

        // Apply velocity to position
        if (glm::length(npc.velocity) > 0.001f) {
            npc.transform.position += npc.velocity * deltaTime;
        }

        // Update alert level for visual feedback (smooth transition)
        float targetAlert = 0.0f;
        if (npc.behaviorState == BehaviorState::Attack) {
            targetAlert = 1.0f;
        } else if (npc.behaviorState == BehaviorState::Chase || npc.behaviorState == BehaviorState::Flee) {
            targetAlert = 0.7f;
        } else if (npc.perception.awareness > npc.config.detectionThreshold) {
            targetAlert = npc.perception.awareness * 0.5f;
        }
        npc.alertLevel += (targetAlert - npc.alertLevel) * (1.0f - std::exp(-5.0f * deltaTime));

        // Update state timer
        npc.stateTimer += deltaTime;
    }
}

NPC* NPCManager::getNPC(NPCID id) {
    int index = findNPCIndex(id);
    return (index >= 0) ? &npcs_[index] : nullptr;
}

const NPC* NPCManager::getNPC(NPCID id) const {
    int index = findNPCIndex(id);
    return (index >= 0) ? &npcs_[index] : nullptr;
}

std::vector<NPC*> NPCManager::getNPCsInRadius(const glm::vec3& position, float radius) {
    std::vector<NPC*> result;
    float radiusSq = radius * radius;

    for (auto& npc : npcs_) {
        if (!npc.isAlive()) continue;

        glm::vec3 diff = npc.transform.position - position;
        if (glm::dot(diff, diff) <= radiusSq) {
            result.push_back(&npc);
        }
    }

    return result;
}

std::vector<NPC*> NPCManager::getActiveHostiles() {
    std::vector<NPC*> result;

    for (auto& npc : npcs_) {
        if (!npc.isAlive()) continue;

        if (npc.hostility == HostilityLevel::Hostile &&
            (npc.behaviorState == BehaviorState::Chase ||
             npc.behaviorState == BehaviorState::Attack)) {
            result.push_back(&npc);
        }
    }

    return result;
}

void NPCManager::applyAreaDamage(const glm::vec3& center, float radius, float damage,
                                  const glm::vec3& attackerPosition) {
    auto npcsInRange = getNPCsInRadius(center, radius);

    for (auto* npc : npcsInRange) {
        // Damage falls off with distance
        float dist = glm::length(npc->transform.position - center);
        float falloff = 1.0f - (dist / radius);
        float actualDamage = damage * falloff;

        applyDamage(*npc, actualDamage, attackerPosition);
    }
}

void NPCManager::applyDamage(NPC& npc, float damage, const glm::vec3& attackerPosition) {
    npc.health = std::max(0.0f, npc.health - damage);

    if (npc.health <= 0.0f) {
        SDL_Log("NPC %s died", npc.name.c_str());
        fireEvent(npc.id, "died");
        return;
    }

    // Become hostile when attacked (unless afraid)
    if (npc.hostility != HostilityLevel::Afraid) {
        if (npc.hostility != HostilityLevel::Hostile) {
            npc.hostility = HostilityLevel::Hostile;
            npc.lastTrigger = HostilityTrigger::PlayerAttack;
            npc.hostilityTimer = 0.0f;

            // Recreate behavior tree for new hostility
            npc.behaviorTree = NPCBehaviorTrees::CreateBehaviorTree(HostilityLevel::Hostile);
        }
    }

    // Update perception with attacker position
    npc.perception.lastKnownPosition = attackerPosition;
    npc.perception.hasLastKnownPosition = true;
    npc.perception.awareness = 1.0f;  // Full awareness when attacked

    SDL_Log("NPC %s took %.1f damage (%.1f remaining)", npc.name.c_str(), damage, npc.health);
    fireEvent(npc.id, "damaged");
}

size_t NPCManager::getAliveCount() const {
    return std::count_if(npcs_.begin(), npcs_.end(),
                         [](const NPC& npc) { return npc.isAlive(); });
}

size_t NPCManager::getHostileCount() const {
    return std::count_if(npcs_.begin(), npcs_.end(),
                         [](const NPC& npc) {
                             return npc.isAlive() && npc.hostility == HostilityLevel::Hostile;
                         });
}

void NPCManager::clear() {
    npcs_.clear();
    SDL_Log("Cleared all NPCs");
}

std::string NPCManager::getDebugSummary() const {
    std::ostringstream ss;
    ss << "NPCs: " << getAliveCount() << "/" << npcs_.size() << " alive, "
       << getHostileCount() << " hostile [BehaviorTree AI]";

    if (!npcs_.empty()) {
        ss << "\n";
        for (const auto& npc : npcs_) {
            ss << "  [" << npc.id << "] " << npc.name
               << " H:" << static_cast<int>(npc.hostility)
               << " S:" << static_cast<int>(npc.behaviorState)
               << " A:" << static_cast<int>(npc.perception.awareness * 100) << "%"
               << " HP:" << static_cast<int>(npc.health) << "/"
               << static_cast<int>(npc.maxHealth);
            if (npc.perception.canSeePlayer) {
                ss << " [SEES]";
            }
            if (npc.behaviorTree) {
                ss << " [BT]";
            }
            ss << "\n";
        }
    }

    return ss.str();
}

int NPCManager::findNPCIndex(NPCID id) const {
    for (size_t i = 0; i < npcs_.size(); ++i) {
        if (npcs_[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void NPCManager::fireEvent(NPCID id, const std::string& event) {
    if (eventCallback_) {
        eventCallback_(id, event);
    }
}

void NPCManager::updateAnimations(float deltaTime, SkinnedMeshRenderer& renderer, uint32_t frameIndex) {
    if (!sharedCharacter_) {
        return;  // No shared character set, can't animate NPCs
    }

    // Assign bone slots and update animation for each NPC
    // Slot 0 is reserved for the player
    uint32_t nextSlot = 1;

    for (auto& npc : npcs_) {
        if (!npc.isAlive()) {
            continue;
        }

        // Skip if we've run out of slots
        if (nextSlot >= MAX_SKINNED_CHARACTERS) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Too many NPCs for skinned rendering");
            break;
        }

        // Assign slot to NPC
        npc.boneSlot = nextSlot++;

        // Update animation time based on movement speed
        float speed = glm::length(npc.velocity);
        float animSpeed = 1.0f;

        // Choose animation based on behavior state
        const auto& animations = sharedCharacter_->getAnimations();
        size_t targetAnim = 0;  // Default to first animation (usually idle)

        // Find appropriate animation by name
        for (size_t i = 0; i < animations.size(); i++) {
            const std::string& animName = animations[i].name;

            if (npc.behaviorState == BehaviorState::Idle && animName.find("Idle") != std::string::npos) {
                targetAnim = i;
                animSpeed = 1.0f;
                break;
            } else if ((npc.behaviorState == BehaviorState::Patrol ||
                        npc.behaviorState == BehaviorState::Return) &&
                       animName.find("Walk") != std::string::npos) {
                targetAnim = i;
                animSpeed = speed / 1.5f;  // Adjust for walk speed
                break;
            } else if ((npc.behaviorState == BehaviorState::Chase ||
                        npc.behaviorState == BehaviorState::Flee) &&
                       animName.find("Run") != std::string::npos) {
                targetAnim = i;
                animSpeed = speed / 4.0f;  // Adjust for run speed
                break;
            } else if (npc.behaviorState == BehaviorState::Attack &&
                       animName.find("Attack") != std::string::npos) {
                targetAnim = i;
                animSpeed = 1.0f;
                break;
            }
        }

        npc.currentAnimation = targetAnim;

        // Update animation time
        if (targetAnim < animations.size()) {
            const auto& clip = animations[targetAnim];
            npc.animationTime += deltaTime * animSpeed;

            // Loop animation
            if (clip.duration > 0.0f) {
                while (npc.animationTime >= clip.duration) {
                    npc.animationTime -= clip.duration;
                }
            }
        }

        // Compute bone matrices for this NPC
        std::vector<glm::mat4> boneMatrices;
        computeNPCBoneMatrices(npc, boneMatrices);

        // Update renderer with this NPC's bones
        renderer.updateBoneMatricesForSlot(frameIndex, npc.boneSlot, boneMatrices);
    }
}

void NPCManager::computeNPCBoneMatrices(NPC& npc, std::vector<glm::mat4>& outBoneMatrices) {
    if (!sharedCharacter_) {
        return;
    }

    // Get skeleton from shared character
    Skeleton& skeleton = sharedCharacter_->getSkeleton();
    const auto& animations = sharedCharacter_->getAnimations();

    if (npc.currentAnimation >= animations.size()) {
        // Just use bind pose if no valid animation
        sharedCharacter_->computeBoneMatrices(outBoneMatrices);
        return;
    }

    // Sample animation at NPC's current time
    const AnimationClip& clip = animations[npc.currentAnimation];

    // We need to temporarily modify the skeleton's local transforms
    // Store original transforms
    std::vector<glm::mat4> originalTransforms;
    originalTransforms.reserve(skeleton.joints.size());
    for (const auto& joint : skeleton.joints) {
        originalTransforms.push_back(joint.localTransform);
    }

    // Apply animation to skeleton
    clip.sample(npc.animationTime, skeleton, true);

    // Compute bone matrices
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    outBoneMatrices.resize(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        outBoneMatrices[i] = globalTransforms[i] * skeleton.joints[i].inverseBindMatrix;
    }

    // Restore original transforms
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        skeleton.joints[i].localTransform = originalTransforms[i];
    }
}

void NPCManager::render(VkCommandBuffer cmd, uint32_t frameIndex, SkinnedMeshRenderer& renderer) {
    if (!sharedCharacter_) {
        return;  // No shared character, can't render
    }

    for (const auto& npc : npcs_) {
        if (!npc.isAlive()) {
            continue;
        }

        if (npc.boneSlot == 0) {
            continue;  // Slot 0 reserved for player, skip unassigned NPCs
        }

        // Build model matrix for NPC
        // Scale down slightly and offset Y for proper ground placement
        glm::mat4 modelMatrix = npc.transform.toMatrix();

        // Apply same scale as player (0.01 for Mixamo characters)
        modelMatrix = modelMatrix * glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));

        // Get tint color based on hostility
        glm::vec4 tintColor = npc.getTintColor();

        // Render NPC
        renderer.recordNPC(cmd, frameIndex, npc.boneSlot, modelMatrix, tintColor, *sharedCharacter_);
    }
}
