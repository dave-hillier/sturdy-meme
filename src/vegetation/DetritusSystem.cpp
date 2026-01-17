#include "DetritusSystem.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

std::unique_ptr<DetritusSystem> DetritusSystem::create(const InitInfo& info, const DetritusConfig& config) {
    auto system = std::make_unique<DetritusSystem>(ConstructToken{});
    if (!system->initInternal(info, config)) {
        return nullptr;
    }
    return system;
}

DetritusSystem::~DetritusSystem() {
    cleanup();
}

bool DetritusSystem::initInternal(const InitInfo& info, const DetritusConfig& cfg) {
    config_ = cfg;

    // Initialize the material with Vulkan context
    SceneMaterial::InitInfo materialInfo;
    materialInfo.device = info.device;
    materialInfo.allocator = info.allocator;
    materialInfo.commandPool = info.commandPool;
    materialInfo.graphicsQueue = info.graphicsQueue;
    materialInfo.physicalDevice = info.physicalDevice;
    materialInfo.resourcePath = info.resourcePath;
    materialInfo.getTerrainHeight = info.getTerrainHeight;
    materialInfo.terrainSize = info.terrainSize;

    SceneMaterial::MaterialProperties matProps;
    matProps.roughness = config_.materialRoughness;
    matProps.metallic = config_.materialMetallic;
    matProps.castsShadow = true;

    material_.init(materialInfo, matProps);

    if (!loadTextures(info)) {
        SDL_Log("DetritusSystem: Failed to load textures");
        return false;
    }

    if (!createBranchMeshes(info)) {
        SDL_Log("DetritusSystem: Failed to create branch meshes");
        return false;
    }

    generatePlacements(info);
    createSceneObjects();

    SDL_Log("DetritusSystem: Initialized with %zu pieces (%zu mesh variations)",
            material_.getInstanceCount(), material_.getMeshVariationCount());

    return true;
}

void DetritusSystem::cleanup() {
    material_.cleanup();
}

bool DetritusSystem::loadTextures(const InitInfo& info) {
    // Use oak bark texture for fallen branches (same textures as TreeSystem)
    std::string texturePath = info.resourcePath + "/textures/bark/oak_color_1k.jpg";
    auto barkTexture = Texture::loadFromFile(texturePath, info.allocator, info.device, info.commandPool,
                                              info.graphicsQueue, info.physicalDevice);
    if (!barkTexture) {
        SDL_Log("DetritusSystem: Failed to load bark texture: %s", texturePath.c_str());
        return false;
    }
    material_.setDiffuseTexture(std::move(barkTexture));

    std::string normalPath = info.resourcePath + "/textures/bark/oak_normal_1k.jpg";
    auto barkNormalMap = Texture::loadFromFile(normalPath, info.allocator, info.device, info.commandPool,
                                                info.graphicsQueue, info.physicalDevice, false);
    if (!barkNormalMap) {
        SDL_Log("DetritusSystem: Failed to load bark normal map: %s", normalPath.c_str());
        return false;
    }
    material_.setNormalTexture(std::move(barkNormalMap));

    return true;
}

