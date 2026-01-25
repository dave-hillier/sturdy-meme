#include "NPCSimulation.h"
#include "AnimatedCharacter.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

// Constructor must be defined in .cpp to allow unique_ptr<AnimatedCharacter> with incomplete type in header
NPCSimulation::NPCSimulation(ConstructToken) {}

std::unique_ptr<NPCSimulation> NPCSimulation::create(const InitInfo& info) {
    auto simulation = std::make_unique<NPCSimulation>(ConstructToken{});
    if (!simulation->initInternal(info)) {
        return nullptr;
    }
    return simulation;
}

NPCSimulation::~NPCSimulation() {
    cleanup();
}

bool NPCSimulation::initInternal(const InitInfo& info) {
    allocator_ = info.allocator;
    device_ = info.device;
    commandPool_ = info.commandPool;
    graphicsQueue_ = info.graphicsQueue;
    resourcePath_ = info.resourcePath;
    terrainHeightFunc_ = info.getTerrainHeight;
    sceneOrigin_ = info.sceneOrigin;

    return true;
}

void NPCSimulation::cleanup() {
    characters_.clear();
    data_.clear();
}

size_t NPCSimulation::spawnNPCs(const std::vector<NPCSpawnInfo>& spawnPoints) {
    if (spawnPoints.empty()) {
        return 0;
    }

    // Reserve space
    data_.reserve(spawnPoints.size());
    characters_.reserve(spawnPoints.size());

    std::string characterPath = resourcePath_ + "/assets/characters/fbx/Y Bot.fbx";

    // Additional animation files to load
    std::vector<std::string> additionalAnimations = {
        resourcePath_ + "/assets/animations/Walk Forward.fbx",
        resourcePath_ + "/assets/animations/Running.fbx"
    };

    size_t createdCount = 0;

    for (const auto& spawn : spawnPoints) {
        AnimatedCharacter::InitInfo charInfo{};
        charInfo.path = characterPath;
        charInfo.allocator = allocator_;
        charInfo.device = device_;
        charInfo.commandPool = commandPool_;
        charInfo.queue = graphicsQueue_;

        auto character = AnimatedCharacter::create(charInfo);
        if (!character) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "NPCSimulation: Failed to create NPC character at (%.1f, %.1f)",
                spawn.x, spawn.z);
            continue;
        }

        // Load animations
        character->loadAdditionalAnimations(additionalAnimations);

        // Calculate world position
        glm::vec3 worldPos(spawn.x + sceneOrigin_.x, 0.0f, spawn.z + sceneOrigin_.y);

        // Get terrain height
        if (terrainHeightFunc_) {
            worldPos.y = terrainHeightFunc_(worldPos.x, worldPos.z);
        }

        // Add to data arrays
        size_t npcIndex = data_.addNPC(spawn.templateIndex, worldPos, spawn.yawDegrees);

        // Store character
        characters_.push_back(std::move(character));

        SDL_Log("NPCSimulation: Created NPC %zu at (%.1f, %.1f, %.1f) facing %.0f degrees",
                npcIndex, worldPos.x, worldPos.y, worldPos.z, spawn.yawDegrees);

        createdCount++;
    }

    SDL_Log("NPCSimulation: Created %zu NPCs", createdCount);
    return createdCount;
}

void NPCSimulation::update(float deltaTime, const glm::vec3& cameraPos) {
    if (data_.count() == 0) return;

    // Update LOD levels based on camera position
    if (lodEnabled_) {
        updateLODLevels(cameraPos);
    }

    // Update NPCs based on their LOD level
    updateRealNPCs(deltaTime);
    updateBulkNPCs(deltaTime);
    updateVirtualNPCs(deltaTime);
}

void NPCSimulation::updateLODLevels(const glm::vec3& cameraPos) {
    for (size_t i = 0; i < data_.count(); ++i) {
        const glm::vec3& npcPos = data_.positions[i];
        float distance = glm::distance(cameraPos, npcPos);

        NPCLODLevel newLevel;
        if (distance < LOD_DISTANCE_REAL) {
            newLevel = NPCLODLevel::Real;
        } else if (distance < LOD_DISTANCE_BULK) {
            newLevel = NPCLODLevel::Bulk;
        } else {
            newLevel = NPCLODLevel::Virtual;
        }

        // Track LOD transitions for debugging
        if (data_.lodLevels[i] != newLevel) {
            data_.framesSinceUpdate[i] = 0;  // Reset counter on LOD change
        }

        data_.lodLevels[i] = newLevel;
    }
}

