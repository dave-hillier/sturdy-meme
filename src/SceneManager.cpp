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
    // Create terrain ground plane (radius 50)
    physics.createTerrainDisc(50.0f, 0.0f);

    // Scene object layout from SceneBuilder:
    // 0: Ground disc (static terrain - already created above)
    // 1: Wooden crate 1 at (2.0, 0.5, 0.0) - unit cube
    // 2: Rotated wooden crate at (-1.5, 0.5, 1.0)
    // 3: Polished metal sphere at (0.0, 0.5, -2.0) - radius 0.5
    // 4: Rough metal sphere at (-3.0, 0.5, -1.0) - radius 0.5
    // 5: Polished metal cube at (3.0, 0.5, -2.0)
    // 6: Brushed metal cube at (-3.0, 0.5, -3.0)
    // 7: Emissive sphere at (2.0, 1.3, 0.0) - scaled 0.3, visual radius 0.15
    // 8: Blue light indicator sphere (index 8) - fixed, no physics
    // 9: Green light indicator sphere (index 9) - fixed, no physics
    // 10: Player capsule (index 10, tracked by playerObjectIndex)

    const size_t numSceneObjects = 11;
    scenePhysicsBodies.resize(numSceneObjects, INVALID_BODY_ID);

    // Box half-extent for unit cube
    glm::vec3 cubeHalfExtents(0.5f, 0.5f, 0.5f);
    float boxMass = 10.0f;
    float sphereMass = 5.0f;

    // Spawn objects slightly above ground to let them settle
    const float spawnOffset = 0.1f;

    // Index 1: Wooden crate 1
    scenePhysicsBodies[1] = physics.createBox(glm::vec3(2.0f, 0.5f + spawnOffset, 0.0f), cubeHalfExtents, boxMass);

    // Index 2: Rotated wooden crate
    scenePhysicsBodies[2] = physics.createBox(glm::vec3(-1.5f, 0.5f + spawnOffset, 1.0f), cubeHalfExtents, boxMass);

    // Index 3: Polished metal sphere (mesh radius 0.5)
    scenePhysicsBodies[3] = physics.createSphere(glm::vec3(0.0f, 0.5f + spawnOffset, -2.0f), 0.5f, sphereMass);

    // Index 4: Rough metal sphere (mesh radius 0.5)
    scenePhysicsBodies[4] = physics.createSphere(glm::vec3(-3.0f, 0.5f + spawnOffset, -1.0f), 0.5f, sphereMass);

    // Index 5: Polished metal cube
    scenePhysicsBodies[5] = physics.createBox(glm::vec3(3.0f, 0.5f + spawnOffset, -2.0f), cubeHalfExtents, boxMass);

    // Index 6: Brushed metal cube
    scenePhysicsBodies[6] = physics.createBox(glm::vec3(-3.0f, 0.5f + spawnOffset, -3.0f), cubeHalfExtents, boxMass);

    // Index 7: Emissive sphere - mesh radius 0.5, scaled 0.3 = visual radius 0.15
    scenePhysicsBodies[7] = physics.createSphere(glm::vec3(2.0f, 1.3f + spawnOffset, 0.0f), 0.5f * 0.3f, 1.0f);

    // Index 8 & 9: Blue and green lights - NO PHYSICS (fixed light indicators)
    // scenePhysicsBodies[8] and [9] remain INVALID_BODY_ID

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

    for (size_t i = 1; i < scenePhysicsBodies.size() && i < sceneObjects.size(); i++) {
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
