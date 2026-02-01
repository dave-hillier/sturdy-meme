#include "SceneManager.h"
#include "lighting/LightSystem.h"
#include "ecs/Components.h"
#include <SDL3/SDL.h>

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

    // Get scene objects from builder
    const auto& sceneObjects = sceneBuilder->getRenderables();
    const auto& physicsIndices = sceneBuilder->getPhysicsEnabledIndices();

    // Resize physics bodies array to match scene objects
    scenePhysicsBodies.resize(sceneObjects.size(), INVALID_BODY_ID);

    // Physics parameters
    glm::vec3 cubeHalfExtents(0.5f, 0.5f, 0.5f);
    float boxMass = 10.0f;
    float sphereMass = 5.0f;
    const float spawnOffset = 0.1f;

    // Helper to get spawn Y position on terrain
    auto getSpawnY = [this, spawnOffset](float x, float z, float objectHeight) {
        return getTerrainHeight(x, z) + objectHeight + spawnOffset;
    };

    // Get the emissive orb index from SceneBuilder
    size_t emissiveOrbIndex = sceneBuilder->getEmissiveOrbIndex();

    // Create physics bodies for each physics-enabled object
    // The physicsIndices tells us which scene objects need physics
    for (size_t i = 0; i < physicsIndices.size(); ++i) {
        size_t objIndex = physicsIndices[i];
        if (objIndex >= sceneObjects.size()) continue;

        const auto& obj = sceneObjects[objIndex];
        glm::vec3 pos = glm::vec3(obj.transform[3]);

        // Determine object type from mesh - cubes vs spheres
        // For the emissive orb (small sphere on crate), use smaller radius
        if (objIndex == emissiveOrbIndex) {
            // Small emissive sphere (scaled 0.3)
            scenePhysicsBodies[objIndex] = physics.createSphere(
                glm::vec3(pos.x, pos.y + spawnOffset, pos.z),
                0.5f * 0.3f, 1.0f);
        } else if (i < 2 || i == 4 || i == 5) {
            // Boxes: crates (0,1) and metal cubes (4,5)
            scenePhysicsBodies[objIndex] = physics.createBox(
                glm::vec3(pos.x, pos.y + spawnOffset, pos.z),
                cubeHalfExtents, boxMass);
        } else {
            // Spheres: indices 2,3
            scenePhysicsBodies[objIndex] = physics.createSphere(
                glm::vec3(pos.x, pos.y + spawnOffset, pos.z),
                0.5f, sphereMass);
        }
    }

    SDL_Log("Scene physics initialized with %zu physics-enabled objects", physicsIndices.size());
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
    // Clear any existing lights from legacy system
    lightManager.clear();

    // Helper to apply scene origin offset
    auto worldPos = [this](float localX, float localZ) -> glm::vec2 {
        return glm::vec2(localX + sceneOrigin.x, localZ + sceneOrigin.y);
    };

    // Create ECS light entities if world is available
    if (ecsWorld_) {
        // Orb light - flickering torch that follows the emissive sphere
        auto orbPos = worldPos(2.0f, 0.0f);
        glm::vec3 orbPosition(orbPos.x, 1.3f, orbPos.y);
        orbLightEntity_ = ecs::light::createTorch(*ecsWorld_, orbPosition, 5.0f);

        // Blue point light
        auto bluePos = worldPos(-3.0f, 2.0f);
        glm::vec3 bluePosition(bluePos.x, 2.0f, bluePos.y);
        blueLightEntity_ = ecs::light::createPointLight(
            *ecsWorld_, bluePosition,
            glm::vec3(0.3f, 0.5f, 1.0f),  // Blue color
            3.0f,                          // Intensity
            6.0f);                         // Radius

        // Green point light
        auto greenPos = worldPos(4.0f, -2.0f);
        glm::vec3 greenPosition(greenPos.x, 1.5f, greenPos.y);
        greenLightEntity_ = ecs::light::createPointLight(
            *ecsWorld_, greenPosition,
            glm::vec3(0.4f, 1.0f, 0.4f),  // Green color
            2.5f,                          // Intensity
            5.0f);                         // Radius

        SDL_Log("ECS scene lights initialized (3 light entities)");
    } else {
        // Fallback to legacy LightManager if ECS world not available
        auto orbPos = worldPos(2.0f, 0.0f);
        Light orbLight;
        orbLight.type = LightType::Point;
        orbLight.position = glm::vec3(orbPos.x, 1.3f, orbPos.y);
        orbLight.color = glm::vec3(1.0f, 0.9f, 0.7f);  // Warm white
        orbLight.intensity = 5.0f;
        orbLight.radius = 8.0f;
        orbLight.priority = 10.0f;
        lightManager.addLight(orbLight);

        auto bluePos = worldPos(-3.0f, 2.0f);
        Light blueLight;
        blueLight.type = LightType::Point;
        blueLight.position = glm::vec3(bluePos.x, 2.0f, bluePos.y);
        blueLight.color = glm::vec3(0.3f, 0.5f, 1.0f);
        blueLight.intensity = 3.0f;
        blueLight.radius = 6.0f;
        blueLight.priority = 5.0f;
        lightManager.addLight(blueLight);

        auto greenPos = worldPos(4.0f, -2.0f);
        Light greenLight;
        greenLight.type = LightType::Point;
        greenLight.position = glm::vec3(greenPos.x, 1.5f, greenPos.y);
        greenLight.color = glm::vec3(0.4f, 1.0f, 0.4f);
        greenLight.intensity = 2.5f;
        greenLight.radius = 5.0f;
        greenLight.priority = 5.0f;
        lightManager.addLight(greenLight);

        SDL_Log("Scene lights initialized (legacy LightManager: %zu lights)", lightManager.getLightCount());
    }
}

