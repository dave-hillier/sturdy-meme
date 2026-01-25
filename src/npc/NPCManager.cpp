#include "NPCManager.h"
#include "BehaviorTree.h"
#include "NPCBehaviorTrees.h"
#include "physics/PhysicsSystem.h"
#include "animation/AnimatedCharacter.h"
#include "animation/SkinnedMeshRenderer.h"
#include "animation/Animation.h"
#include "loaders/GLTFLoader.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
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

    // Archetype and schedule locations
    npc.archetype = info.archetype;
    npc.homeLocation = (glm::length(info.homeLocation) > 0.01f) ? info.homeLocation : info.position;
    npc.workLocation = (glm::length(info.workLocation) > 0.01f) ? info.workLocation : info.position;
    npc.socialLocation = (glm::length(info.socialLocation) > 0.01f) ? info.socialLocation : info.position;

    // Deterministic seeding
    npc.spawnSeed = (info.seed != 0) ? info.seed :
        static_cast<uint32_t>(info.position.x * 1000.0f + info.position.z * 7919.0f);

    // Initialize needs based on time of day
    npc.needs.hunger = 0.3f;
    npc.needs.tiredness = 0.2f;

    // Start in Real state - updateLODStates will demote to Bulk/Virtual if far from player
    // This ensures newly spawned NPCs are immediately visible
    npc.lodData.state = NPCLODState::Real;

    // Create behavior tree based on hostility type
    npc.behaviorTree = NPCBehaviorTrees::CreateBehaviorTree(info.hostility);

    NPCID spawnedId = npc.id;
    npcs_.push_back(std::move(npc));

    SDL_Log("Spawned NPC '%s' (ID: %u) at (%.1f, %.1f, %.1f) hostility=%d archetype=%d [LOD+BT]",
            info.name.c_str(), spawnedId,
            info.position.x, info.position.y, info.position.z,
            static_cast<int>(info.hostility), static_cast<int>(info.archetype));

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
    // Advance game time
    float gameHours = (deltaTime * timeScale_) / 3600.0f;  // Convert to hours
    timeOfDay_ += gameHours;
    if (timeOfDay_ >= 24.0f) timeOfDay_ -= 24.0f;

    // Update LOD states for all NPCs based on distance to player
    updateLODStates(playerPosition);

    // Update systemic events
    updateSystemicEvents(deltaTime);

    // Update NPCs based on their LOD state
    // Virtual NPCs: minimal updates (needs only)
    updateVirtualNPCs(deltaTime, gameHours);

    // Bulk NPCs: simplified updates (needs + schedule + simple movement)
    updateBulkNPCs(deltaTime, gameHours, playerPosition);

    // Real NPCs: full updates (perception + behavior tree + physics)
    updateRealNPCs(deltaTime, gameHours, playerPosition, physics);
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
       << getHostileCount() << " hostile\n";
    ss << "  LOD: V=" << getVirtualCount() << " B=" << getBulkCount() << " R=" << getRealCount();
    ss << " | Time: " << static_cast<int>(timeOfDay_) << ":"
       << std::setfill('0') << std::setw(2) << static_cast<int>((timeOfDay_ - static_cast<int>(timeOfDay_)) * 60);
    ss << " | Events: " << activeEvents_.size();

    if (!npcs_.empty() && npcs_.size() <= 10) {
        ss << "\n";
        for (const auto& npc : npcs_) {
            const char* lodStr = npc.isVirtual() ? "V" : (npc.isBulk() ? "B" : "R");
            ss << "  [" << npc.id << "] " << npc.name
               << " " << lodStr
               << " H:" << static_cast<int>(npc.hostility)
               << " S:" << static_cast<int>(npc.behaviorState);
            if (npc.isReal()) {
                ss << " A:" << static_cast<int>(npc.perception.awareness * 100) << "%";
                if (npc.perception.canSeePlayer) ss << " [SEES]";
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

    // Reset bone slots for all NPCs first
    for (auto& npc : npcs_) {
        npc.boneSlot = 0;  // 0 means not assigned (slot 0 is reserved for player)
    }

    // Assign bone slots and update animation only for VISIBLE NPCs
    // Slot 0 is reserved for the player
    uint32_t nextSlot = 1;

    for (auto& npc : npcs_) {
        if (!npc.isAlive()) {
            continue;
        }

        // Only process visible NPCs (Bulk and Real states)
        if (!npc.isVisible()) {
            continue;
        }

        // Skip if we've run out of slots
        if (nextSlot >= MAX_SKINNED_CHARACTERS) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Too many visible NPCs for skinned rendering");
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
        static bool loggedOnce = false;
        if (!loggedOnce) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NPCManager::render - No shared character set");
            loggedOnce = true;
        }
        return;  // No shared character, can't render
    }

    uint32_t renderedCount = 0;
    for (const auto& npc : npcs_) {
        if (!npc.isAlive()) {
            continue;
        }

        // Only render visible NPCs (Bulk and Real states)
        if (!npc.isVisible()) {
            continue;
        }

        if (npc.boneSlot == 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NPC %s visible but boneSlot=0!", npc.name.c_str());
            continue;  // Slot 0 reserved for player, skip unassigned NPCs
        }

        // Build model matrix for NPC
        glm::mat4 modelMatrix = npc.transform.toMatrix();

        // Apply same scale as player (0.01 for Mixamo characters)
        modelMatrix = modelMatrix * glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));

        // Get tint color based on hostility
        glm::vec4 tintColor = npc.getTintColor();

        // Render NPC
        renderer.recordNPC(cmd, frameIndex, npc.boneSlot, modelMatrix, tintColor, *sharedCharacter_);
        renderedCount++;
    }

    // Log once per second approximately (every 60 frames)
    static uint32_t frameCounter = 0;
    if (++frameCounter >= 60) {
        frameCounter = 0;
        SDL_Log("NPCManager::render - rendered %u/%zu NPCs (V=%zu B=%zu R=%zu)",
                renderedCount, npcs_.size(), getVirtualCount(), getBulkCount(), getRealCount());
    }
}

