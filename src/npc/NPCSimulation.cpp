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
    ecsWorld_ = info.ecsWorld;

    return true;
}

void NPCSimulation::cleanup() {
    // Destroy ECS entities if ECS is enabled
    if (ecsWorld_) {
        for (ecs::Entity entity : npcEntities_) {
            if (ecsWorld_->valid(entity)) {
                ecsWorld_->destroy(entity);
            }
        }
        npcEntities_.clear();
    }

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
    if (ecsWorld_) {
        npcEntities_.reserve(spawnPoints.size());
    }

    std::string characterPath = resourcePath_ + "/assets/characters/fbx/Y Bot.fbx";

    // Additional animation files to load (same as player character)
    std::vector<std::string> additionalAnimations = {
        resourcePath_ + "/assets/characters/fbx/sword and shield idle.fbx",
        resourcePath_ + "/assets/characters/fbx/sword and shield walk.fbx",
        resourcePath_ + "/assets/characters/fbx/sword and shield run.fbx",
        resourcePath_ + "/assets/characters/fbx/sword and shield jump.fbx"
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

        // Add to data arrays (legacy path)
        size_t npcIndex = data_.addNPC(spawn.templateIndex, worldPos, spawn.yawDegrees);

        // Set initial activity state for animation variety
        data_.animStates[npcIndex].activity = spawn.activity;

        // Convert activity enum for ECS
        ecs::NPCActivity ecsActivity = ecs::NPCActivity::Idle;
        switch (spawn.activity) {
            case NPCActivity::Walking: ecsActivity = ecs::NPCActivity::Walking; break;
            case NPCActivity::Running: ecsActivity = ecs::NPCActivity::Running; break;
            default: break;
        }

        // Create ECS entity if ECS is enabled
        if (ecsWorld_) {
            ecs::Entity entity = ecsWorld_->create();

            // Transform - position with height offset for character center
            constexpr float CHARACTER_HEIGHT_OFFSET = 0.9f;
            glm::mat4 transform = glm::mat4(1.0f);
            transform = glm::translate(transform, worldPos + glm::vec3(0.0f, CHARACTER_HEIGHT_OFFSET, 0.0f));
            transform = glm::rotate(transform, glm::radians(spawn.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            ecsWorld_->add<ecs::Transform>(entity, transform);

            // NPC identification
            ecsWorld_->add<ecs::NPCTag>(entity, spawn.templateIndex);
            ecsWorld_->add<ecs::NPCFacing>(entity, spawn.yawDegrees);

            // Animation state
            ecs::NPCAnimationState animState;
            animState.activity = ecsActivity;
            ecsWorld_->add<ecs::NPCAnimationState>(entity, animState);

            // LOD controller
            ecsWorld_->add<ecs::NPCLODController>(entity);

            // Bone cache for LOD skipping
            ecsWorld_->add<ecs::NPCBoneCache>(entity);

            // Skinned mesh reference (link to AnimatedCharacter)
            ecsWorld_->add<ecs::SkinnedMeshRef>(entity, character.get(), npcIndex);

            // Bounding sphere for culling (approximate character bounds)
            ecsWorld_->add<ecs::BoundingSphere>(entity, glm::vec3(0.0f, 1.0f, 0.0f), 1.0f);

            // Mark as visible initially
            ecsWorld_->add<ecs::Visible>(entity);

            npcEntities_.push_back(entity);
        }

        // Store character
        characters_.push_back(std::move(character));

        const char* activityName = spawn.activity == NPCActivity::Idle ? "idle" :
                                   spawn.activity == NPCActivity::Walking ? "walking" : "running";
        SDL_Log("NPCSimulation: Created NPC %zu at (%.1f, %.1f, %.1f) facing %.0f degrees (%s)%s",
                npcIndex, worldPos.x, worldPos.y, worldPos.z, spawn.yawDegrees, activityName,
                ecsWorld_ ? " [ECS]" : "");

        createdCount++;
    }

    SDL_Log("NPCSimulation: Created %zu NPCs%s", createdCount, ecsWorld_ ? " with ECS entities" : "");
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

    // Determine movement speed based on NPC's activity state
    // These values drive the animation state machine blend (idle/walk/run)
    float movementSpeed = 0.0f;
    switch (data_.animStates[npcIndex].activity) {
        case NPCActivity::Idle:
            movementSpeed = 0.0f;
            break;
        case NPCActivity::Walking:
            movementSpeed = 1.5f;  // Walk speed (m/s)
            break;
        case NPCActivity::Running:
            movementSpeed = 5.0f;  // Run speed (m/s)
            break;
    }

    // Update animation with activity-appropriate movement speed
    character->update(deltaTime, allocator_, device_, commandPool_, graphicsQueue_,
                      movementSpeed,
                      true,  // isGrounded
                      false, // isJumping
                      worldTransform);

    // Cache bone matrices for LOD skipping
    character->computeBoneMatrices(data_.cachedBoneMatrices[npcIndex]);
}

glm::mat4 NPCSimulation::buildCharacterTransform(const glm::vec3& position, float yawRadians) const {
    // Character model origin is at the center, but position is at ground level.
    // Add vertical offset to raise the character so feet are on the ground.
    // This matches the player character offset (CAPSULE_HEIGHT * 0.5f = 0.9m).
    constexpr float CHARACTER_HEIGHT_OFFSET = 0.9f;

    glm::mat4 transform = glm::mat4(1.0f);
    transform = glm::translate(transform, position + glm::vec3(0.0f, CHARACTER_HEIGHT_OFFSET, 0.0f));
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

void NPCSimulation::updateECS(float deltaTime, const glm::vec3& cameraPos) {
    if (!ecsWorld_ || npcEntities_.empty()) {
        // Fall back to legacy update if ECS not enabled
        update(deltaTime, cameraPos);
        return;
    }

    // Update LOD levels based on camera distance using ECS query
    for (auto [entity, transform, lodCtrl] : ecsWorld_->view<ecs::Transform, ecs::NPCLODController>().each()) {
        glm::vec3 npcPos = transform.position();
        float distance = glm::distance(cameraPos, npcPos);

        ecs::NPCLODLevel newLevel;
        if (distance < ecs::NPCLODController::DISTANCE_REAL) {
            newLevel = ecs::NPCLODLevel::Real;
        } else if (distance < ecs::NPCLODController::DISTANCE_BULK) {
            newLevel = ecs::NPCLODLevel::Bulk;
        } else {
            newLevel = ecs::NPCLODLevel::Virtual;
        }

        // Reset frame counter on LOD change
        if (lodCtrl.level != newLevel) {
            lodCtrl.framesSinceUpdate = 0;
        }

        lodCtrl.level = newLevel;
        lodCtrl.framesSinceUpdate++;
    }

    // Update NPC animations using ECS query
    for (auto [entity, transform, lodCtrl, animState, skinnedRef] :
         ecsWorld_->view<ecs::Transform, ecs::NPCLODController, ecs::NPCAnimationState, ecs::SkinnedMeshRef>().each()) {

        // Check if we should update this frame based on LOD
        if (!lodCtrl.shouldUpdate()) {
            // Skip update, use cached bones
            if (skinnedRef.valid()) {
                auto* character = static_cast<AnimatedCharacter*>(skinnedRef.character);
                if (character) {
                    character->setSkipAnimationUpdate(true);
                }
            }
            continue;
        }

        // Reset frame counter after update
        lodCtrl.framesSinceUpdate = 0;

        if (!skinnedRef.valid()) continue;

        auto* character = static_cast<AnimatedCharacter*>(skinnedRef.character);
        if (!character) continue;

        character->setSkipAnimationUpdate(false);

        // Get movement speed from activity
        float movementSpeed = ecs::NPCLODController::getMovementSpeed(animState.activity);

        // Calculate effective delta time for LOD-adjusted updates
        float effectiveDelta = deltaTime;
        if (lodCtrl.level == ecs::NPCLODLevel::Bulk) {
            effectiveDelta *= static_cast<float>(ecs::NPCLODController::INTERVAL_BULK);
        } else if (lodCtrl.level == ecs::NPCLODLevel::Virtual) {
            effectiveDelta *= static_cast<float>(ecs::NPCLODController::INTERVAL_VIRTUAL);
        }

        // Update animation
        character->update(effectiveDelta, allocator_, device_, commandPool_, graphicsQueue_,
                          movementSpeed, true, false, transform.matrix);

        // Cache bone matrices (store in ECS component if available)
        if (ecsWorld_->has<ecs::NPCBoneCache>(entity)) {
            auto& boneCache = ecsWorld_->get<ecs::NPCBoneCache>(entity);
            character->computeBoneMatrices(boneCache.matrices);
        }

        // Also update legacy cache for backward compatibility
        if (skinnedRef.npcIndex < data_.cachedBoneMatrices.size()) {
            character->computeBoneMatrices(data_.cachedBoneMatrices[skinnedRef.npcIndex]);
        }
    }
}
