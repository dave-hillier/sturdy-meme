#include "SceneManager.h"
#include "lighting/LightSystem.h"
#include "ecs/Components.h"
#include <SDL3/SDL.h>
#include <algorithm>

std::unique_ptr<SceneManager> SceneManager::create(SceneBuilder::InitInfo& builderInfo) {
    auto system = std::make_unique<SceneManager>(ConstructToken{});
    if (!system->initInternal(builderInfo)) {
        return nullptr;
    }
    return system;
}

SceneManager::~SceneManager() {
    cleanup();
}

bool SceneManager::initInternal(SceneBuilder::InitInfo& builderInfo) {
    // Store for cleanup
    storedAllocator = builderInfo.allocator;
    storedDevice = builderInfo.device;

    // Store terrain height function for physics placement
    terrainHeightFunc = builderInfo.getTerrainHeight;

    // Store scene origin for light positioning
    sceneOrigin = builderInfo.sceneOrigin;

    // Initialize scene builder (meshes, textures, objects)
    sceneBuilder = SceneBuilder::create(builderInfo);
    if (!sceneBuilder) {
        SDL_Log("Failed to initialize SceneBuilder");
        return false;
    }

    // Initialize scene lights
    initializeSceneLights();

    SDL_Log("SceneManager initialized successfully");
    return true;
}

float SceneManager::getTerrainHeight(float x, float z) const {
    if (terrainHeightFunc) {
        return terrainHeightFunc(x, z);
    }
    return 0.0f;
}

void SceneManager::initPhysics(PhysicsWorld& physics) {
    // Store physics pointer for deferred initialization callback
    storedPhysics_ = &physics;

    // Register callback for when deferred renderables are created
    sceneBuilder->setOnRenderablesCreated([this]() {
        if (storedPhysics_ && scenePhysicsBodies.empty()) {
            SDL_Log("SceneManager: Deferred renderables created, initializing physics bodies...");
            initializeScenePhysics(*storedPhysics_);
        }
    });

    // Initialize immediately if renderables already exist
    if (sceneBuilder->hasRenderables()) {
        initializeScenePhysics(physics);
    }
}

void SceneManager::initTerrainPhysics(PhysicsWorld& physics, const float* heightSamples,
                                       uint32_t sampleCount, float worldSize, float heightScale) {
    // Call overload with no hole mask
    initTerrainPhysics(physics, heightSamples, nullptr, sampleCount, worldSize, heightScale);
}

void SceneManager::initTerrainPhysics(PhysicsWorld& physics, const float* heightSamples,
                                       const uint8_t* holeMask, uint32_t sampleCount,
                                       float worldSize, float heightScale) {
    // Create heightfield collision shape from terrain data (with optional hole mask)
    PhysicsBodyID terrainBody = physics.createTerrainHeightfield(
        heightSamples, holeMask, sampleCount, worldSize, heightScale
    );

    if (terrainBody != INVALID_BODY_ID) {
        SDL_Log("Terrain heightfield physics initialized%s", holeMask ? " (with hole mask)" : "");
    } else {
        SDL_Log("Failed to create terrain heightfield, falling back to flat ground");
        physics.createTerrainDisc(worldSize * 0.5f, 0.0f);
    }
}

void SceneManager::cleanup() {
    // SceneBuilder is RAII-managed, just reset the unique_ptr
    sceneBuilder.reset();
}

void SceneManager::update(PhysicsWorld& physics) {
    updatePhysicsToScene(physics);
}

void SceneManager::updatePlayerTransform(const glm::mat4& transform) {
    sceneBuilder->updatePlayerTransform(transform);
}

void SceneManager::initializeScenePhysics(PhysicsWorld& physics) {
    // NOTE: Terrain physics is now initialized separately via initTerrainPhysics()
    // which creates a heightfield from the TerrainSystem's height data

    const auto& sceneObjects = sceneBuilder->getRenderables();

    // Resize physics bodies array to match scene objects
    scenePhysicsBodies.resize(sceneObjects.size(), INVALID_BODY_ID);

    const float spawnOffset = 0.1f;

    if (!ecsWorld_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SceneManager: No ECS world during physics init - bodies will be linked later");
        return;
    }

    size_t bodyCount = 0;
    for (auto [entity, shapeInfo, transform] :
         ecsWorld_->view<ecs::PhysicsShapeInfo, ecs::Transform>().each()) {

        // Find the renderable index for this entity via pointer identity
        const Renderable* renderable = sceneBuilder->getRenderableForEntity(entity);
        if (!renderable) continue;

        size_t objIndex = static_cast<size_t>(renderable - sceneObjects.data());
        if (objIndex >= sceneObjects.size()) continue;

        glm::vec3 pos = transform.position();

        if (shapeInfo.shapeType == ecs::PhysicsShapeType::Box) {
            scenePhysicsBodies[objIndex] = physics.createBox(
                glm::vec3(pos.x, pos.y + spawnOffset, pos.z),
                shapeInfo.halfExtents, shapeInfo.mass);
        } else {
            scenePhysicsBodies[objIndex] = physics.createSphere(
                glm::vec3(pos.x, pos.y + spawnOffset, pos.z),
                shapeInfo.radius(), shapeInfo.mass);
        }

        // Add PhysicsBody component to entity
        if (scenePhysicsBodies[objIndex] != INVALID_BODY_ID) {
            ecsWorld_->add<ecs::PhysicsBody>(entity,
                static_cast<ecs::PhysicsBodyId>(scenePhysicsBodies[objIndex]));
            bodyCount++;
        }
    }
    SDL_Log("Scene physics initialized with %zu bodies from ECS components", bodyCount);
}