void NPCManager::generateRenderData(NPCRenderData& outData, const NPCRenderConfig& config) const {
    outData.clear();
    outData.reserve(npcs_.size());

    outData.totalCount = static_cast<uint32_t>(npcs_.size());

    for (const auto& npc : npcs_) {
        if (!npc.isAlive()) {
            continue;
        }

        // Count LOD states
        if (npc.isVirtual()) outData.virtualCount++;
        else if (npc.isBulk()) outData.bulkCount++;
        else outData.realCount++;

        // Skip non-visible NPCs unless debug mode forces visibility
        bool shouldRender = npc.isVisible() || config.debugForceVisible;
        if (!shouldRender) {
            continue;
        }

        // Skip NPCs without assigned bone slots
        if (npc.boneSlot == 0) {
            continue;
        }

        NPCRenderInstance instance;
        instance.visible = true;
        instance.boneSlot = npc.boneSlot;
        instance.tintColor = npc.getTintColor();

        // Build model matrix with scale
        instance.modelMatrix = npc.transform.toMatrix();
        instance.modelMatrix = instance.modelMatrix * glm::scale(glm::mat4(1.0f), glm::vec3(config.characterScale));

        outData.instances.push_back(instance);
        outData.visibleCount++;
    }
}

void NPCManager::renderWithData(VkCommandBuffer cmd, uint32_t frameIndex,
                                 SkinnedMeshRenderer& renderer, const NPCRenderData& data) {
    if (!sharedCharacter_) {
        return;  // No shared character, can't render
    }

    for (const auto& instance : data.instances) {
        if (!instance.visible) {
            continue;
        }

        renderer.recordNPC(cmd, frameIndex, instance.boneSlot,
                          instance.modelMatrix, instance.tintColor, *sharedCharacter_);
    }
}

// =============================================================================
// LOD System Implementation
// =============================================================================

size_t NPCManager::getVirtualCount() const {
    return std::count_if(npcs_.begin(), npcs_.end(),
        [](const NPC& npc) { return npc.isAlive() && npc.isVirtual(); });
}

size_t NPCManager::getBulkCount() const {
    return std::count_if(npcs_.begin(), npcs_.end(),
        [](const NPC& npc) { return npc.isAlive() && npc.isBulk(); });
}

