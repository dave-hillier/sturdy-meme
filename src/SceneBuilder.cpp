#include "SceneBuilder.h"
#include "PhysicsSystem.h"
#include <SDL3/SDL_log.h>

bool SceneBuilder::init(const InitInfo& info) {
    // Store terrain height function for object placement
    terrainHeightFunc = info.getTerrainHeight;

    if (!createMeshes(info)) return false;
    if (!loadTextures(info)) return false;
    registerMaterials();
    createRenderables();
    return true;
}

void SceneBuilder::registerMaterials() {
    // Register crate material
    crateMaterialId = materialRegistry.registerMaterial("crate", crateTexture, crateNormalMap);

    // Register metal material
    metalMaterialId = materialRegistry.registerMaterial("metal", metalTexture, metalNormalMap);

    // Register white material (for vertex-colored objects like animated characters)
    // Uses white texture with a flat normal map
    whiteMaterialId = materialRegistry.registerMaterial("white", whiteTexture, groundNormalMap);

    SDL_Log("SceneBuilder: Registered %zu materials", materialRegistry.getMaterialCount());
}

float SceneBuilder::getTerrainHeight(float x, float z) const {
    if (terrainHeightFunc) {
        return terrainHeightFunc(x, z);
    }
    return 0.0f;  // Default to ground level if no terrain
}

