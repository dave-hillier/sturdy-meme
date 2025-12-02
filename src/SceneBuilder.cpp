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
    if (hasCharacterMesh) {
        characterMesh.destroy(allocator);
    }

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

    // Try to load character model from glTF
    std::string characterPath = info.resourcePath + "/assets/characters/player.glb";
    auto gltfResult = GLTFLoader::load(characterPath);
    if (gltfResult) {
        characterMesh.setCustomGeometry(gltfResult->vertices, gltfResult->indices);
        characterMesh.upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);
        hasCharacterMesh = true;
        SDL_Log("SceneBuilder: Loaded character mesh with %zu vertices", gltfResult->vertices.size());
    } else {
        SDL_Log("SceneBuilder: Character model not found at %s, using capsule fallback", characterPath.c_str());
        hasCharacterMesh = false;
    }

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
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(2.0f, 0.5f, 0.0f))
        .withMesh(&cubeMesh)
        .withTexture(&crateTexture)
        .withRoughness(0.4f)
        .withMetallic(0.0f)
        .build());

    // Rotated wooden crate
    glm::mat4 rotatedCube = glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.5f, 1.0f));
    rotatedCube = glm::rotate(rotatedCube, glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(rotatedCube)
        .withMesh(&cubeMesh)
        .withTexture(&crateTexture)
        .withRoughness(0.4f)
        .withMetallic(0.0f)
        .build());

    // Polished metal sphere - smooth, fully metallic
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(0.0f, 0.5f, -2.0f))
        .withMesh(&sphereMesh)
        .withTexture(&metalTexture)
        .withRoughness(0.1f)
        .withMetallic(1.0f)
        .build());

    // Rough/brushed metal sphere - moderately rough, metallic
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(-3.0f, 0.5f, -1.0f))
        .withMesh(&sphereMesh)
        .withTexture(&metalTexture)
        .withRoughness(0.5f)
        .withMetallic(1.0f)
        .build());

    // Polished metal cube - smooth, fully metallic
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(3.0f, 0.5f, -2.0f))
        .withMesh(&cubeMesh)
        .withTexture(&metalTexture)
        .withRoughness(0.1f)
        .withMetallic(1.0f)
        .build());

    // Brushed metal cube - rough, metallic
    glm::mat4 brushedCube = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.5f, -3.0f));
    brushedCube = glm::rotate(brushedCube, glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(brushedCube)
        .withMesh(&cubeMesh)
        .withTexture(&metalTexture)
        .withRoughness(0.6f)
        .withMetallic(1.0f)
        .build());

    // Glowing emissive sphere on top of the first crate - demonstrates bloom effect
    glm::mat4 glowingSphereTransform = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 1.3f, 0.0f));
    glowingSphereTransform = glm::scale(glowingSphereTransform, glm::vec3(0.3f));
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(glowingSphereTransform)
        .withMesh(&sphereMesh)
        .withTexture(&metalTexture)
        .withRoughness(0.2f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(25.0f)
        .withEmissiveColor(glm::vec3(1.0f, 0.9f, 0.7f))
        .withCastsShadow(false)
        .build());

    // Blue light indicator sphere - saturated blue, lower intensity to preserve color
    glm::mat4 blueLightTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 2.0f, 2.0f));
    blueLightTransform = glm::scale(blueLightTransform, glm::vec3(0.2f));
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(blueLightTransform)
        .withMesh(&sphereMesh)
        .withTexture(&metalTexture)
        .withRoughness(0.2f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(4.0f)
        .withEmissiveColor(glm::vec3(0.0f, 0.3f, 1.0f))
        .withCastsShadow(false)
        .build());

    // Green light indicator sphere - saturated green, lower intensity to preserve color
    glm::mat4 greenLightTransform = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 1.5f, -2.0f));
    greenLightTransform = glm::scale(greenLightTransform, glm::vec3(0.2f));
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(greenLightTransform)
        .withMesh(&sphereMesh)
        .withTexture(&metalTexture)
        .withRoughness(0.2f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(3.0f)
        .withEmissiveColor(glm::vec3(0.0f, 1.0f, 0.2f))
        .withCastsShadow(false)
        .build());

    // Debug cube at Suzanne position for visibility test
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(5.0f, 5.0f, -5.0f))
        .withMesh(&cubeMesh)
        .withTexture(&crateTexture)
        .withRoughness(0.3f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(5.0f)
        .withEmissiveColor(glm::vec3(1.0f, 0.0f, 0.0f))
        .build());

    // Player character - uses character mesh if loaded, otherwise capsule
    playerObjectIndex = sceneObjects.size();
    if (hasCharacterMesh) {
        // Character mesh loaded from glTF - position at origin
        // Note: Character models often have their pivot at the feet, so no Y offset needed
        sceneObjects.push_back(RenderableBuilder()
            .atPosition(glm::vec3(0.0f, 0.0f, 0.0f))
            .withMesh(&characterMesh)
            .withTexture(&metalTexture)
            .withRoughness(0.5f)
            .withMetallic(0.0f)
            .withCastsShadow(true)
            .build());
    } else {
        // Capsule fallback - centered at origin, uses metal texture for visibility
        sceneObjects.push_back(RenderableBuilder()
            .atPosition(glm::vec3(0.0f, 0.9f, 0.0f))
            .withMesh(&capsuleMesh)
            .withTexture(&metalTexture)
            .withRoughness(0.3f)
            .withMetallic(0.8f)
            .withCastsShadow(true)
            .build());
    }

    // Flag pole - positioned at (5, 1.5, 0) so the 3m pole sits on the ground
    flagPoleIndex = sceneObjects.size();
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(5.0f, 1.5f, 0.0f))
        .withMesh(&flagPoleMesh)
        .withTexture(&metalTexture)
        .withRoughness(0.4f)
        .withMetallic(0.9f)
        .withCastsShadow(true)
        .build());

    // Flag cloth - will be positioned and updated by ClothSimulation
    flagClothIndex = sceneObjects.size();
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(glm::mat4(1.0f))  // Identity, will be handled differently
        .withMesh(&flagClothMesh)
        .withTexture(&crateTexture)  // Using crate texture for now
        .withRoughness(0.6f)
        .withMetallic(0.0f)
        .withCastsShadow(true)
        .build());
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
