#include "NPCManager.h"
#include "NPCBehavior.h"
#include "physics/PhysicsSystem.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <sstream>

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

    npcs_.push_back(std::move(npc));

    SDL_Log("Spawned NPC '%s' (ID: %u) at (%.1f, %.1f, %.1f) with hostility %d",
            info.name.c_str(), npc.id,
            info.position.x, info.position.y, info.position.z,
            static_cast<int>(info.hostility));

    fireEvent(npc.id, "spawned");
    return npc.id;
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

        // Update behavior and get desired velocity
        glm::vec3 velocity = NPCBehavior::update(npc, deltaTime, playerPosition, physics);

        // Apply velocity to position (simple integration - could use physics character controllers)
        if (glm::length(velocity) > 0.001f) {
            npc.transform.position += velocity * deltaTime;

            // Keep on terrain (would need terrain height query here)
            // For now, just keep Y at spawn height
            npc.transform.position.y = npc.spawnPosition.y;
        }
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

        NPCBehavior::applyDamage(*npc, actualDamage, attackerPosition);
    }
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
       << getHostileCount() << " hostile";

    if (!npcs_.empty()) {
        ss << "\n";
        for (const auto& npc : npcs_) {
            ss << "  [" << npc.id << "] " << npc.name
               << " H:" << static_cast<int>(npc.hostility)
               << " S:" << static_cast<int>(npc.behaviorState)
               << " HP:" << static_cast<int>(npc.health) << "/"
               << static_cast<int>(npc.maxHealth);
            if (npc.perception.canSeePlayer) {
                ss << " (sees player)";
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