size_t NPCManager::getRealCount() const {
    return std::count_if(npcs_.begin(), npcs_.end(),
        [](const NPC& npc) { return npc.isAlive() && npc.isReal(); });
}

void NPCManager::updateLODStates(const glm::vec3& playerPosition) {
    uint32_t realCount = 0;
    uint32_t bulkCount = 0;

    // First pass: calculate distances and count current states
    for (auto& npc : npcs_) {
        if (!npc.isAlive()) continue;

        glm::vec3 diff = npc.transform.position - playerPosition;
        npc.lodData.distanceToPlayer = glm::length(diff);

        // Calculate priority based on hostility and distance
        float priorityScore = 0.0f;
        if (npc.hostility == HostilityLevel::Hostile) priorityScore += 100.0f;
        if (npc.perception.canSeePlayer) priorityScore += 50.0f;
        priorityScore += npc.needs.getUrgencyScore() * 10.0f;
        priorityScore -= npc.lodData.distanceToPlayer;
        npc.lodData.updatePriority = static_cast<uint8_t>(glm::clamp(priorityScore, 0.0f, 255.0f));
    }

    // Sort by priority for LOD budget allocation
    std::vector<NPC*> sortedNPCs;
    for (auto& npc : npcs_) {
        if (npc.isAlive()) sortedNPCs.push_back(&npc);
    }
    std::sort(sortedNPCs.begin(), sortedNPCs.end(),
        [](const NPC* a, const NPC* b) {
            return a->lodData.updatePriority > b->lodData.updatePriority;
        });

    // Assign LOD states respecting budgets
    for (NPC* npc : sortedNPCs) {
        float dist = npc->lodData.distanceToPlayer;
        NPCLODState currentState = npc->lodData.state;
        NPCLODState newState = currentState;

        // Determine desired state based on distance (with hysteresis)
        if (currentState == NPCLODState::Virtual) {
            if (dist < lodConfig_.virtualToRealDistance) {
                newState = (dist < lodConfig_.bulkToRealDistance) ? NPCLODState::Real : NPCLODState::Bulk;
            }
        } else if (currentState == NPCLODState::Bulk) {
            if (dist < lodConfig_.bulkToRealDistance) {
                newState = NPCLODState::Real;
            } else if (dist > lodConfig_.realToVirtualDistance) {
                newState = NPCLODState::Virtual;
            }
        } else {  // Real
            if (dist > lodConfig_.realToBulkDistance) {
                newState = (dist > lodConfig_.realToVirtualDistance) ? NPCLODState::Virtual : NPCLODState::Bulk;
            }
        }

        // Apply budget constraints
        if (newState == NPCLODState::Real && realCount >= lodConfig_.maxRealNPCs) {
            newState = NPCLODState::Bulk;
        }
        if (newState == NPCLODState::Bulk && bulkCount >= lodConfig_.maxBulkNPCs) {
            newState = NPCLODState::Virtual;
        }

        // Update counts
        if (newState == NPCLODState::Real) realCount++;
        else if (newState == NPCLODState::Bulk) bulkCount++;

        npc->lodData.state = newState;
    }
}

void NPCManager::updateVirtualNPCs(float deltaTime, float gameHours) {
    for (auto& npc : npcs_) {
        if (!npc.isAlive() || !npc.isVirtual()) continue;

        npc.lodData.timeSinceLastUpdate += deltaTime;

        // Only update virtual NPCs periodically (every 5-15 seconds)
        if (npc.lodData.timeSinceLastUpdate < lodConfig_.virtualUpdateInterval) {
            continue;
        }
        npc.lodData.timeSinceLastUpdate = 0.0f;

        // Update needs
        updateNPCNeeds(npc, gameHours * lodConfig_.virtualUpdateInterval);

        // Update schedule (move to scheduled location)
        updateNPCSchedule(npc);

        // Teleport to activity location if far away (virtual NPCs don't animate travel)
        glm::vec3 targetLoc = npc.getActivityLocation(npc.currentActivity);
        float distToTarget = glm::length(targetLoc - npc.transform.position);
        if (distToTarget > 50.0f) {
            // Teleport - we're virtual anyway
            npc.transform.position = targetLoc;
        }
    }
}

