#include "RockSystem.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

std::unique_ptr<RockSystem> RockSystem::create(const InitInfo& info, const RockConfig& config) {
    std::unique_ptr<RockSystem> system(new RockSystem());
    if (!system->initInternal(info, config)) {
        return nullptr;
    }
    return system;
}

RockSystem::~RockSystem() {
    cleanup();
}

bool RockSystem::initInternal(const InitInfo& info, const RockConfig& cfg) {
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
        SDL_Log("RockSystem: Failed to load textures");
        return false;
    }

    if (!createRockMeshes(info)) {
        SDL_Log("RockSystem: Failed to create rock meshes");
        return false;
    }

    generateRockPlacements(info);
    createSceneObjects();

    SDL_Log("RockSystem: Initialized with %zu rocks (%zu mesh variations)",
            material_.getInstanceCount(), material_.getMeshVariationCount());

    return true;
}

void RockSystem::cleanup() {
    material_.cleanup();
}

bool RockSystem::loadTextures(const InitInfo& info) {
    // Use concrete texture as a rock-like surface
    std::string texturePath = info.resourcePath + "/assets/textures/industrial/concrete_1.jpg";
    auto rockTexture = Texture::loadFromFile(texturePath, info.allocator, info.device, info.commandPool,
                                              info.graphicsQueue, info.physicalDevice);
    if (!rockTexture) {
        SDL_Log("RockSystem: Failed to load rock texture: %s", texturePath.c_str());
        return false;
    }
    material_.setDiffuseTexture(std::move(rockTexture));

    std::string normalPath = info.resourcePath + "/assets/textures/industrial/concrete_1_norm.jpg";
    auto rockNormalMap = Texture::loadFromFile(normalPath, info.allocator, info.device, info.commandPool,
                                                info.graphicsQueue, info.physicalDevice, false);
    if (!rockNormalMap) {
        SDL_Log("RockSystem: Failed to load rock normal map: %s", normalPath.c_str());
        return false;
    }
    material_.setNormalTexture(std::move(rockNormalMap));

    return true;
}

bool RockSystem::createRockMeshes(const InitInfo& info) {
    std::vector<Mesh> meshes(config_.rockVariations);

    for (int i = 0; i < config_.rockVariations; ++i) {
        // Use different seeds for each variation
        uint32_t seed = 12345 + i * 7919;  // Prime number for better distribution

        // Vary parameters slightly for each rock type
        float roughnessVariation = config_.roughness * (0.8f + 0.4f * DeterministicRandom::hashPosition(float(i), 0.0f, seed));
        float asymmetryVariation = config_.asymmetry * (0.7f + 0.6f * DeterministicRandom::hashPosition(float(i), 1.0f, seed + 100));

        meshes[i].createRock(1.0f, config_.subdivisions, seed, roughnessVariation, asymmetryVariation);
        meshes[i].upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);
    }

    material_.setMeshes(std::move(meshes));
    return true;
}

void RockSystem::generateRockPlacements(const InitInfo& info) {
    std::vector<SceneObjectInstance> instances;

    // Use Poisson disk-like sampling for natural rock distribution
    const int totalRocks = config_.rockVariations * config_.rocksPerVariation;
    const float minDist = config_.minDistanceBetween;
    const float minDistSq = minDist * minDist;

    // Golden angle for spiral distribution
    const float goldenAngle = 2.39996323f;

    int placed = 0;
    int attempts = 0;
    const int maxAttempts = totalRocks * 20;

    while (placed < totalRocks && attempts < maxAttempts) {
        attempts++;

        // Generate candidate position using various methods
        float x, z;

        if (attempts % 3 == 0) {
            // Spiral distribution
            float radius = config_.placementRadius * std::sqrt(float(placed + 1) / float(totalRocks + 1));
            float angle = placed * goldenAngle;
            x = radius * std::cos(angle);
            z = radius * std::sin(angle);
        } else {
            // Random with hash
            float angle = DeterministicRandom::hashPosition(float(attempts), 0.0f, 54321) * 2.0f * 3.14159f;
            float radius = std::sqrt(DeterministicRandom::hashPosition(float(attempts), 1.0f, 54322)) * config_.placementRadius;
            x = radius * std::cos(angle);
            z = radius * std::sin(angle);
        }

        // Add some jitter
        x += (DeterministicRandom::hashPosition(x, z, 11111) - 0.5f) * minDist * 0.5f;
        z += (DeterministicRandom::hashPosition(x, z, 22222) - 0.5f) * minDist * 0.5f;

        // Offset by placement center
        x += config_.placementCenter.x;
        z += config_.placementCenter.y;

        // Check bounds (rocks must be inside terrain)
        float halfTerrain = info.terrainSize * 0.48f;  // Stay slightly inside terrain
        if (std::abs(x) > halfTerrain || std::abs(z) > halfTerrain) {
            continue;
        }

        // Check distance from existing instances
        bool tooClose = false;
        for (const auto& existing : instances) {
            float dx = x - existing.position.x;
            float dz = z - existing.position.z;
            if (dx * dx + dz * dz < minDistSq) {
                tooClose = true;
                break;
            }
        }

        if (tooClose) {
            continue;
        }

        // Get terrain height at this position
        float y = 0.0f;
        if (info.getTerrainHeight) {
            y = info.getTerrainHeight(x, z);
        }

        // Skip very steep or very low areas (water level)
        if (y < 0.5f) {
            continue;
        }

        // Random rotation and scale
        float rotation = DeterministicRandom::hashPosition(x, z, 33333) * 2.0f * 3.14159f;
        float t = DeterministicRandom::hashPosition(x, z, 44444);
        float scale = config_.minRadius + t * (config_.maxRadius - config_.minRadius);

        // Create rock instance with Y-axis rotation
        instances.push_back(SceneObjectInstance::withYRotation(
            glm::vec3(x, y, z),
            rotation,
            scale,
            static_cast<uint32_t>(placed % config_.rockVariations)
        ));
        placed++;
    }

    material_.setInstances(std::move(instances));
    SDL_Log("RockSystem: Placed %d rocks in %d attempts", placed, attempts);
}

void RockSystem::createSceneObjects() {
    // Use transform modifier to add tilt and sink rocks into ground
    material_.rebuildSceneObjects([](const SceneObjectInstance& instance, const glm::mat4& baseTransform) {
        glm::mat4 transform = baseTransform;

        // Add slight random tilt for natural appearance
        float tiltX = (DeterministicRandom::hashPosition(instance.position.x, instance.position.z, 55555) - 0.5f) * 0.15f;
        float tiltZ = (DeterministicRandom::hashPosition(instance.position.x, instance.position.z, 66666) - 0.5f) * 0.15f;

        // Apply tilt after base transform rotation but before scale
        // We need to decompose, apply tilt, and recompose
        glm::mat4 tiltedTransform = glm::translate(glm::mat4(1.0f), instance.position);
        tiltedTransform = tiltedTransform * glm::mat4_cast(instance.rotation);
        tiltedTransform = glm::rotate(tiltedTransform, tiltX, glm::vec3(1.0f, 0.0f, 0.0f));
        tiltedTransform = glm::rotate(tiltedTransform, tiltZ, glm::vec3(0.0f, 0.0f, 1.0f));
        tiltedTransform = glm::scale(tiltedTransform, glm::vec3(instance.scale));

        // Sink rock slightly into ground
        tiltedTransform[3][1] -= instance.scale * 0.15f;

        return tiltedTransform;
    });
}
