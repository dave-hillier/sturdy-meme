#include "ScatterSystemFactory.h"
#include "Mesh.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace ScatterSystemFactory {

// ============================================================================
// Rock System Implementation
// ============================================================================

namespace {

std::vector<Mesh> generateRockMeshes(
    const ScatterSystem::InitInfo& info,
    const RockConfig& config)
{
    std::vector<Mesh> meshes(config.rockVariations);

    for (int i = 0; i < config.rockVariations; ++i) {
        uint32_t seed = 12345 + i * 7919;

        float roughnessVariation = config.roughness *
            (0.8f + 0.4f * DeterministicRandom::hashPosition(float(i), 0.0f, seed));
        float asymmetryVariation = config.asymmetry *
            (0.7f + 0.6f * DeterministicRandom::hashPosition(float(i), 1.0f, seed + 100));

        meshes[i].createRock(1.0f, config.subdivisions, seed, roughnessVariation, asymmetryVariation);
        meshes[i].upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);
    }

    return meshes;
}

std::vector<SceneObjectInstance> generateRockPlacements(
    const ScatterSystem::InitInfo& info,
    const RockConfig& config)
{
    std::vector<SceneObjectInstance> instances;

    const int totalRocks = config.rockVariations * config.rocksPerVariation;
    const float minDist = config.minDistanceBetween;
    const float minDistSq = minDist * minDist;
    const float goldenAngle = 2.39996323f;

    int placed = 0;
    int attempts = 0;
    const int maxAttempts = totalRocks * 20;

    while (placed < totalRocks && attempts < maxAttempts) {
        attempts++;

        float x, z;
        if (attempts % 3 == 0) {
            // Spiral distribution
            float radius = config.placementRadius * std::sqrt(float(placed + 1) / float(totalRocks + 1));
            float angle = placed * goldenAngle;
            x = radius * std::cos(angle);
            z = radius * std::sin(angle);
        } else {
            // Random with hash
            float angle = DeterministicRandom::hashPosition(float(attempts), 0.0f, 54321) * 2.0f * 3.14159f;
            float radius = std::sqrt(DeterministicRandom::hashPosition(float(attempts), 1.0f, 54322)) * config.placementRadius;
            x = radius * std::cos(angle);
            z = radius * std::sin(angle);
        }

        // Add jitter
        x += (DeterministicRandom::hashPosition(x, z, 11111) - 0.5f) * minDist * 0.5f;
        z += (DeterministicRandom::hashPosition(x, z, 22222) - 0.5f) * minDist * 0.5f;

        // Offset by placement center
        x += config.placementCenter.x;
        z += config.placementCenter.y;

        // Check bounds
        float halfTerrain = info.terrainSize * 0.48f;
        if (std::abs(x) > halfTerrain || std::abs(z) > halfTerrain) {
            continue;
        }

        // Check distance from existing instances
        bool tooClose = false;
        for (const auto& existing : instances) {
            float dx = x - existing.position().x;
            float dz = z - existing.position().z;
            if (dx * dx + dz * dz < minDistSq) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        // Get terrain height
        float y = 0.0f;
        if (info.getTerrainHeight) {
            y = info.getTerrainHeight(x, z);
        }

        // Skip very low areas (water level)
        if (y < 0.5f) continue;

        // Random rotation and scale
        float rotation = DeterministicRandom::hashPosition(x, z, 33333) * 2.0f * 3.14159f;
        float t = DeterministicRandom::hashPosition(x, z, 44444);
        float scale = config.minRadius + t * (config.maxRadius - config.minRadius);

        instances.push_back(SceneObjectInstance::withYRotation(
            glm::vec3(x, y, z),
            rotation,
            scale,
            static_cast<uint32_t>(placed % config.rockVariations)
        ));
        placed++;
    }

    SDL_Log("ScatterSystemFactory: Placed %d rocks in %d attempts", placed, attempts);
    return instances;
}

glm::mat4 rockTransformModifier(const SceneObjectInstance& instance, const glm::mat4& baseTransform) {
    const auto& t = instance.transform;

    // Add slight random tilt for natural appearance
    float tiltX = (DeterministicRandom::hashPosition(t.position.x, t.position.z, 55555) - 0.5f) * 0.15f;
    float tiltZ = (DeterministicRandom::hashPosition(t.position.x, t.position.z, 66666) - 0.5f) * 0.15f;

    glm::mat4 tiltedTransform = glm::translate(glm::mat4(1.0f), t.position);
    tiltedTransform = tiltedTransform * glm::mat4_cast(t.rotation);
    tiltedTransform = glm::rotate(tiltedTransform, tiltX, glm::vec3(1.0f, 0.0f, 0.0f));
    tiltedTransform = glm::rotate(tiltedTransform, tiltZ, glm::vec3(0.0f, 0.0f, 1.0f));
    tiltedTransform = glm::scale(tiltedTransform, t.scale);

    // Sink rock slightly into ground
    tiltedTransform[3][1] -= t.scale.x * 0.15f;

    return tiltedTransform;
}

} // anonymous namespace