void NPCManager::updateBulkNPCs(float deltaTime, float gameHours, const glm::vec3& playerPosition) {
    for (auto& npc : npcs_) {
        if (!npc.isAlive() || !npc.isBulk()) continue;

        npc.lodData.timeSinceLastUpdate += deltaTime;

        // Update bulk NPCs every second
        if (npc.lodData.timeSinceLastUpdate < lodConfig_.bulkUpdateInterval) {
            continue;
        }
        float updateDelta = npc.lodData.timeSinceLastUpdate;
        npc.lodData.timeSinceLastUpdate = 0.0f;

        // Update needs
        updateNPCNeeds(npc, gameHours * updateDelta);

        // Update schedule
        updateNPCSchedule(npc);

        // Simple movement toward activity target
        glm::vec3 targetLoc = npc.activityTarget;
        glm::vec3 toTarget = targetLoc - npc.transform.position;
        float dist = glm::length(toTarget);

        if (dist > 2.0f) {
            // Move toward target at reduced speed
            glm::vec3 dir = toTarget / dist;
            float speed = npc.baseSpeed * 0.5f;  // Slower for bulk NPCs
            npc.transform.position += dir * speed * updateDelta;
            npc.transform.lookAt(targetLoc);
            npc.behaviorState = BehaviorState::Patrol;
        } else {
            npc.velocity = glm::vec3(0.0f);
            npc.behaviorState = BehaviorState::Idle;
        }
    }
}