bool DetritusSystem::createBranchMeshes(const InitInfo& info) {
    int totalMeshes = config_.branchVariations + config_.forkedVariations;
    std::vector<Mesh> meshes(totalMeshes);

    // Create regular branches with intentional size variation
    for (int i = 0; i < config_.branchVariations; ++i) {
        uint32_t seed = 98765 + i * 1337;

        // Every 3rd branch is deliberately longer, every 4th is gnarlier
        bool makeLong = (i % 3 == 0);
        bool makeGnarly = (i % 4 == 0);

        float t = DeterministicRandom::hashPosition(float(i), 0.0f, seed);
        if (makeLong) t = 0.6f + t * 0.4f;  // Bias to 0.6-1.0 for longer branches
        float length = config_.minLength + t * (config_.maxLength - config_.minLength);

        float r = DeterministicRandom::hashPosition(float(i), 1.0f, seed + 100);
        if (makeLong) r = 0.4f + r * 0.6f;  // Thicker radius for long branches
        float radius = config_.minRadius + r * (config_.maxRadius - config_.minRadius);

        // More sections for longer branches
        int sections = 4 + static_cast<int>(length * 2.5f);
        int segments = 6;

        // Vary taper and gnarliness - higher gnarliness range
        float taper = 0.5f + DeterministicRandom::hashPosition(float(i), 2.0f, seed + 200) * 0.4f;
        float gnarliness = 0.15f + DeterministicRandom::hashPosition(float(i), 3.0f, seed + 300) * 0.35f;
        if (makeGnarly) gnarliness = 0.35f + DeterministicRandom::hashPosition(float(i), 3.0f, seed + 300) * 0.25f;

        meshes[i].createBranch(radius, length, sections, segments, seed, taper, gnarliness);
        meshes[i].upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);

        SDL_Log("DetritusSystem: Created branch mesh %d (r=%.2f, h=%.2f, sections=%d, taper=%.2f, gnarl=%.2f%s%s)",
                i, radius, length, sections, taper, gnarliness,
                makeLong ? " LONG" : "", makeGnarly ? " GNARLED" : "");
    }

    // Create Y-shaped forked branches - these are generally larger and gnarlier
    for (int i = 0; i < config_.forkedVariations; ++i) {
        int meshIdx = config_.branchVariations + i;
        uint32_t seed = 54321 + i * 2741;

        // Forked branches are larger - bias strongly toward upper range
        float t = 0.65f + DeterministicRandom::hashPosition(float(i + 100), 0.0f, seed) * 0.35f;
        float length = config_.minLength + t * (config_.maxLength - config_.minLength);

        float r = 0.5f + DeterministicRandom::hashPosition(float(i + 100), 1.0f, seed + 100) * 0.5f;
        float radius = config_.minRadius + r * (config_.maxRadius - config_.minRadius);

        int sections = 6 + static_cast<int>(length * 2.5f);
        int segments = 6;

        float taper = 0.55f + DeterministicRandom::hashPosition(float(i + 100), 2.0f, seed + 200) * 0.35f;
        float gnarliness = 0.3f + DeterministicRandom::hashPosition(float(i + 100), 3.0f, seed + 300) * 0.35f;
        float forkAngle = 0.3f + DeterministicRandom::hashPosition(float(i + 100), 4.0f, seed + 400) * 0.4f;

        meshes[meshIdx].createForkedBranch(radius, length, sections, segments, seed, taper, gnarliness, forkAngle);
        meshes[meshIdx].upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);

        SDL_Log("DetritusSystem: Created forked branch mesh %d (r=%.2f, h=%.2f, fork=%.2f, gnarl=%.2f)",
                meshIdx, radius, length, forkAngle, gnarliness);
    }

    material_.setMeshes(std::move(meshes));
    return true;
}

