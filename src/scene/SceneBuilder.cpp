#include "SceneBuilder.h"
#include "Transform.h"
#include "PhysicsSystem.h"
#include "asset/AssetRegistry.h"
#include <SDL3/SDL_log.h>

std::unique_ptr<SceneBuilder> SceneBuilder::create(const InitInfo& info) {
    auto instance = std::make_unique<SceneBuilder>(ConstructToken{});
    if (!instance->initInternal(info)) {
        return nullptr;
    }
    return instance;
}

SceneBuilder::~SceneBuilder() {
    cleanup();
}

bool SceneBuilder::initInternal(const InitInfo& info) {
    // Store terrain height function for object placement
    terrainHeightFunc = info.getTerrainHeight;
    storedAllocator = info.allocator;
    storedDevice = info.device;
    sceneOrigin = info.sceneOrigin;

    if (!info.assetRegistry) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SceneBuilder: AssetRegistry is required");
        return false;
    }
    assetRegistry_.emplace(*info.assetRegistry);

    if (!createMeshes(info)) return false;
    if (!loadTextures(info)) return false;
    registerMaterials();
    createRenderables();
    return true;
}

void SceneBuilder::registerMaterials() {
    // Get textures from registry
    const Texture* crateTex = getCrateTexture();
    const Texture* crateNorm = getCrateNormalMap();
    const Texture* groundTex = getGroundTexture();
    const Texture* groundNorm = getGroundNormalMap();
    const Texture* metalTex = getMetalTexture();
    const Texture* metalNorm = getMetalNormalMap();
    const Texture* whiteTex = getWhiteTexture();

    if (!crateTex || !crateNorm || !groundTex || !groundNorm ||
        !metalTex || !metalNorm || !whiteTex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SceneBuilder: Missing textures for material registration");
        return;
    }

    // Register crate material
    crateMaterialId = materialRegistry.registerMaterial("crate", *crateTex, *crateNorm);

    // Register ground material (for any ground-related objects)
    groundMaterialId = materialRegistry.registerMaterial("ground", *groundTex, *groundNorm);

    // Register metal material
    metalMaterialId = materialRegistry.registerMaterial("metal", *metalTex, *metalNorm);

    // Register white material (for vertex-colored objects like animated characters)
    // Uses white texture with a flat normal map
    whiteMaterialId = materialRegistry.registerMaterial("white", *whiteTex, *groundNorm);

    // Register cape material (using metal texture)
    capeMaterialId = materialRegistry.registerMaterial("cape", *metalTex, *metalNorm);

    SDL_Log("SceneBuilder: Registered %zu materials", materialRegistry.getMaterialCount());
}

float SceneBuilder::getTerrainHeight(float x, float z) const {
    if (terrainHeightFunc) {
        return terrainHeightFunc(x, z);
    }
    return 0.0f;  // Default to ground level if no terrain
}

void SceneBuilder::cleanup() {
    // Release texture shared_ptrs (textures freed automatically when last reference is released)
    crateTexture_.reset();
    crateNormal_.reset();
    groundTexture_.reset();
    groundNormal_.reset();
    metalTexture_.reset();
    metalNormal_.reset();
    defaultEmissive_.reset();
    whiteTexture_.reset();

    // RAII-managed meshes (static)
    cubeMesh.reset();
    sphereMesh.reset();
    capsuleMesh.reset();
    flagPoleMesh.reset();
    swordMesh.reset();
    shieldMesh.reset();

    // Manually managed meshes (dynamic - re-uploaded during runtime)
    flagClothMesh.releaseGPUResources();
    capeMesh.releaseGPUResources();

    // RAII-managed animated character
    animatedCharacter.reset();

    sceneObjects.clear();
}