void NPCManager::updateRealNPCs(float deltaTime, float gameHours,
                                 const glm::vec3& playerPosition, PhysicsWorld* physics) {
    for (auto& npc : npcs_) {
        if (!npc.isAlive() || !npc.isReal()) continue;

        npc.lodData.timeSinceLastUpdate = 0.0f;  // Real NPCs update every frame

        // Update needs
        updateNPCNeeds(npc, gameHours);

        // Update schedule
        updateNPCSchedule(npc);

        // Full perception update
        npc.perception.update(deltaTime, npc.transform.position, npc.transform.forward(),
                              playerPosition, npc.config, physics);

        // Update attack cooldown
        npc.attackCooldownTimer = std::max(0.0f, npc.attackCooldownTimer - deltaTime);

        // Update behavior tree (full AI)
        if (npc.behaviorTree) {
            npc.behaviorTree->tick(&npc, playerPosition, physics, deltaTime);
        }

        // Apply velocity to position
        if (glm::length(npc.velocity) > 0.001f) {
            npc.transform.position += npc.velocity * deltaTime;
        }

        // Update alert level for visual feedback
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

void NPCManager::updateNPCNeeds(NPC& npc, float gameHours) {
    bool isSafe = npc.hostility != HostilityLevel::Hostile &&
                  npc.behaviorState != BehaviorState::Flee;
    bool isWorking = npc.currentActivity == ScheduleActivity::Work ||
                     npc.currentActivity == ScheduleActivity::Patrol;
    bool isSocializing = npc.currentActivity == ScheduleActivity::Socialize;
    bool isEating = npc.currentActivity == ScheduleActivity::Eat;
    bool isResting = npc.currentActivity == ScheduleActivity::Sleep;

    npc.needs.update(gameHours, isSafe, isWorking, isSocializing, isEating, isResting);

    // Needs can override scheduled activity
    auto urgentNeed = npc.needs.getMostUrgentNeed(0.8f);
    if (urgentNeed == NPCNeeds::NeedType::Fear && npc.hostility != HostilityLevel::Hostile) {
        npc.needs.fear += 0.5f;  // Fear increases hostility reaction
    }
    if (urgentNeed == NPCNeeds::NeedType::Aggression && npc.hostility == HostilityLevel::Neutral) {
        // Aggression might make neutral NPCs confront the player
        npc.needs.aggression = glm::clamp(npc.needs.aggression, 0.0f, 1.0f);
    }
}

void NPCManager::updateNPCSchedule(NPC& npc) {
    // Get scheduled activity for current time
    ScheduleActivity scheduled = npc.getScheduledActivity(timeOfDay_);

    // Check if urgent needs override schedule
    auto urgentNeed = npc.needs.getMostUrgentNeed(0.85f);
    if (urgentNeed == NPCNeeds::NeedType::Hunger && scheduled != ScheduleActivity::Eat) {
        scheduled = ScheduleActivity::Eat;
    } else if (urgentNeed == NPCNeeds::NeedType::Tiredness && scheduled != ScheduleActivity::Sleep) {
        scheduled = ScheduleActivity::Sleep;
    }

    npc.currentActivity = scheduled;
    npc.activityTarget = npc.getActivityLocation(scheduled);
}

// =============================================================================
// Systemic Events
// =============================================================================

SystemicEvent* NPCManager::getEventAt(const glm::vec3& position, float radius) {
    float radiusSq = radius * radius;
    for (auto& event : activeEvents_) {
        if (!event.isActive()) continue;
        glm::vec3 diff = event.location - position;
        if (glm::dot(diff, diff) <= radiusSq) {
            return &event;
        }
    }
    return nullptr;
}

void NPCManager::updateSystemicEvents(float deltaTime) {
    // Update existing events
    for (auto& event : activeEvents_) {
        if (event.isActive()) {
            event.elapsed += deltaTime;
        }
    }

    // Remove completed events
    activeEvents_.erase(
        std::remove_if(activeEvents_.begin(), activeEvents_.end(),
            [](const SystemicEvent& e) { return !e.isActive(); }),
        activeEvents_.end());

    // Try to spawn new events periodically
    eventSpawnTimer_ += deltaTime;
    if (eventSpawnTimer_ >= EVENT_CHECK_INTERVAL) {
        eventSpawnTimer_ = 0.0f;
        trySpawnSystemicEvent();
    }
}

void NPCManager::trySpawnSystemicEvent() {
    // Only spawn events if we have enough real NPCs
    if (getRealCount() < 2) return;

    // Find potential instigators for events
    for (auto& npc : npcs_) {
        if (!npc.isAlive() || !npc.isReal()) continue;

        // Hostile NPCs might start fights or muggings
        if (npc.hostility == HostilityLevel::Hostile && npc.needs.aggression > 0.6f) {
            if (canSpawnEvent(SystemicEventType::Fistfight, npc)) {
                // Find a nearby target
                auto nearbyNPCs = getNPCsInRadius(npc.transform.position, 10.0f);
                for (NPC* target : nearbyNPCs) {
                    if (target->id != npc.id && target->hostility != HostilityLevel::Hostile) {
                        SystemicEvent event;
                        event.type = SystemicEventType::Fistfight;
                        event.instigatorId = npc.id;
                        event.targetId = target->id;
                        event.location = (npc.transform.position + target->transform.position) * 0.5f;
                        event.duration = 15.0f;
                        event.playerCanIntervene = true;
                        activeEvents_.push_back(event);
                        SDL_Log("Systemic event: Fistfight between %s and %s",
                                npc.name.c_str(), target->name.c_str());
                        return;  // One event per check
                    }
                }
            }
        }

        // Social NPCs might start conversations
        if (npc.needs.social > 0.7f && npc.behaviorState == BehaviorState::Idle) {
            auto nearbyNPCs = getNPCsInRadius(npc.transform.position, 5.0f);
            for (NPC* target : nearbyNPCs) {
                if (target->id != npc.id && target->behaviorState == BehaviorState::Idle) {
                    SystemicEvent event;
                    event.type = SystemicEventType::Conversation;
                    event.instigatorId = npc.id;
                    event.targetId = target->id;
                    event.location = (npc.transform.position + target->transform.position) * 0.5f;
                    event.duration = 30.0f;
                    event.playerCanIntervene = false;
                    activeEvents_.push_back(event);

                    // Reduce social need for both
                    npc.needs.social -= 0.3f;
                    target->needs.social -= 0.3f;
                    return;
                }
            }
        }
    }
}

bool NPCManager::canSpawnEvent(SystemicEventType type, const NPC& instigator) const {
    // Check if this NPC is already involved in an event
    for (const auto& event : activeEvents_) {
        if (event.instigatorId == instigator.id || event.targetId == instigator.id) {
            return false;
        }
    }

    // Limit total active events
    if (activeEvents_.size() >= 5) {
        return false;
    }

    return true;
}