void DetritusSystem::generatePlacements(const InitInfo& info) {
    std::vector<SceneObjectInstance> instances;

    const int totalMeshes = config_.branchVariations + config_.forkedVariations;

    // If no tree positions provided, skip placement
    if (info.treePositions.empty()) {
        SDL_Log("DetritusSystem: No tree positions provided, skipping detritus placement");
        return;
    }

    // Limit total detritus to avoid performance issues with many trees
    const int maxTotalDetritus = 100;
    const int numTrees = static_cast<int>(info.treePositions.size());

    // Distribute detritus across trees, but cap total count
    int branchesPerTree = std::max(1, maxTotalDetritus / numTrees);
    branchesPerTree = std::min(branchesPerTree, config_.branchesPerVariation);

    int placed = 0;

    for (int treeIndex = 0; treeIndex < numTrees && placed < maxTotalDetritus; ++treeIndex) {
        const auto& treePos = info.treePositions[treeIndex];

        // Place branches near this tree
        for (int b = 0; b < branchesPerTree && placed < maxTotalDetritus; ++b) {
            // Generate position near tree - use hash for deterministic placement
            uint32_t seed = static_cast<uint32_t>(treeIndex * 1000 + b * 100);
            float angle = DeterministicRandom::hashPosition(float(seed), 0.0f, 12345) * 2.0f * 3.14159f;
            // Distance from tree: 1.5m to placementRadius (default ~8m from trunk)
            float distFromTree = 1.5f + DeterministicRandom::hashPosition(float(seed), 1.0f, 23456) * (config_.placementRadius - 1.5f);

            float x = treePos.x + distFromTree * std::cos(angle);
            float z = treePos.z + distFromTree * std::sin(angle);

            // Check bounds
            float halfTerrain = info.terrainSize * 0.48f;
            if (std::abs(x) > halfTerrain || std::abs(z) > halfTerrain) {
                continue;
            }

            // Get terrain height
            float y = 0.0f;
            if (info.getTerrainHeight) {
                y = info.getTerrainHeight(x, z);
            }

            // Skip areas below tree line (24m elevation)
            if (y < 24.0f) {
                continue;
            }

            // Rotation: fallen branches lie on the ground with random orientations
            float yaw = DeterministicRandom::hashPosition(x, z, 33333) * 2.0f * 3.14159f;

            // Branch is generated pointing UP (Y axis). To make it lie flat,
            // we rotate around X (pitch) by ~π/2.
            float pitch = glm::half_pi<float>() - 0.1f + (DeterministicRandom::hashPosition(x, z, 44444) - 0.5f) * 0.2f;
            float roll = (DeterministicRandom::hashPosition(x, z, 55555) - 0.5f) * 0.3f;

            glm::vec3 eulerAngles(pitch, yaw, roll);

            // Random scale
            float t = DeterministicRandom::hashPosition(x, z, 66666);
            float scale = 0.7f + t * 0.6f;

            // Create detritus instance with full 3D rotation
            instances.push_back(SceneObjectInstance::withEulerAngles(
                glm::vec3(x, y, z),
                eulerAngles,
                scale,
                static_cast<uint32_t>(placed % totalMeshes)
            ));
            placed++;
        }
    }

    material_.setInstances(std::move(instances));
    SDL_Log("DetritusSystem: Placed %d pieces near %d trees (max %d)",
            placed, numTrees, maxTotalDetritus);
}

void DetritusSystem::createSceneObjects() {
    // No transform modification needed - the rotation already includes
    // pitch to lay branches flat on the ground
    material_.rebuildSceneObjects();
}

bool DetritusSystem::createDescriptorSets(
    VkDevice device,
    DescriptorManager::Pool& pool,
    VkDescriptorSetLayout layout,
    uint32_t frameCount,
    std::function<MaterialDescriptorFactory::CommonBindings(uint32_t)> getCommonBindings)
{
    // Allocate descriptor sets
    descriptorSets_ = pool.allocate(layout, frameCount);
    if (descriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DetritusSystem: Failed to allocate descriptor sets");
        return false;
    }

    // Write descriptor sets
    MaterialDescriptorFactory factory(device);
    for (uint32_t i = 0; i < frameCount; i++) {
        MaterialDescriptorFactory::CommonBindings common = getCommonBindings(i);

        MaterialDescriptorFactory::MaterialTextures mat{};
        mat.diffuseView = getBarkTexture().getImageView();
        mat.diffuseSampler = getBarkTexture().getSampler();
        mat.normalView = getBarkNormalMap().getImageView();
        mat.normalSampler = getBarkNormalMap().getSampler();

        factory.writeDescriptorSet(descriptorSets_[i], common, mat);
    }

    SDL_Log("DetritusSystem: Created %u descriptor sets", frameCount);
    return true;
}