void SceneManager::initializeECSLights() {
    // Only initialize if ECS world is available
    if (!ecsWorld_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "initializeECSLights called without ECS world");
        return;
    }

    // Re-initialize scene lights using ECS
    initializeSceneLights();
}

void SceneManager::initializeSceneLights() {
    if (!ecsWorld_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "initializeSceneLights: No ECS world available, skipping light creation");
        return;
    }

    // Helper to apply scene origin offset
    auto worldPos = [this](float localX, float localZ) -> glm::vec2 {
        return glm::vec2(localX + sceneOrigin.x, localZ + sceneOrigin.y);
    };

    // Orb light - flickering torch as child of the emissive orb entity.
    // The light follows the orb automatically via ECS hierarchy (parent-child transform propagation).
    ecs::Entity orbEntity = sceneBuilder ? sceneBuilder->getEmissiveOrbEntity() : ecs::NullEntity;
    if (orbEntity != ecs::NullEntity && ecsWorld_->valid(orbEntity)) {
        orbLightEntity_ = ecs::light::createChildTorch(*ecsWorld_, orbEntity, 5.0f);
        ecsWorld_->add<ecs::DebugName>(orbLightEntity_, "Orb Torch");
        SDL_Log("Orb light created as child of emissive orb entity (hierarchy-driven)");
    } else {
        // Fallback: create standalone torch if orb entity doesn't exist yet
        auto orbPos = worldPos(2.0f, 0.0f);
        glm::vec3 orbPosition(orbPos.x, 1.3f, orbPos.y);
        orbLightEntity_ = ecs::light::createTorch(*ecsWorld_, orbPosition, 5.0f);
        ecsWorld_->add<ecs::DebugName>(orbLightEntity_, "Orb Torch");
        SDL_Log("Orb light created as standalone (orb entity not available)");
    }

    // Blue point light
    auto bluePos = worldPos(-3.0f, 2.0f);
    glm::vec3 bluePosition(bluePos.x, 2.0f, bluePos.y);
    blueLightEntity_ = ecs::light::createPointLight(
        *ecsWorld_, bluePosition,
        glm::vec3(0.3f, 0.5f, 1.0f),  // Blue color
        3.0f,                          // Intensity
        6.0f);                         // Radius
    ecsWorld_->add<ecs::DebugName>(blueLightEntity_, "Blue Light");

    // Green point light
    auto greenPos = worldPos(4.0f, -2.0f);
    glm::vec3 greenPosition(greenPos.x, 1.5f, greenPos.y);
    greenLightEntity_ = ecs::light::createPointLight(
        *ecsWorld_, greenPosition,
        glm::vec3(0.4f, 1.0f, 0.4f),  // Green color
        2.5f,                          // Intensity
        5.0f);                         // Radius
    ecsWorld_->add<ecs::DebugName>(greenLightEntity_, "Green Light");

    SDL_Log("ECS scene lights initialized (3 light entities)");
}

void SceneManager::updatePhysicsToScene(PhysicsWorld& physics) {
    if (!ecsWorld_) return;

    for (auto [entity, physBody] : ecsWorld_->view<ecs::PhysicsBody>().each()) {
        if (!physBody.valid()) continue;

        // Skip player (handled separately by physics character controller)
        if (ecsWorld_->has<ecs::PlayerTag>(entity)) continue;

        PhysicsBodyID bodyID = static_cast<PhysicsBodyID>(physBody.bodyId);
        glm::mat4 physicsTransform = physics.getBodyTransform(bodyID);

        // Find and update the renderable
        Renderable* renderable = sceneBuilder->getRenderableForEntity(entity);
        if (renderable) {
            // Extract scale from current transform to preserve it
            glm::vec3 scale;
            scale.x = glm::length(glm::vec3(renderable->transform[0]));
            scale.y = glm::length(glm::vec3(renderable->transform[1]));
            scale.z = glm::length(glm::vec3(renderable->transform[2]));

            physicsTransform = glm::scale(physicsTransform, scale);
            renderable->transform = physicsTransform;
        }

        // Sync ECS Transform from physics - enables hierarchy propagation
        // (e.g., orb light follows orb entity via parent-child relationship)
        if (ecsWorld_->has<ecs::Transform>(entity)) {
            ecsWorld_->get<ecs::Transform>(entity).matrix = physicsTransform;
        }

        // Track orb light position for external queries
        if (ecsWorld_->has<ecs::OrbTag>(entity)) {
            orbLightPosition = glm::vec3(physicsTransform[3]);
        }
    }
}
