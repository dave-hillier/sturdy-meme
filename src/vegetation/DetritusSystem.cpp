#include "DetritusSystem.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

std::unique_ptr<DetritusSystem> DetritusSystem::create(const InitInfo& info, const DetritusConfig& config) {
    std::unique_ptr<DetritusSystem> system(new DetritusSystem());
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
    storedAllocator_ = info.allocator;
    storedDevice_ = info.device;

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
            instances_.size(), meshes_.size());

    return true;
}

void DetritusSystem::cleanup() {
    if (storedDevice_ == VK_NULL_HANDLE) return;

    // RAII-managed textures
    barkTexture_.reset();
    barkNormalMap_.reset();

    // Manually managed mesh vector
    for (auto& mesh : meshes_) {
        mesh.destroy(storedAllocator_);
    }
    meshes_.clear();

    instances_.clear();
    sceneObjects_.clear();
}

bool DetritusSystem::loadTextures(const InitInfo& info) {
    // Use oak bark texture for fallen branches (same textures as TreeSystem)
    std::string texturePath = info.resourcePath + "/textures/bark/oak_color_1k.jpg";
    barkTexture_ = RAIIAdapter<Texture>::create(
        [&](auto& t) {
            if (!t.load(texturePath, info.allocator, info.device, info.commandPool,
                        info.graphicsQueue, info.physicalDevice)) {
                SDL_Log("DetritusSystem: Failed to load bark texture: %s", texturePath.c_str());
                return false;
            }
            return true;
        },
        [this](auto& t) { t.destroy(storedAllocator_, storedDevice_); }
    );
    if (!barkTexture_) return false;

    std::string normalPath = info.resourcePath + "/textures/bark/oak_normal_1k.jpg";
    barkNormalMap_ = RAIIAdapter<Texture>::create(
        [&](auto& t) {
            if (!t.load(normalPath, info.allocator, info.device, info.commandPool,
                        info.graphicsQueue, info.physicalDevice, false)) {
                SDL_Log("DetritusSystem: Failed to load bark normal map: %s", normalPath.c_str());
                return false;
            }
            return true;
        },
        [this](auto& t) { t.destroy(storedAllocator_, storedDevice_); }
    );
    if (!barkNormalMap_) return false;

    return true;
}

bool DetritusSystem::createBranchMeshes(const InitInfo& info) {
    int totalMeshes = config_.branchVariations + config_.forkedVariations;
    meshes_.resize(totalMeshes);

    // Create regular branches with intentional size variation
    for (int i = 0; i < config_.branchVariations; ++i) {
        uint32_t seed = 98765 + i * 1337;

        // Every 3rd branch is deliberately longer, every 4th is gnarlier
        bool makeLong = (i % 3 == 0);
        bool makeGnarly = (i % 4 == 0);

        float t = hashPosition(float(i), 0.0f, seed);
        if (makeLong) t = 0.6f + t * 0.4f;  // Bias to 0.6-1.0 for longer branches
        float length = config_.minLength + t * (config_.maxLength - config_.minLength);

        float r = hashPosition(float(i), 1.0f, seed + 100);
        if (makeLong) r = 0.4f + r * 0.6f;  // Thicker radius for long branches
        float radius = config_.minRadius + r * (config_.maxRadius - config_.minRadius);

        // More sections for longer branches
        int sections = 4 + static_cast<int>(length * 2.5f);
        int segments = 6;

        // Vary taper and gnarliness - higher gnarliness range
        float taper = 0.5f + hashPosition(float(i), 2.0f, seed + 200) * 0.4f;
        float gnarliness = 0.15f + hashPosition(float(i), 3.0f, seed + 300) * 0.35f;  // 0.15-0.5 range
        if (makeGnarly) gnarliness = 0.35f + hashPosition(float(i), 3.0f, seed + 300) * 0.25f;  // 0.35-0.6

        meshes_[i].createBranch(radius, length, sections, segments, seed, taper, gnarliness);
        meshes_[i].upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);

        SDL_Log("DetritusSystem: Created branch mesh %d (r=%.2f, h=%.2f, sections=%d, taper=%.2f, gnarl=%.2f%s%s)",
                i, radius, length, sections, taper, gnarliness,
                makeLong ? " LONG" : "", makeGnarly ? " GNARLED" : "");
    }

    // Create Y-shaped forked branches - these are generally larger and gnarlier
    for (int i = 0; i < config_.forkedVariations; ++i) {
        int meshIdx = config_.branchVariations + i;
        uint32_t seed = 54321 + i * 2741;

        // Forked branches are larger - bias strongly toward upper range
        float t = 0.65f + hashPosition(float(i + 100), 0.0f, seed) * 0.35f;  // 0.65-1.0 range
        float length = config_.minLength + t * (config_.maxLength - config_.minLength);

        float r = 0.5f + hashPosition(float(i + 100), 1.0f, seed + 100) * 0.5f;  // 0.5-1.0 range
        float radius = config_.minRadius + r * (config_.maxRadius - config_.minRadius);

        int sections = 6 + static_cast<int>(length * 2.5f);
        int segments = 6;

        float taper = 0.55f + hashPosition(float(i + 100), 2.0f, seed + 200) * 0.35f;
        float gnarliness = 0.3f + hashPosition(float(i + 100), 3.0f, seed + 300) * 0.35f;  // 0.3-0.65 range - gnarlier
        float forkAngle = 0.3f + hashPosition(float(i + 100), 4.0f, seed + 400) * 0.4f;  // 0.3-0.7 radians

        meshes_[meshIdx].createForkedBranch(radius, length, sections, segments, seed, taper, gnarliness, forkAngle);
        meshes_[meshIdx].upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);

        SDL_Log("DetritusSystem: Created forked branch mesh %d (r=%.2f, h=%.2f, fork=%.2f, gnarl=%.2f)",
                meshIdx, radius, length, forkAngle, gnarliness);
    }

    return true;
}

