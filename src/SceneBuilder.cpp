#include "SceneBuilder.h"
#include <SDL3/SDL_log.h>

bool SceneBuilder::init(const InitInfo& info) {
    if (!createMeshes(info)) return false;
    if (!loadTextures(info)) return false;
    createSceneObjects();
    return true;
}

void SceneBuilder::destroy(VmaAllocator allocator, VkDevice device) {
    crateTexture.destroy(allocator, device);
    crateNormalMap.destroy(allocator, device);
    groundTexture.destroy(allocator, device);
    groundNormalMap.destroy(allocator, device);
    metalTexture.destroy(allocator, device);
    metalNormalMap.destroy(allocator, device);
    defaultEmissiveMap.destroy(allocator, device);

    cubeMesh.destroy(allocator);
    sphereMesh.destroy(allocator);
    capsuleMesh.destroy(allocator);
    groundMesh.destroy(allocator);
    flagPoleMesh.destroy(allocator);
    flagClothMesh.destroy(allocator);

    sceneObjects.clear();
}

bool SceneBuilder::createMeshes(const InitInfo& info) {
    // Create disc ground mesh with radius 50, 64 segments, UV tiling of 10x
    groundMesh.createDisc(50.0f, 64, 10.0f);
    groundMesh.upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);

    cubeMesh.createCube();
    cubeMesh.upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);

    sphereMesh.createSphere(0.5f, 32, 32);
    sphereMesh.upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);

    // Player capsule mesh (1.8m tall, 0.3m radius)
    capsuleMesh.createCapsule(0.3f, 1.8f, 16, 16);
    capsuleMesh.upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);

    // Flag pole mesh (cylinder: 0.05m radius, 3m height)
    flagPoleMesh.createCylinder(0.05f, 3.0f, 16);
    flagPoleMesh.upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);

    // Flag cloth mesh will be initialized later by ClothSimulation
    // (it's dynamic and will be updated each frame)

    return true;
}

bool SceneBuilder::loadTextures(const InitInfo& info) {
    std::string texturePath = info.resourcePath + "/textures/crates/crate1/crate1_diffuse.png";
    if (!crateTexture.load(texturePath, info.allocator, info.device, info.commandPool,
                           info.graphicsQueue, info.physicalDevice)) {
        SDL_Log("Failed to load texture: %s", texturePath.c_str());
        return false;
    }

    std::string crateNormalPath = info.resourcePath + "/textures/crates/crate1/crate1_normal.png";
    if (!crateNormalMap.load(crateNormalPath, info.allocator, info.device, info.commandPool,
                              info.graphicsQueue, info.physicalDevice, false)) {
        SDL_Log("Failed to load crate normal map: %s", crateNormalPath.c_str());
        return false;
    }

    std::string grassTexturePath = info.resourcePath + "/textures/grass/grass/grass01.jpg";
    if (!groundTexture.load(grassTexturePath, info.allocator, info.device, info.commandPool,
                            info.graphicsQueue, info.physicalDevice)) {
        SDL_Log("Failed to load grass texture: %s", grassTexturePath.c_str());
        return false;
    }

    std::string grassNormalPath = info.resourcePath + "/textures/grass/grass/grass01_n.jpg";
    if (!groundNormalMap.load(grassNormalPath, info.allocator, info.device, info.commandPool,
                               info.graphicsQueue, info.physicalDevice, false)) {
        SDL_Log("Failed to load grass normal map: %s", grassNormalPath.c_str());
        return false;
    }

    std::string metalTexturePath = info.resourcePath + "/textures/industrial/metal_1.jpg";
    if (!metalTexture.load(metalTexturePath, info.allocator, info.device, info.commandPool,
                           info.graphicsQueue, info.physicalDevice)) {
        SDL_Log("Failed to load metal texture: %s", metalTexturePath.c_str());
        return false;
    }

    std::string metalNormalPath = info.resourcePath + "/textures/industrial/metal_1_norm.jpg";
    if (!metalNormalMap.load(metalNormalPath, info.allocator, info.device, info.commandPool,
                              info.graphicsQueue, info.physicalDevice, false)) {
        SDL_Log("Failed to load metal normal map: %s", metalNormalPath.c_str());
        return false;
    }

    // Create default black emissive map for objects without emissive textures
    if (!defaultEmissiveMap.createSolidColor(0, 0, 0, 255, info.allocator, info.device,
                                              info.commandPool, info.graphicsQueue)) {
        SDL_Log("Failed to create default emissive map");
        return false;
    }

    return true;
}