void SceneManager::updatePhysicsToScene(PhysicsWorld& physics) {
    // Update scene object transforms from physics simulation
    auto& sceneObjects = sceneBuilder->getRenderables();
    size_t emissiveOrbIndex = sceneBuilder->getEmissiveOrbIndex();

    for (size_t i = 0; i < scenePhysicsBodies.size() && i < sceneObjects.size(); i++) {
        PhysicsBodyID bodyID = scenePhysicsBodies[i];
        if (bodyID == INVALID_BODY_ID) continue;

        // Skip player object (handled separately)
        if (i == sceneBuilder->getPlayerObjectIndex()) continue;

        // Get transform from physics (position and rotation only)
        glm::mat4 physicsTransform = physics.getBodyTransform(bodyID);

        // Extract scale from current transform to preserve it
        glm::vec3 scale;
        scale.x = glm::length(glm::vec3(sceneObjects[i].transform[0]));
        scale.y = glm::length(glm::vec3(sceneObjects[i].transform[1]));
        scale.z = glm::length(glm::vec3(sceneObjects[i].transform[2]));

        // Apply scale to physics transform
        physicsTransform = glm::scale(physicsTransform, scale);

        // Update scene object transform
        sceneObjects[i].transform = physicsTransform;

        // Update orb light position to follow the emissive sphere
        if (i == emissiveOrbIndex) {
            glm::vec3 orbPosition = glm::vec3(physicsTransform[3]);
            orbLightPosition = orbPosition;

            // Update ECS light entity position
            if (ecsWorld_ && orbLightEntity_ != ecs::NullEntity && ecsWorld_->valid(orbLightEntity_)) {
                if (ecsWorld_->has<ecs::Transform>(orbLightEntity_)) {
                    ecsWorld_->get<ecs::Transform>(orbLightEntity_).matrix =
                        ecs::Transform::fromPosition(orbPosition).matrix;
                }
            } else if (lightManager.getLightCount() > 0) {
                // Fallback to legacy light manager
                lightManager.getLight(0).position = orbPosition;
            }
        }
    }
}
