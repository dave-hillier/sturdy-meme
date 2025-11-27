#include "SceneManager.h"
#include <SDL3/SDL.h>

bool SceneManager::init(SceneBuilder::InitInfo& builderInfo) {
    // Initialize scene builder (meshes, textures, objects)
    if (!sceneBuilder.init(builderInfo)) {
        SDL_Log("Failed to initialize SceneBuilder");
        return false;
    }

    // Initialize scene lights
    initializeSceneLights();

    SDL_Log("SceneManager initialized successfully");
    return true;
}

void SceneManager::initPhysics(PhysicsWorld& physics) {
    initializeScenePhysics(physics);
}

void SceneManager::initTerrainPhysics(PhysicsWorld& physics, const float* heightSamples,
                                       uint32_t sampleCount, float worldSize, float heightScale) {
    // Create heightfield collision shape from terrain data
    PhysicsBodyID terrainBody = physics.createTerrainHeightfield(
        heightSamples, sampleCount, worldSize, heightScale
    );

    if (terrainBody != INVALID_BODY_ID) {
        SDL_Log("Terrain heightfield physics initialized");
    } else {
        SDL_Log("Failed to create terrain heightfield, falling back to flat ground");
        physics.createTerrainDisc(worldSize * 0.5f, 0.0f);
    }
}

void SceneManager::destroy(VmaAllocator allocator, VkDevice device) {
    sceneBuilder.destroy(allocator, device);
}

void SceneManager::update(PhysicsWorld& physics) {
    updatePhysicsToScene(physics);
}

void SceneManager::updatePlayerTransform(const glm::mat4& transform) {
    sceneBuilder.updatePlayerTransform(transform);
}

void SceneManager::initializeScenePhysics(PhysicsWorld& physics) {
    // NOTE: Terrain physics is now initialized separately via initTerrainPhysics()
    // which creates a heightfield from the TerrainSystem's height data

    // Scene object layout from SceneBuilder:
    // 0: Wooden crate 1 at (2.0, 0.5, 0.0) - unit cube
    // 1: Rotated wooden crate at (-1.5, 0.5, 1.0)
    // 2: Polished metal sphere at (0.0, 0.5, -2.0) - radius 0.5
    // 3: Rough metal sphere at (-3.0, 0.5, -1.0) - radius 0.5
    // 4: Polished metal cube at (3.0, 0.5, -2.0)
    // 5: Brushed metal cube at (-3.0, 0.5, -3.0)
    // 6: Emissive sphere at (2.0, 1.3, 0.0) - scaled 0.3, visual radius 0.15
    // 7: Blue light indicator sphere (index 7) - fixed, no physics
    // 8: Green light indicator sphere (index 8) - fixed, no physics
    // 9: Player capsule (index 9, tracked by playerObjectIndex)

    const size_t numSceneObjects = 10;
    scenePhysicsBodies.resize(numSceneObjects, INVALID_BODY_ID);

    // Box half-extent for unit cube
    glm::vec3 cubeHalfExtents(0.5f, 0.5f, 0.5f);
    float boxMass = 10.0f;
    float sphereMass = 5.0f;

    // Spawn objects slightly above ground to let them settle
    const float spawnOffset = 0.1f;

    // Index 0: Wooden crate 1
    scenePhysicsBodies[0] = physics.createBox(glm::vec3(2.0f, 0.5f + spawnOffset, 0.0f), cubeHalfExtents, boxMass);

    // Index 1: Rotated wooden crate
    scenePhysicsBodies[1] = physics.createBox(glm::vec3(-1.5f, 0.5f + spawnOffset, 1.0f), cubeHalfExtents, boxMass);

    // Index 2: Polished metal sphere (mesh radius 0.5)
    scenePhysicsBodies[2] = physics.createSphere(glm::vec3(0.0f, 0.5f + spawnOffset, -2.0f), 0.5f, sphereMass);

    // Index 3: Rough metal sphere (mesh radius 0.5)
    scenePhysicsBodies[3] = physics.createSphere(glm::vec3(-3.0f, 0.5f + spawnOffset, -1.0f), 0.5f, sphereMass);

    // Index 4: Polished metal cube
    scenePhysicsBodies[4] = physics.createBox(glm::vec3(3.0f, 0.5f + spawnOffset, -2.0f), cubeHalfExtents, boxMass);

    // Index 5: Brushed metal cube
    scenePhysicsBodies[5] = physics.createBox(glm::vec3(-3.0f, 0.5f + spawnOffset, -3.0f), cubeHalfExtents, boxMass);

    // Index 6: Emissive sphere - mesh radius 0.5, scaled 0.3 = visual radius 0.15
    // Sphere center should be at radius height (0.15) to sit on ground
    scenePhysicsBodies[6] = physics.createSphere(glm::vec3(2.0f, 0.15f + spawnOffset, 0.0f), 0.5f * 0.3f, 1.0f);

    // Index 7 & 8: Blue and green lights - NO PHYSICS (fixed light indicators)
    // scenePhysicsBodies[7] and [8] remain INVALID_BODY_ID

    SDL_Log("Scene physics initialized with static terrain and dynamic objects");
}

void SceneManager::initializeSceneLights() {
    // Clear any existing lights
    lightManager.clear();

    // Add the glowing orb point light
    Light orbLight;
    orbLight.type = LightType::Point;
    orbLight.position = glm::vec3(2.0f, 1.3f, 0.0f);
    orbLight.color = glm::vec3(1.0f, 0.9f, 0.7f);  // Warm white
    orbLight.intensity = 5.0f;
    orbLight.radius = 8.0f;
    orbLight.priority = 10.0f;  // High priority - always visible
    lightManager.addLight(orbLight);

    // Add blue light
    Light blueLight;
    blueLight.type = LightType::Point;
    blueLight.position = glm::vec3(-3.0f, 2.0f, 2.0f);
    blueLight.color = glm::vec3(0.3f, 0.5f, 1.0f);  // Blue
    blueLight.intensity = 3.0f;
    blueLight.radius = 6.0f;
    blueLight.priority = 5.0f;
    lightManager.addLight(blueLight);

    // Add green light
    Light greenLight;
    greenLight.type = LightType::Point;
    greenLight.position = glm::vec3(4.0f, 1.5f, -2.0f);
    greenLight.color = glm::vec3(0.4f, 1.0f, 0.4f);  // Green
    greenLight.intensity = 2.5f;
    greenLight.radius = 5.0f;
    greenLight.priority = 5.0f;
    lightManager.addLight(greenLight);

    SDL_Log("Scene lights initialized (%zu lights)", lightManager.getLightCount());
}

void SceneManager::updatePhysicsToScene(PhysicsWorld& physics) {
    // Update scene object transforms from physics simulation
    auto& sceneObjects = sceneBuilder.getSceneObjects();

    for (size_t i = 0; i < scenePhysicsBodies.size() && i < sceneObjects.size(); i++) {
        PhysicsBodyID bodyID = scenePhysicsBodies[i];
        if (bodyID == INVALID_BODY_ID) continue;

        // Skip player object (handled separately)
        if (i == sceneBuilder.getPlayerObjectIndex()) continue;

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

        // Update orb light position to follow the emissive sphere (index 7)
        if (i == ORB_LIGHT_OBJECT_INDEX) {
            glm::vec3 orbPosition = glm::vec3(physicsTransform[3]);
            orbLightPosition = orbPosition;

            // Update light manager's orb light position (light index 0)
            if (lightManager.getLightCount() > 0) {
                lightManager.getLight(0).position = orbPosition;
            }
        }
    }
}