void SceneBuilder::createSceneObjects() {
    sceneObjects.clear();

    // Ground disc removed - terrain system provides the ground now

    // Wooden crate - slightly shiny, non-metallic
    sceneObjects.push_back({
        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.5f, 0.0f)),
        &cubeMesh, &crateTexture, 0.4f, 0.0f
    });

    // Rotated wooden crate
    glm::mat4 rotatedCube = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.5f, 1.0f));
    rotatedCube = glm::rotate(rotatedCube, glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    sceneObjects.push_back({rotatedCube, &cubeMesh, &crateTexture, 0.4f, 0.0f});

    // Polished metal sphere - smooth, fully metallic
    sceneObjects.push_back({
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, -2.0f)),
        &sphereMesh, &metalTexture, 0.1f, 1.0f
    });

    // Rough/brushed metal sphere - moderately rough, metallic
    sceneObjects.push_back({
        glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.5f, -1.0f)),
        &sphereMesh, &metalTexture, 0.5f, 1.0f
    });

    // Polished metal cube - smooth, fully metallic
    sceneObjects.push_back({
        glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.5f, -2.0f)),
        &cubeMesh, &metalTexture, 0.1f, 1.0f
    });

    // Brushed metal cube - rough, metallic
    glm::mat4 brushedCube = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.5f, -3.0f));
    brushedCube = glm::rotate(brushedCube, glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    sceneObjects.push_back({brushedCube, &cubeMesh, &metalTexture, 0.6f, 1.0f});

    // Glowing emissive sphere on top of the first crate - demonstrates bloom effect
    glm::mat4 glowingSphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 1.3f, 0.0f));
    glowingSphereTransform = glm::scale(glowingSphereTransform, glm::vec3(0.3f));
    sceneObjects.push_back({glowingSphereTransform, &sphereMesh, &metalTexture, 0.2f, 0.0f, 25.0f, glm::vec3(1.0f, 0.9f, 0.7f), false});

    // Blue light indicator sphere - saturated blue, lower intensity to preserve color
    glm::mat4 blueLightTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 2.0f, 2.0f));
    blueLightTransform = glm::scale(blueLightTransform, glm::vec3(0.2f));
    sceneObjects.push_back({blueLightTransform, &sphereMesh, &metalTexture, 0.2f, 0.0f, 4.0f, glm::vec3(0.0f, 0.3f, 1.0f), false});

    // Green light indicator sphere - saturated green, lower intensity to preserve color
    glm::mat4 greenLightTransform = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 1.5f, -2.0f));
    greenLightTransform = glm::scale(greenLightTransform, glm::vec3(0.2f));
    sceneObjects.push_back({greenLightTransform, &sphereMesh, &metalTexture, 0.2f, 0.0f, 3.0f, glm::vec3(0.0f, 1.0f, 0.2f), false});

    // Player capsule - centered at origin, uses metal texture for visibility
    playerObjectIndex = sceneObjects.size();
    glm::mat4 playerTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.9f, 0.0f));
    sceneObjects.push_back({playerTransform, &capsuleMesh, &metalTexture, 0.3f, 0.8f, 0.0f, glm::vec3(1.0f), true});

    // Flag pole - positioned at (5, 1.5, 0) so the 3m pole sits on the ground
    flagPoleIndex = sceneObjects.size();
    glm::mat4 flagPoleTransform = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 1.5f, 0.0f));
    sceneObjects.push_back({flagPoleTransform, &flagPoleMesh, &metalTexture, 0.4f, 0.9f, 0.0f, glm::vec3(1.0f), true});

    // Flag cloth - will be positioned and updated by ClothSimulation
    flagClothIndex = sceneObjects.size();
    glm::mat4 flagClothTransform = glm::mat4(1.0f);  // Identity, will be handled differently
    sceneObjects.push_back({flagClothTransform, &flagClothMesh, &crateTexture, 0.6f, 0.0f, 0.0f, glm::vec3(1.0f), true});
}

void SceneBuilder::uploadFlagClothMesh(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue) {
    flagClothMesh.destroy(allocator);
    flagClothMesh.upload(allocator, device, commandPool, queue);
}

void SceneBuilder::updatePlayerTransform(const glm::mat4& transform) {
    if (playerObjectIndex < sceneObjects.size()) {
        sceneObjects[playerObjectIndex].transform = transform;
    }
}