void SceneBuilder::destroy(VmaAllocator allocator, VkDevice device) {
    crateTexture.destroy(allocator, device);
    crateNormalMap.destroy(allocator, device);
    groundTexture.destroy(allocator, device);
    groundNormalMap.destroy(allocator, device);
    metalTexture.destroy(allocator, device);
    metalNormalMap.destroy(allocator, device);
    defaultEmissiveMap.destroy(allocator, device);
    whiteTexture.destroy(allocator, device);

    cubeMesh.destroy(allocator);
    sphereMesh.destroy(allocator);
    capsuleMesh.destroy(allocator);
    groundMesh.destroy(allocator);
    flagPoleMesh.destroy(allocator);
    flagClothMesh.destroy(allocator);
    if (hasAnimatedCharacter) {
        animatedCharacter.destroy(allocator);
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

    // Load animated character from FBX
    std::string characterPath = info.resourcePath + "/assets/characters/fbx/Y Bot.fbx";
    if (animatedCharacter.load(characterPath, info.allocator, info.device, info.commandPool, info.graphicsQueue)) {
        hasAnimatedCharacter = true;
        SDL_Log("SceneBuilder: Loaded FBX animated character");

        // Load additional animations (sword and shield locomotion set)
        std::vector<std::string> additionalAnimations = {
            info.resourcePath + "/assets/characters/fbx/ss_idle.fbx",
            info.resourcePath + "/assets/characters/fbx/ss_walk.fbx",
            info.resourcePath + "/assets/characters/fbx/ss_run.fbx",
            info.resourcePath + "/assets/characters/fbx/ss_jump.fbx"
        };
        animatedCharacter.loadAdditionalAnimations(additionalAnimations);

        // Setup default IK chains for arms, legs, look-at, and foot placement
        animatedCharacter.setupDefaultIKChains();

        // Setup ground query for foot placement IK
        if (terrainHeightFunc) {
            auto& ikSystem = animatedCharacter.getIKSystem();
            ikSystem.setGroundQueryFunc([this](const glm::vec3& position, float maxDistance) -> GroundQueryResult {
                GroundQueryResult result;
                result.hit = true;

                // Get height at position
                float h = getTerrainHeight(position.x, position.z);
                result.position = glm::vec3(position.x, h, position.z);
                result.distance = glm::abs(position.y - h);

                // Compute terrain normal using finite differences
                const float delta = 0.1f;  // 10cm sample distance
                float hPosX = getTerrainHeight(position.x + delta, position.z);
                float hNegX = getTerrainHeight(position.x - delta, position.z);
                float hPosZ = getTerrainHeight(position.x, position.z + delta);
                float hNegZ = getTerrainHeight(position.x, position.z - delta);

                // Tangent vectors
                glm::vec3 tangentX(2.0f * delta, hPosX - hNegX, 0.0f);
                glm::vec3 tangentZ(0.0f, hPosZ - hNegZ, 2.0f * delta);

                // Normal is cross product of tangents
                result.normal = glm::normalize(glm::cross(tangentZ, tangentX));

                return result;
            });
            SDL_Log("SceneBuilder: Setup ground query for foot IK");
        }
    } else {
        hasAnimatedCharacter = false;
        SDL_Log("SceneBuilder: Failed to load FBX character, using capsule fallback");
    }

    return true;
}

bool SceneBuilder::loadTextures(const InitInfo& info) {
    std::string texturePath = info.resourcePath + "/assets/textures/crates/crate1/crate1_diffuse.png";
    if (!crateTexture.load(texturePath, info.allocator, info.device, info.commandPool,
                           info.graphicsQueue, info.physicalDevice)) {
        SDL_Log("Failed to load texture: %s", texturePath.c_str());
        return false;
    }

    std::string crateNormalPath = info.resourcePath + "/assets/textures/crates/crate1/crate1_normal.png";
    if (!crateNormalMap.load(crateNormalPath, info.allocator, info.device, info.commandPool,
                              info.graphicsQueue, info.physicalDevice, false)) {
        SDL_Log("Failed to load crate normal map: %s", crateNormalPath.c_str());
        return false;
    }

    std::string grassTexturePath = info.resourcePath + "/assets/textures/grass/grass/grass01.jpg";
    if (!groundTexture.load(grassTexturePath, info.allocator, info.device, info.commandPool,
                            info.graphicsQueue, info.physicalDevice)) {
        SDL_Log("Failed to load grass texture: %s", grassTexturePath.c_str());
        return false;
    }

    std::string grassNormalPath = info.resourcePath + "/assets/textures/grass/grass/grass01_n.jpg";
    if (!groundNormalMap.load(grassNormalPath, info.allocator, info.device, info.commandPool,
                               info.graphicsQueue, info.physicalDevice, false)) {
        SDL_Log("Failed to load grass normal map: %s", grassNormalPath.c_str());
        return false;
    }

    std::string metalTexturePath = info.resourcePath + "/assets/textures/industrial/metal_1.jpg";
    if (!metalTexture.load(metalTexturePath, info.allocator, info.device, info.commandPool,
                           info.graphicsQueue, info.physicalDevice)) {
        SDL_Log("Failed to load metal texture: %s", metalTexturePath.c_str());
        return false;
    }

    std::string metalNormalPath = info.resourcePath + "/assets/textures/industrial/metal_1_norm.jpg";
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

    // Create white texture for vertex-colored objects (like glTF characters)
    if (!whiteTexture.createSolidColor(255, 255, 255, 255, info.allocator, info.device,
                                        info.commandPool, info.graphicsQueue)) {
        SDL_Log("Failed to create white texture");
        return false;
    }

    return true;
}

void SceneBuilder::createRenderables() {
    sceneObjects.clear();

    // Ground disc removed - terrain system provides the ground now

    // Helper: get Y position for object sitting on terrain
    // objectHeight is the distance from object origin to its bottom
    auto getGroundY = [this](float x, float z, float objectHeight) {
        return getTerrainHeight(x, z) + objectHeight;
    };

    // Wooden crate - slightly shiny, non-metallic (unit cube, half-extent 0.5)
    float crateX = 2.0f, crateZ = 0.0f;
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(crateX, getGroundY(crateX, crateZ, 0.5f), crateZ))
        .withMesh(&cubeMesh)
        .withTexture(&crateTexture)
        .withMaterialId(crateMaterialId)
        .withRoughness(0.4f)
        .withMetallic(0.0f)
        .build());

    // Rotated wooden crate
    float rotatedCrateX = -1.5f, rotatedCrateZ = 1.0f;
    glm::mat4 rotatedCube = glm::translate(glm::mat4(1.0f),
        glm::vec3(rotatedCrateX, getGroundY(rotatedCrateX, rotatedCrateZ, 0.5f), rotatedCrateZ));
    rotatedCube = glm::rotate(rotatedCube, glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(rotatedCube)
        .withMesh(&cubeMesh)
        .withTexture(&crateTexture)
        .withMaterialId(crateMaterialId)
        .withRoughness(0.4f)
        .withMetallic(0.0f)
        .build());

    // Polished metal sphere - smooth, fully metallic (radius 0.5)
    float polishedSphereX = 0.0f, polishedSphereZ = -2.0f;
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(polishedSphereX, getGroundY(polishedSphereX, polishedSphereZ, 0.5f), polishedSphereZ))
        .withMesh(&sphereMesh)
        .withTexture(&metalTexture)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.1f)
        .withMetallic(1.0f)
        .build());

    // Rough/brushed metal sphere - moderately rough, metallic (radius 0.5)
    float roughSphereX = -3.0f, roughSphereZ = -1.0f;
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(roughSphereX, getGroundY(roughSphereX, roughSphereZ, 0.5f), roughSphereZ))
        .withMesh(&sphereMesh)
        .withTexture(&metalTexture)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.5f)
        .withMetallic(1.0f)
        .build());

    // Polished metal cube - smooth, fully metallic (half-extent 0.5)
    float polishedCubeX = 3.0f, polishedCubeZ = -2.0f;
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(polishedCubeX, getGroundY(polishedCubeX, polishedCubeZ, 0.5f), polishedCubeZ))
        .withMesh(&cubeMesh)
        .withTexture(&metalTexture)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.1f)
        .withMetallic(1.0f)
        .build());

    // Brushed metal cube - rough, metallic
    float brushedCubeX = -3.0f, brushedCubeZ = -3.0f;
    glm::mat4 brushedCube = glm::translate(glm::mat4(1.0f),
        glm::vec3(brushedCubeX, getGroundY(brushedCubeX, brushedCubeZ, 0.5f), brushedCubeZ));
    brushedCube = glm::rotate(brushedCube, glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(brushedCube)
        .withMesh(&cubeMesh)
        .withTexture(&metalTexture)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.6f)
        .withMetallic(1.0f)
        .build());

    // Glowing emissive sphere on top of the first crate - demonstrates bloom effect
    // Sits 0.8m above crate (crate top at terrain+1.0, sphere center at terrain+1.0+0.3)
    float glowSphereScale = 0.3f;
    glm::mat4 glowingSphereTransform = glm::translate(glm::mat4(1.0f),
        glm::vec3(crateX, getGroundY(crateX, crateZ, 1.0f + glowSphereScale), crateZ));
    glowingSphereTransform = glm::scale(glowingSphereTransform, glm::vec3(glowSphereScale));
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(glowingSphereTransform)
        .withMesh(&sphereMesh)
        .withTexture(&metalTexture)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.2f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(25.0f)
        .withEmissiveColor(glm::vec3(1.0f, 0.9f, 0.7f))
        .withCastsShadow(false)
        .build());

    // Blue light indicator sphere - saturated blue, floating above terrain
    float blueLightX = -3.0f, blueLightZ = 2.0f;
    glm::mat4 blueLightTransform = glm::translate(glm::mat4(1.0f),
        glm::vec3(blueLightX, getGroundY(blueLightX, blueLightZ, 2.0f), blueLightZ));
    blueLightTransform = glm::scale(blueLightTransform, glm::vec3(0.2f));
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(blueLightTransform)
        .withMesh(&sphereMesh)
        .withTexture(&metalTexture)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.2f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(4.0f)
        .withEmissiveColor(glm::vec3(0.0f, 0.3f, 1.0f))
        .withCastsShadow(false)
        .build());

    // Green light indicator sphere - saturated green, floating above terrain
    float greenLightX = 4.0f, greenLightZ = -2.0f;
    glm::mat4 greenLightTransform = glm::translate(glm::mat4(1.0f),
        glm::vec3(greenLightX, getGroundY(greenLightX, greenLightZ, 1.5f), greenLightZ));
    greenLightTransform = glm::scale(greenLightTransform, glm::vec3(0.2f));
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(greenLightTransform)
        .withMesh(&sphereMesh)
        .withTexture(&metalTexture)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.2f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(3.0f)
        .withEmissiveColor(glm::vec3(0.0f, 1.0f, 0.2f))
        .withCastsShadow(false)
        .build());

    // Debug cube at elevated position
    float debugCubeX = 5.0f, debugCubeZ = -5.0f;
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(debugCubeX, getGroundY(debugCubeX, debugCubeZ, 5.0f), debugCubeZ))
        .withMesh(&cubeMesh)
        .withTexture(&crateTexture)
        .withMaterialId(crateMaterialId)
        .withRoughness(0.3f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(5.0f)
        .withEmissiveColor(glm::vec3(1.0f, 0.0f, 0.0f))
        .build());

    // Player character - uses animated character if loaded, otherwise capsule
    // Player position is controlled by physics, so we place at origin on terrain
    float playerX = 0.0f, playerZ = 0.0f;
    float playerTerrainY = getTerrainHeight(playerX, playerZ);
    playerObjectIndex = sceneObjects.size();
    if (hasAnimatedCharacter) {
        // Use materials from FBX if available, otherwise use defaults
        float charRoughness = 0.5f;  // Default roughness for more specular highlights
        float charMetallic = 0.0f;
        glm::vec3 charEmissiveColor = glm::vec3(0.0f);
        float charEmissiveIntensity = 0.0f;

        const auto& materials = animatedCharacter.getMaterials();
        if (!materials.empty()) {
            // Use first material's properties (most characters have a primary material)
            const auto& mat = materials[0];
            charRoughness = mat.roughness;
            charMetallic = mat.metallic;
            charEmissiveColor = mat.emissiveColor;
            charEmissiveIntensity = mat.emissiveFactor;
            SDL_Log("SceneBuilder: Using FBX material '%s' - roughness=%.2f metallic=%.2f",
                    mat.name.c_str(), charRoughness, charMetallic);
        }

        sceneObjects.push_back(RenderableBuilder()
            .withTransform(buildCharacterTransform(glm::vec3(playerX, playerTerrainY, playerZ), 0.0f))
            .withMesh(&animatedCharacter.getMesh())
            .withTexture(&whiteTexture)  // White texture so vertex colors show through
            .withMaterialId(whiteMaterialId)
            .withRoughness(charRoughness)
            .withMetallic(charMetallic)
            .withEmissiveColor(charEmissiveColor)
            .withEmissiveIntensity(charEmissiveIntensity)
            .withCastsShadow(true)
            .build());
    } else {
        // Capsule fallback - capsule height 1.8m, center at 0.9m above ground
        sceneObjects.push_back(RenderableBuilder()
            .atPosition(glm::vec3(playerX, playerTerrainY + 0.9f, playerZ))
            .withMesh(&capsuleMesh)
            .withTexture(&metalTexture)
            .withMaterialId(metalMaterialId)
            .withRoughness(0.3f)
            .withMetallic(0.8f)
            .withCastsShadow(true)
            .build());
    }

    // Flag pole - 3m pole, center at 1.5m above ground
    float flagPoleX = 5.0f, flagPoleZ = 0.0f;
    flagPoleIndex = sceneObjects.size();
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(flagPoleX, getGroundY(flagPoleX, flagPoleZ, 1.5f), flagPoleZ))
        .withMesh(&flagPoleMesh)
        .withTexture(&metalTexture)
        .withMaterialId(metalMaterialId)
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
        .withMaterialId(crateMaterialId)
        .withRoughness(0.6f)
        .withMetallic(0.0f)
        .withCastsShadow(true)
        .build());

    // Well entrance - demonstrates terrain hole mask system
    // A stone-like frame floating above the terrain hole
    wellEntranceX = 20.0f;
    wellEntranceZ = 20.0f;
    float wellY = getTerrainHeight(wellEntranceX, wellEntranceZ);
    // Frame floats 3m above terrain so hole is visible
    glm::mat4 wellTransform = glm::translate(glm::mat4(1.0f),
        glm::vec3(wellEntranceX, wellY + 3.0f, wellEntranceZ));
    wellTransform = glm::scale(wellTransform, glm::vec3(2.0f, 0.5f, 2.0f));
    wellEntranceIndex = sceneObjects.size();
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(wellTransform)
        .withMesh(&cubeMesh)
        .withTexture(&metalTexture)  // Stone-like appearance
        .withMaterialId(metalMaterialId)
        .withRoughness(0.8f)
        .withMetallic(0.1f)
        .withCastsShadow(true)
        .build());
}