// Note: Complex branch mesh generation removed - now using simple cylinders
// like RockSystem uses createRock()

float DetritusSystem::hashPosition(float x, float z, uint32_t seed) const {
    uint32_t ix = *reinterpret_cast<uint32_t*>(&x);
    uint32_t iz = *reinterpret_cast<uint32_t*>(&z);
    uint32_t n = ix ^ (iz * 1597334673U) ^ seed;
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    return float(n & 0x7fffffffU) / float(0x7fffffff);
}

void DetritusSystem::generatePlacements(const InitInfo& info) {
    instances_.clear();

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
            float angle = hashPosition(float(seed), 0.0f, 12345) * 2.0f * 3.14159f;
            // Distance from tree: 1.5m to placementRadius (default ~8m from trunk)
            float distFromTree = 1.5f + hashPosition(float(seed), 1.0f, 23456) * (config_.placementRadius - 1.5f);

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

            // Skip very low areas (water level)
            if (y < 1.0f) {
                continue;
            }

            // Create detritus instance
            DetritusInstance instance;
            instance.position = glm::vec3(x, y, z);

            // Rotation: fallen branches lie on the ground with random orientations
            float yaw = hashPosition(x, z, 33333) * 2.0f * 3.14159f;

            // Branch is generated pointing UP (Y axis). To make it lie flat,
            // we rotate around X (pitch) by ~π/2.
            float pitch = glm::half_pi<float>() - 0.1f + (hashPosition(x, z, 44444) - 0.5f) * 0.2f;
            float roll = (hashPosition(x, z, 55555) - 0.5f) * 0.3f;

            instance.rotation = glm::vec3(pitch, yaw, roll);

            // Random scale
            float t = hashPosition(x, z, 66666);
            instance.scale = 0.7f + t * 0.6f;

            // Assign mesh variation
            instance.meshVariation = placed % totalMeshes;

            instances_.push_back(instance);
            placed++;
        }
    }

    SDL_Log("DetritusSystem: Placed %d pieces near %d trees (max %d)",
            placed, numTrees, maxTotalDetritus);
}

void DetritusSystem::createSceneObjects() {
    sceneObjects_.clear();
    sceneObjects_.reserve(instances_.size());

    for (const auto& instance : instances_) {
        // Build transform: translate, rotate (Euler: pitch, yaw, roll), scale
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), instance.position);

        // Apply rotations (Y-X-Z order for Euler angles)
        transform = glm::rotate(transform, instance.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));  // Yaw
        transform = glm::rotate(transform, instance.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));  // Pitch
        transform = glm::rotate(transform, instance.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));  // Roll

        transform = glm::scale(transform, glm::vec3(instance.scale));

        sceneObjects_.push_back(RenderableBuilder()
            .withTransform(transform)
            .withMesh(&meshes_[instance.meshVariation])
            .withTexture(&**barkTexture_)
            .withRoughness(config_.materialRoughness)
            .withMetallic(config_.materialMetallic)
            .withCastsShadow(true)
            .build());
    }
}