std::unique_ptr<ScatterSystem> createRocks(
    const ScatterSystem::InitInfo& info,
    const RockConfig& config)
{
    auto meshes = generateRockMeshes(info, config);
    auto instances = generateRockPlacements(info, config);

    ScatterSystem::Config sysConfig;
    sysConfig.name = "rocks";
    sysConfig.diffuseTexturePath = "assets/textures/industrial/concrete_1.jpg";
    sysConfig.normalTexturePath = "assets/textures/industrial/concrete_1_norm.jpg";
    sysConfig.materialRoughness = config.materialRoughness;
    sysConfig.materialMetallic = config.materialMetallic;
    sysConfig.castsShadow = true;

    return ScatterSystem::create(info, sysConfig, std::move(meshes), std::move(instances), rockTransformModifier);
}

// ============================================================================
// Detritus System Implementation
// ============================================================================

namespace {

std::vector<Mesh> generateDetritusMeshes(
    const ScatterSystem::InitInfo& info,
    const DetritusConfig& config)
{
    int totalMeshes = config.branchVariations + config.forkedVariations;
    std::vector<Mesh> meshes(totalMeshes);

    // Create regular branches
    for (int i = 0; i < config.branchVariations; ++i) {
        uint32_t seed = 98765 + i * 1337;

        bool makeLong = (i % 3 == 0);
        bool makeGnarly = (i % 4 == 0);

        float t = DeterministicRandom::hashPosition(float(i), 0.0f, seed);
        if (makeLong) t = 0.6f + t * 0.4f;
        float length = config.minLength + t * (config.maxLength - config.minLength);

        float r = DeterministicRandom::hashPosition(float(i), 1.0f, seed + 100);
        if (makeLong) r = 0.4f + r * 0.6f;
        float radius = config.minRadius + r * (config.maxRadius - config.minRadius);

        int sections = 4 + static_cast<int>(length * 2.5f);
        int segments = 6;

        float taper = 0.5f + DeterministicRandom::hashPosition(float(i), 2.0f, seed + 200) * 0.4f;
        float gnarliness = 0.15f + DeterministicRandom::hashPosition(float(i), 3.0f, seed + 300) * 0.35f;
        if (makeGnarly) gnarliness = 0.35f + DeterministicRandom::hashPosition(float(i), 3.0f, seed + 300) * 0.25f;

        meshes[i].createBranch(radius, length, sections, segments, seed, taper, gnarliness);
        meshes[i].upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);
    }

    // Create Y-shaped forked branches
    for (int i = 0; i < config.forkedVariations; ++i) {
        int meshIdx = config.branchVariations + i;
        uint32_t seed = 54321 + i * 2741;

        float t = 0.65f + DeterministicRandom::hashPosition(float(i + 100), 0.0f, seed) * 0.35f;
        float length = config.minLength + t * (config.maxLength - config.minLength);

        float r = 0.5f + DeterministicRandom::hashPosition(float(i + 100), 1.0f, seed + 100) * 0.5f;
        float radius = config.minRadius + r * (config.maxRadius - config.minRadius);

        int sections = 6 + static_cast<int>(length * 2.5f);
        int segments = 6;

        float taper = 0.55f + DeterministicRandom::hashPosition(float(i + 100), 2.0f, seed + 200) * 0.35f;
        float gnarliness = 0.3f + DeterministicRandom::hashPosition(float(i + 100), 3.0f, seed + 300) * 0.35f;
        float forkAngle = 0.3f + DeterministicRandom::hashPosition(float(i + 100), 4.0f, seed + 400) * 0.4f;

        meshes[meshIdx].createForkedBranch(radius, length, sections, segments, seed, taper, gnarliness, forkAngle);
        meshes[meshIdx].upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);
    }

    return meshes;
}