bool SceneBuilder::createMeshes(const InitInfo& info) {

    cubeMesh = std::make_unique<Mesh>();
    cubeMesh->createCube();
    if (!cubeMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    sphereMesh = std::make_unique<Mesh>();
    sphereMesh->createSphere(0.5f, 32, 32);
    if (!sphereMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Player capsule mesh (1.8m tall, 0.3m radius)
    capsuleMesh = std::make_unique<Mesh>();
    capsuleMesh->createCapsule(0.3f, 1.8f, 16, 16);
    if (!capsuleMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Flag pole mesh (cylinder: 0.05m radius, 3m height)
    flagPoleMesh = std::make_unique<Mesh>();
    flagPoleMesh->createCylinder(0.05f, 3.0f, 16);
    if (!flagPoleMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Sword mesh (long thin cylinder: 0.02m radius, 0.8m length)
    swordMesh = std::make_unique<Mesh>();
    swordMesh->createCylinder(0.02f, 0.8f, 12);
    if (!swordMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Shield mesh (flat wide cylinder: 0.2m radius, 0.03m thickness)
    shieldMesh = std::make_unique<Mesh>();
    shieldMesh->createCylinder(0.2f, 0.03f, 16);
    if (!shieldMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Flag cloth mesh will be initialized later by ClothSimulation
    // (it's dynamic and will be updated each frame)

    // Load animated character from FBX
    std::string characterPath = info.resourcePath + "/assets/characters/fbx/Y Bot.fbx";
    std::vector<std::string> additionalAnimations = {
        info.resourcePath + "/assets/characters/fbx/ss_idle.fbx",
        info.resourcePath + "/assets/characters/fbx/ss_walk.fbx",
        info.resourcePath + "/assets/characters/fbx/ss_run.fbx",
        info.resourcePath + "/assets/characters/fbx/ss_jump.fbx"
    };

    AnimatedCharacter::InitInfo charInfo{};
    charInfo.path = characterPath;
    charInfo.allocator = info.allocator;
    charInfo.device = info.device;
    charInfo.commandPool = info.commandPool;
    charInfo.queue = info.graphicsQueue;

    animatedCharacter = AnimatedCharacter::create(charInfo);

    if (animatedCharacter) {
        // Load additional animations (sword and shield locomotion set)
        animatedCharacter->loadAdditionalAnimations(additionalAnimations);

        // Setup default IK chains for arms, legs, look-at, and foot placement
        animatedCharacter->setupDefaultIKChains();

        hasAnimatedCharacter = true;
        SDL_Log("SceneBuilder: Loaded FBX animated character");

        // Setup ground query for foot placement IK
        if (terrainHeightFunc) {
            auto& ikSystem = animatedCharacter->getIKSystem();
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

        // Initialize player cape attached to the character
        playerCape.create(8, 12, 0.08f);  // 8x12 grid, 8cm spacing
        playerCape.setupDefaultColliders();
        playerCape.setupDefaultAttachments();
        playerCape.createMesh(capeMesh);
        capeMesh.upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);
        hasCapeEnabled = true;
        SDL_Log("SceneBuilder: Initialized player cape");

        // Find hand bone indices for weapon attachment
        const auto& skeleton = animatedCharacter->getSkeleton();
        // Try common bone name patterns (Mixamo uses "mixamorig:" prefix)
        const std::vector<std::string> rightHandNames = {"RightHand", "mixamorig:RightHand", "R_Hand", "hand.R"};
        const std::vector<std::string> leftHandNames = {"LeftHand", "mixamorig:LeftHand", "L_Hand", "hand.L"};

        for (const auto& name : rightHandNames) {
            rightHandBoneIndex = skeleton.findJointIndex(name);
            if (rightHandBoneIndex >= 0) {
                SDL_Log("SceneBuilder: Found right hand bone '%s' at index %d", name.c_str(), rightHandBoneIndex);
                break;
            }
        }
        for (const auto& name : leftHandNames) {
            leftHandBoneIndex = skeleton.findJointIndex(name);
            if (leftHandBoneIndex >= 0) {
                SDL_Log("SceneBuilder: Found left hand bone '%s' at index %d", name.c_str(), leftHandBoneIndex);
                break;
            }
        }
        if (rightHandBoneIndex < 0 || leftHandBoneIndex < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SceneBuilder: Could not find hand bones for weapon attachment");
        }
    } else {
        hasAnimatedCharacter = false;
        SDL_Log("SceneBuilder: Failed to load FBX character, using capsule fallback");
    }

    return true;
}

bool SceneBuilder::loadTextures(const InitInfo& info) {
    // Load textures via AssetRegistry (with caching and deduplication)
    TextureLoadConfig srgbConfig{};
    srgbConfig.useSRGB = true;
    srgbConfig.generateMipmaps = true;

    TextureLoadConfig linearConfig{};
    linearConfig.useSRGB = false;  // Normal maps are linear
    linearConfig.generateMipmaps = true;

    // Crate textures
    std::string texturePath = info.resourcePath + "/assets/textures/crates/crate1/crate1_diffuse.png";
    crateTexture_ = assetRegistry_->get().loadTexture(texturePath, srgbConfig);
    if (!crateTexture_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load texture: %s", texturePath.c_str());
        return false;
    }

    std::string crateNormalPath = info.resourcePath + "/assets/textures/crates/crate1/crate1_normal.png";
    crateNormal_ = assetRegistry_->get().loadTexture(crateNormalPath, linearConfig);
    if (!crateNormal_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load crate normal map: %s", crateNormalPath.c_str());
        return false;
    }

    // Ground/grass textures
    std::string grassTexturePath = info.resourcePath + "/assets/textures/grass/grass/grass01.jpg";
    groundTexture_ = assetRegistry_->get().loadTexture(grassTexturePath, srgbConfig);
    if (!groundTexture_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load grass texture: %s", grassTexturePath.c_str());
        return false;
    }

    std::string grassNormalPath = info.resourcePath + "/assets/textures/grass/grass/grass01_n.jpg";
    groundNormal_ = assetRegistry_->get().loadTexture(grassNormalPath, linearConfig);
    if (!groundNormal_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load grass normal map: %s", grassNormalPath.c_str());
        return false;
    }

    // Metal textures
    std::string metalTexturePath = info.resourcePath + "/assets/textures/industrial/metal_1.jpg";
    metalTexture_ = assetRegistry_->get().loadTexture(metalTexturePath, srgbConfig);
    if (!metalTexture_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load metal texture: %s", metalTexturePath.c_str());
        return false;
    }

    std::string metalNormalPath = info.resourcePath + "/assets/textures/industrial/metal_1_norm.jpg";
    metalNormal_ = assetRegistry_->get().loadTexture(metalNormalPath, linearConfig);
    if (!metalNormal_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load metal normal map: %s", metalNormalPath.c_str());
        return false;
    }

    // Create default black emissive map for objects without emissive textures
    defaultEmissive_ = assetRegistry_->get().createSolidColorTexture(0, 0, 0, 255, "default_emissive");
    if (!defaultEmissive_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create default emissive map");
        return false;
    }

    // Create white texture for vertex-colored objects (like glTF characters)
    whiteTexture_ = assetRegistry_->get().createSolidColorTexture(255, 255, 255, 255, "white");
    if (!whiteTexture_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create white texture");
        return false;
    }

    SDL_Log("SceneBuilder: Loaded 8 textures via AssetRegistry");
    return true;
}

void SceneBuilder::createRenderables() {
    sceneObjects.clear();
    physicsEnabledIndices.clear();

    // Ground disc removed - terrain system provides the ground now

    // Scene objects are placed relative to sceneOrigin (settlement location)
    const float originX = sceneOrigin.x;
    const float originZ = sceneOrigin.y;

    // Cache texture pointers from registry (const_cast needed as Renderable uses non-const ptr)
    Texture* crateTex = const_cast<Texture*>(getCrateTexture());
    Texture* metalTex = const_cast<Texture*>(getMetalTexture());
    Texture* whiteTex = const_cast<Texture*>(getWhiteTexture());

    // Helper: get world position with scene origin offset
    auto worldPos = [originX, originZ](float localX, float localZ) -> std::pair<float, float> {
        return {localX + originX, localZ + originZ};
    };

    // Helper: get Y position for object sitting on terrain
    // objectHeight is the distance from object origin to its bottom
    auto getGroundY = [this](float x, float z, float objectHeight) {
        return getTerrainHeight(x, z) + objectHeight;
    };

    // Helper to add physics-enabled objects
    auto addPhysicsObject = [this](Renderable&& r) -> size_t {
        size_t idx = sceneObjects.size();
        sceneObjects.push_back(std::move(r));
        physicsEnabledIndices.push_back(idx);
        return idx;
    };

    // Wooden crate - slightly shiny, non-metallic (unit cube, half-extent 0.5)
    auto [crateX, crateZ] = worldPos(2.0f, 0.0f);
    addPhysicsObject(RenderableBuilder()
        .atPosition(glm::vec3(crateX, getGroundY(crateX, crateZ, 0.5f), crateZ))
        .withMesh(cubeMesh.get())
        .withTexture(crateTex)
        .withMaterialId(crateMaterialId)
        .withRoughness(0.4f)
        .withMetallic(0.0f)
        .build());

    // Rotated wooden crate
    auto [rotatedCrateX, rotatedCrateZ] = worldPos(-1.5f, 1.0f);
    addPhysicsObject(RenderableBuilder()
        .withTransform(Transform(
            glm::vec3(rotatedCrateX, getGroundY(rotatedCrateX, rotatedCrateZ, 0.5f), rotatedCrateZ),
            Transform::yRotation(glm::radians(30.0f))))
        .withMesh(cubeMesh.get())
        .withTexture(crateTex)
        .withMaterialId(crateMaterialId)
        .withRoughness(0.4f)
        .withMetallic(0.0f)
        .build());

    // Polished metal sphere - smooth, fully metallic (radius 0.5)
    auto [polishedSphereX, polishedSphereZ] = worldPos(0.0f, -2.0f);
    addPhysicsObject(RenderableBuilder()
        .atPosition(glm::vec3(polishedSphereX, getGroundY(polishedSphereX, polishedSphereZ, 0.5f), polishedSphereZ))
        .withMesh(sphereMesh.get())
        .withTexture(metalTex)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.1f)
        .withMetallic(1.0f)
        .build());

    // Rough/brushed metal sphere - moderately rough, metallic (radius 0.5)
    auto [roughSphereX, roughSphereZ] = worldPos(-3.0f, -1.0f);
    addPhysicsObject(RenderableBuilder()
        .atPosition(glm::vec3(roughSphereX, getGroundY(roughSphereX, roughSphereZ, 0.5f), roughSphereZ))
        .withMesh(sphereMesh.get())
        .withTexture(metalTex)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.5f)
        .withMetallic(1.0f)
        .build());

    // Polished metal cube - smooth, fully metallic (half-extent 0.5)
    auto [polishedCubeX, polishedCubeZ] = worldPos(3.0f, -2.0f);
    addPhysicsObject(RenderableBuilder()
        .atPosition(glm::vec3(polishedCubeX, getGroundY(polishedCubeX, polishedCubeZ, 0.5f), polishedCubeZ))
        .withMesh(cubeMesh.get())
        .withTexture(metalTex)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.1f)
        .withMetallic(1.0f)
        .build());

    // Brushed metal cube - rough, metallic (half-extent 0.5)
    auto [brushedCubeX, brushedCubeZ] = worldPos(-3.0f, -3.0f);
    addPhysicsObject(RenderableBuilder()
        .withTransform(Transform(
            glm::vec3(brushedCubeX, getGroundY(brushedCubeX, brushedCubeZ, 0.5f), brushedCubeZ),
            Transform::yRotation(glm::radians(45.0f))))
        .withMesh(cubeMesh.get())
        .withTexture(metalTex)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.6f)
        .withMetallic(1.0f)
        .build());

    // Glowing emissive sphere on top of the first crate - demonstrates bloom effect
    // Sits 0.8m above crate (crate top at terrain+1.0, sphere center at terrain+1.0+0.3)
    // This object has physics AND is tracked as the emissive orb for light sync
    float glowSphereScale = 0.3f;
    emissiveOrbIndex = addPhysicsObject(RenderableBuilder()
        .withTransform(Transform(
            glm::vec3(crateX, getGroundY(crateX, crateZ, 1.0f + glowSphereScale), crateZ),
            glm::quat(1, 0, 0, 0),  // Identity rotation
            glowSphereScale))
        .withMesh(sphereMesh.get())
        .withTexture(metalTex)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.2f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(25.0f)
        .withEmissiveColor(glm::vec3(1.0f, 0.9f, 0.7f))
        .withCastsShadow(false)
        .build());

    // Blue light indicator sphere - saturated blue, floating above terrain
    auto [blueLightX, blueLightZ] = worldPos(-3.0f, 2.0f);
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(Transform(
            glm::vec3(blueLightX, getGroundY(blueLightX, blueLightZ, 1.5f), blueLightZ),
            glm::quat(1, 0, 0, 0), 0.2f))
        .withMesh(sphereMesh.get())
        .withTexture(metalTex)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.2f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(4.0f)
        .withEmissiveColor(glm::vec3(0.0f, 0.3f, 1.0f))
        .withCastsShadow(false)
        .build());

    // Green light indicator sphere - saturated green, floating above terrain
    auto [greenLightX, greenLightZ] = worldPos(4.0f, -2.0f);
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(Transform(
            glm::vec3(greenLightX, getGroundY(greenLightX, greenLightZ, 1.5f), greenLightZ),
            glm::quat(1, 0, 0, 0), 0.2f))
        .withMesh(sphereMesh.get())
        .withTexture(metalTex)
        .withMaterialId(metalMaterialId)
        .withRoughness(0.2f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(3.0f)
        .withEmissiveColor(glm::vec3(0.0f, 1.0f, 0.2f))
        .withCastsShadow(false)
        .build());

    // Debug cube - red emissive cube for testing (half-extent 0.5)
    auto [debugCubeX, debugCubeZ] = worldPos(5.0f, -5.0f);
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(debugCubeX, getGroundY(debugCubeX, debugCubeZ, 0.5f), debugCubeZ))
        .withMesh(cubeMesh.get())
        .withTexture(crateTex)
        .withMaterialId(crateMaterialId)
        .withRoughness(0.3f)
        .withMetallic(0.0f)
        .withEmissiveIntensity(5.0f)
        .withEmissiveColor(glm::vec3(1.0f, 0.0f, 0.0f))
        .build());

    // Player character - uses animated character if loaded, otherwise capsule
    // Player position is controlled by physics, so we place at scene origin
    auto [playerX, playerZ] = worldPos(0.0f, 0.0f);
    float playerTerrainY = getTerrainHeight(playerX, playerZ);
    playerObjectIndex = sceneObjects.size();
    if (hasAnimatedCharacter) {
        // Use materials from FBX if available, otherwise use defaults
        float charRoughness = 0.5f;  // Default roughness for more specular highlights
        float charMetallic = 0.0f;
        glm::vec3 charEmissiveColor = glm::vec3(0.0f);
        float charEmissiveIntensity = 0.0f;

        const auto& materials = animatedCharacter->getMaterials();
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
            .withTransform(buildCharacterTransform(glm::vec3(playerX, playerTerrainY, playerZ), 10.0f))
            .withMesh(&animatedCharacter->getMesh())
            .withTexture(whiteTex)  // White texture so vertex colors show through
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
            .withMesh(capsuleMesh.get())
            .withTexture(metalTex)
            .withMaterialId(metalMaterialId)
            .withRoughness(0.3f)
            .withMetallic(0.8f)
            .withCastsShadow(true)
            .build());
    }

    // Player weapons - attached to hand bones, transforms updated each frame
    if (hasAnimatedCharacter && rightHandBoneIndex >= 0) {
        swordIndex = sceneObjects.size();
        sceneObjects.push_back(RenderableBuilder()
            .withTransform(glm::mat4(1.0f))  // Updated per frame
            .withMesh(swordMesh.get())
            .withTexture(metalTex)
            .withMaterialId(metalMaterialId)
            .withRoughness(0.2f)
            .withMetallic(0.95f)
            .withCastsShadow(true)
            .build());
        SDL_Log("SceneBuilder: Added sword renderable at index %zu", swordIndex);
    }
    if (hasAnimatedCharacter && leftHandBoneIndex >= 0) {
        shieldIndex = sceneObjects.size();
        sceneObjects.push_back(RenderableBuilder()
            .withTransform(glm::mat4(1.0f))  // Updated per frame
            .withMesh(shieldMesh.get())
            .withTexture(metalTex)
            .withMaterialId(metalMaterialId)
            .withRoughness(0.3f)
            .withMetallic(0.9f)
            .withCastsShadow(true)
            .build());
        SDL_Log("SceneBuilder: Added shield renderable at index %zu", shieldIndex);
    }

    // Flag pole - 3m pole, center at 1.5m above ground
    auto [flagPoleX, flagPoleZ] = worldPos(5.0f, 0.0f);
    flagPoleIndex = sceneObjects.size();
    sceneObjects.push_back(RenderableBuilder()
        .atPosition(glm::vec3(flagPoleX, getGroundY(flagPoleX, flagPoleZ, 1.5f), flagPoleZ))
        .withMesh(flagPoleMesh.get())
        .withTexture(metalTex)
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
        .withTexture(crateTex)  // Using crate texture for now
        .withMaterialId(crateMaterialId)
        .withRoughness(0.6f)
        .withMetallic(0.0f)
        .withCastsShadow(true)
        .build());

    // Player cape - attached to character, updated each frame (using metal texture)
    if (hasCapeEnabled) {
        capeIndex = sceneObjects.size();
        sceneObjects.push_back(RenderableBuilder()
            .withTransform(glm::mat4(1.0f))  // Identity, cloth positions are in world space
            .withMesh(&capeMesh)
            .withTexture(metalTex)
            .withMaterialId(capeMaterialId)
            .withRoughness(0.3f)
            .withMetallic(0.8f)
            .withCastsShadow(true)
            .build());
    }

    // Well entrance - demonstrates terrain hole mask system
    // A stone-like frame floating above the terrain hole
    auto [wellX, wellZ] = worldPos(20.0f, 20.0f);
    wellEntranceX = wellX;
    wellEntranceZ = wellZ;
    float wellY = getTerrainHeight(wellEntranceX, wellEntranceZ);
    // Frame floats 3m above terrain so hole is visible
    glm::mat4 wellTransform = glm::translate(glm::mat4(1.0f),
        glm::vec3(wellEntranceX, wellY + 3.0f, wellEntranceZ));
    wellTransform = glm::scale(wellTransform, glm::vec3(2.0f, 0.5f, 12.0f));
    wellEntranceIndex = sceneObjects.size();
    sceneObjects.push_back(RenderableBuilder()
        .withTransform(wellTransform)
        .withMesh(cubeMesh.get())
        .withTexture(metalTex)  // Stone-like appearance
        .withMaterialId(metalMaterialId)
        .withRoughness(0.8f)
        .withMetallic(0.1f)
        .withCastsShadow(true)
        .build());
}