void SceneBuilder::uploadFlagClothMesh(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue) {
    flagClothMesh.destroy(allocator);
    flagClothMesh.upload(allocator, device, commandPool, queue);
}

glm::mat4 SceneBuilder::buildCharacterTransform(const glm::vec3& position, float yRotation) const {
    // Character model transform:
    // 1. Translate to world position
    // 2. Apply Y rotation (facing direction)
    // Note: Scale is now handled by FBX post-import processing
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
    transform = glm::rotate(transform, yRotation, glm::vec3(0.0f, 1.0f, 0.0f));
    return transform;
}

void SceneBuilder::updatePlayerTransform(const glm::mat4& transform) {
    if (playerObjectIndex < sceneObjects.size()) {
        if (hasAnimatedCharacter) {
            // Extract position and adjust Y (remove capsule center offset)
            glm::vec3 pos = glm::vec3(transform[3]);
            pos.y -= 0.9f;  // CAPSULE_HEIGHT * 0.5 = 1.8 * 0.5

            // Use the player transform's rotation directly, just adjust position
            // Note: Scale is now handled by FBX post-import processing
            glm::mat4 result = transform;
            result[3] = glm::vec4(pos, 1.0f);

            sceneObjects[playerObjectIndex].transform = result;
        } else {
            sceneObjects[playerObjectIndex].transform = transform;
        }
    }
}

void SceneBuilder::updateAnimatedCharacter(float deltaTime, VmaAllocator allocator, VkDevice device,
                                            VkCommandPool commandPool, VkQueue queue,
                                            float movementSpeed, bool isGrounded, bool isJumping) {
    if (!hasAnimatedCharacter) return;

    // Get the character's current world transform for IK ground queries
    glm::mat4 worldTransform = glm::mat4(1.0f);
    if (playerObjectIndex < sceneObjects.size()) {
        worldTransform = sceneObjects[playerObjectIndex].transform;
    }

    animatedCharacter.update(deltaTime, allocator, device, commandPool, queue,
                             movementSpeed, isGrounded, isJumping, worldTransform);

    // Update the mesh pointer in the renderable (in case it was re-created)
    if (playerObjectIndex < sceneObjects.size()) {
        sceneObjects[playerObjectIndex].mesh = &animatedCharacter.getMesh();
    }
}

void SceneBuilder::startCharacterJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics) {
    if (!hasAnimatedCharacter) return;
    animatedCharacter.startJump(startPos, velocity, gravity, physics);
}