std::vector<SceneObjectInstance> generateDetritusPlacements(
    const ScatterSystem::InitInfo& info,
    const DetritusConfig& config,
    const std::vector<glm::vec3>& treePositions)
{
    std::vector<SceneObjectInstance> instances;

    if (treePositions.empty()) {
        SDL_Log("ScatterSystemFactory: No tree positions provided, skipping detritus placement");
        return instances;
    }

    const int totalMeshes = config.branchVariations + config.forkedVariations;
    const int numTrees = static_cast<int>(treePositions.size());
    int branchesPerTree = std::max(1, config.maxTotal / numTrees);
    branchesPerTree = std::min(branchesPerTree, config.branchesPerVariation);

    int placed = 0;

    for (int treeIndex = 0; treeIndex < numTrees && placed < config.maxTotal; ++treeIndex) {
        const auto& treePos = treePositions[treeIndex];

        for (int b = 0; b < branchesPerTree && placed < config.maxTotal; ++b) {
            uint32_t seed = static_cast<uint32_t>(treeIndex * 1000 + b * 100);
            float angle = DeterministicRandom::hashPosition(float(seed), 0.0f, 12345) * 2.0f * 3.14159f;
            float distFromTree = 1.5f + DeterministicRandom::hashPosition(float(seed), 1.0f, 23456) * (config.placementRadius - 1.5f);

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

            // Skip areas below minimum elevation
            if (y < config.minElevation) {
                continue;
            }

            // Rotation: fallen branches lie on the ground
            float yaw = DeterministicRandom::hashPosition(x, z, 33333) * 2.0f * 3.14159f;
            float pitch = glm::half_pi<float>() - 0.1f + (DeterministicRandom::hashPosition(x, z, 44444) - 0.5f) * 0.2f;
            float roll = (DeterministicRandom::hashPosition(x, z, 55555) - 0.5f) * 0.3f;

            glm::vec3 eulerAngles(pitch, yaw, roll);

            // Random scale
            float t = DeterministicRandom::hashPosition(x, z, 66666);
            float scale = 0.7f + t * 0.6f;

            instances.push_back(SceneObjectInstance::withEulerAngles(
                glm::vec3(x, y, z),
                eulerAngles,
                scale,
                static_cast<uint32_t>(placed % totalMeshes)
            ));
            placed++;
        }
    }

    SDL_Log("ScatterSystemFactory: Placed %d detritus pieces near %d trees", placed, numTrees);
    return instances;
}

} // anonymous namespace

std::unique_ptr<ScatterSystem> createDetritus(
    const ScatterSystem::InitInfo& info,
    const DetritusConfig& config,
    const std::vector<glm::vec3>& treePositions)
{
    auto meshes = generateDetritusMeshes(info, config);
    auto instances = generateDetritusPlacements(info, config, treePositions);

    ScatterSystem::Config sysConfig;
    sysConfig.name = "detritus";
    sysConfig.diffuseTexturePath = "textures/bark/oak_color_1k.jpg";
    sysConfig.normalTexturePath = "textures/bark/oak_normal_1k.jpg";
    sysConfig.materialRoughness = config.materialRoughness;
    sysConfig.materialMetallic = config.materialMetallic;
    sysConfig.castsShadow = true;

    // No transform modifier needed for detritus - rotation already makes branches lie flat
    return ScatterSystem::create(info, sysConfig, std::move(meshes), std::move(instances), nullptr);
}

} // namespace ScatterSystemFactory