void SceneBuilder::uploadFlagClothMesh(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue) {
    flagClothMesh.releaseGPUResources();
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

    animatedCharacter->update(deltaTime, allocator, device, commandPool, queue,
                                  movementSpeed, isGrounded, isJumping, worldTransform);

    // Update the mesh pointer in the renderable (in case it was re-created)
    if (playerObjectIndex < sceneObjects.size()) {
        sceneObjects[playerObjectIndex].mesh = &animatedCharacter->getMesh();
    }

    // Update player cape if enabled
    if (hasCapeEnabled) {
        // Update cape simulation with current skeleton pose
        playerCape.update(animatedCharacter->getSkeleton(), worldTransform, deltaTime, nullptr);

        // Update cape mesh and re-upload
        playerCape.updateMesh(capeMesh);
        capeMesh.releaseGPUResources();
        capeMesh.upload(allocator, device, commandPool, queue);

        // Update mesh pointer in renderable
        if (capeIndex < sceneObjects.size()) {
            sceneObjects[capeIndex].mesh = &capeMesh;
        }
    }
}

void SceneBuilder::startCharacterJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics) {
    if (!hasAnimatedCharacter) return;
    animatedCharacter->startJump(startPos, velocity, gravity, physics);
}

void SceneBuilder::updateWeaponTransforms(const glm::mat4& characterWorldTransform) {
    if (!hasAnimatedCharacter) return;

    // Compute global bone transforms
    const auto& skeleton = animatedCharacter->getSkeleton();
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    // Update sword transform (attached to right hand)
    if (rightHandBoneIndex >= 0 && swordIndex < sceneObjects.size()) {
        // Get the right hand bone's global transform
        glm::mat4 boneGlobal = globalTransforms[rightHandBoneIndex];

        // Combine with character world transform
        glm::mat4 weaponWorld = characterWorldTransform * boneGlobal;

        // Offset sword so it extends from the hand:
        // - Rotate to point along the hand's direction (along Y axis of the bone)
        // - Translate so the base is at the hand position
        glm::mat4 swordOffset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.4f, 0.0f));
        sceneObjects[swordIndex].transform = weaponWorld * swordOffset;
    }

    // Update shield transform (attached to left hand)
    if (leftHandBoneIndex >= 0 && shieldIndex < sceneObjects.size()) {
        // Get the left hand bone's global transform
        glm::mat4 boneGlobal = globalTransforms[leftHandBoneIndex];

        // Combine with character world transform
        glm::mat4 weaponWorld = characterWorldTransform * boneGlobal;

        // Offset shield so it's on the forearm:
        // - Rotate 90 degrees to face forward (shield face perpendicular to arm)
        // - Translate slightly up the arm
        glm::mat4 shieldOffset = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        shieldOffset = glm::translate(shieldOffset, glm::vec3(0.0f, -0.15f, 0.0f));
        sceneObjects[shieldIndex].transform = weaponWorld * shieldOffset;
    }
}

// Texture accessors (return raw pointer from shared_ptr)
const Texture* SceneBuilder::getGroundTexture() const {
    return groundTexture_.get();
}

const Texture* SceneBuilder::getGroundNormalMap() const {
    return groundNormal_.get();
}

const Texture* SceneBuilder::getCrateTexture() const {
    return crateTexture_.get();
}

const Texture* SceneBuilder::getCrateNormalMap() const {
    return crateNormal_.get();
}

const Texture* SceneBuilder::getMetalTexture() const {
    return metalTexture_.get();
}

const Texture* SceneBuilder::getMetalNormalMap() const {
    return metalNormal_.get();
}

const Texture* SceneBuilder::getDefaultEmissiveMap() const {
    return defaultEmissive_.get();
}

const Texture* SceneBuilder::getWhiteTexture() const {
    return whiteTexture_.get();
}