void NPCSimulation::updateVirtualNPCs(float deltaTime) {
    for (size_t i = 0; i < data_.count(); ++i) {
        if (data_.lodLevels[i] != NPCLODLevel::Virtual) continue;

        data_.framesSinceUpdate[i]++;

        // Only update every UPDATE_INTERVAL_VIRTUAL frames
        if (data_.framesSinceUpdate[i] < UPDATE_INTERVAL_VIRTUAL) {
            continue;
        }

        data_.framesSinceUpdate[i] = 0;

        // Minimal update: just advance animation time, no bone matrix computation
        auto& animState = data_.animStates[i];
        if (characters_[i]) {
            // Just update internal time without computing bones
            animState.currentTime += deltaTime * UPDATE_INTERVAL_VIRTUAL * animState.playbackSpeed;
        }
    }
}

void NPCSimulation::updateBulkNPCs(float deltaTime) {
    for (size_t i = 0; i < data_.count(); ++i) {
        if (data_.lodLevels[i] != NPCLODLevel::Bulk) continue;

        data_.framesSinceUpdate[i]++;

        // Only update every UPDATE_INTERVAL_BULK frames
        if (data_.framesSinceUpdate[i] < UPDATE_INTERVAL_BULK) {
            // Use cached bone matrices
            if (characters_[i]) {
                characters_[i]->setSkipAnimationUpdate(true);
            }
            continue;
        }

        data_.framesSinceUpdate[i] = 0;

        // Reduced update: compute bones but at lower frequency
        updateNPCAnimation(i, deltaTime * UPDATE_INTERVAL_BULK);
    }
}

void NPCSimulation::updateRealNPCs(float deltaTime) {
    for (size_t i = 0; i < data_.count(); ++i) {
        if (data_.lodLevels[i] != NPCLODLevel::Real) continue;

        // Full update every frame
        data_.framesSinceUpdate[i] = 0;
        updateNPCAnimation(i, deltaTime);
    }
}

void NPCSimulation::updateNPCAnimation(size_t npcIndex, float deltaTime) {
    if (npcIndex >= characters_.size() || !characters_[npcIndex]) return;

    auto& character = characters_[npcIndex];
    character->setSkipAnimationUpdate(false);

    // Build world transform for this NPC
    glm::mat4 worldTransform = buildNPCTransform(npcIndex);

    // Update animation (idle state - movementSpeed = 0, grounded, not jumping)
    character->update(deltaTime, allocator_, device_, commandPool_, graphicsQueue_,
                      0.0f,  // movementSpeed (idle)
                      true,  // isGrounded
                      false, // isJumping
                      worldTransform);

    // Cache bone matrices for LOD skipping
    character->computeBoneMatrices(data_.cachedBoneMatrices[npcIndex]);
}

glm::mat4 NPCSimulation::buildCharacterTransform(const glm::vec3& position, float yawRadians) const {
    glm::mat4 transform = glm::mat4(1.0f);
    transform = glm::translate(transform, position);
    transform = glm::rotate(transform, yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
    return transform;
}

glm::mat4 NPCSimulation::buildNPCTransform(size_t npcIndex) const {
    if (npcIndex >= data_.count()) {
        return glm::mat4(1.0f);
    }
    return buildCharacterTransform(data_.positions[npcIndex],
                                   glm::radians(data_.yawDegrees[npcIndex]));
}

AnimatedCharacter* NPCSimulation::getCharacter(size_t npcIndex) {
    if (npcIndex >= characters_.size()) return nullptr;
    return characters_[npcIndex].get();
}

const AnimatedCharacter* NPCSimulation::getCharacter(size_t npcIndex) const {
    if (npcIndex >= characters_.size()) return nullptr;
    return characters_[npcIndex].get();
}

void NPCSimulation::setRenderableIndex(size_t npcIndex, size_t renderableIndex) {
    if (npcIndex < data_.renderableIndices.size()) {
        data_.renderableIndices[npcIndex] = renderableIndex;
    }
}
