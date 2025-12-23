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
    // Use oak bark texture for fallen branches
    std::string texturePath = info.resourcePath + "/assets/textures/bark/oak_bark.jpg";
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

    std::string normalPath = info.resourcePath + "/assets/textures/bark/oak_bark_norm.jpg";
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
    meshes_.resize(config_.branchVariations);

    for (int i = 0; i < config_.branchVariations; ++i) {
        // Create varied branch configurations
        BranchConfig branchConfig;
        branchConfig.seed = 98765 + i * 1337;

        // Vary length and radius
        float t = hashPosition(float(i), 0.0f, branchConfig.seed);
        branchConfig.length = config_.minLength + t * (config_.maxLength - config_.minLength);

        float r = hashPosition(float(i), 1.0f, branchConfig.seed + 100);
        branchConfig.radius = config_.minRadius + r * (config_.maxRadius - config_.minRadius);

        // More sections for longer branches
        branchConfig.sectionCount = 4 + static_cast<int>(branchConfig.length * 3);
        branchConfig.segmentCount = 5;

        // Varied appearance
        branchConfig.taper = 0.6f + hashPosition(float(i), 2.0f, branchConfig.seed + 200) * 0.3f;
        branchConfig.gnarliness = 0.1f + hashPosition(float(i), 3.0f, branchConfig.seed + 300) * 0.2f;
        branchConfig.twist = (hashPosition(float(i), 4.0f, branchConfig.seed + 400) - 0.5f) * 0.1f;

        // Break point configuration
        if (hashPosition(float(i), 5.0f, branchConfig.seed + 500) < config_.breakChance) {
            branchConfig.hasBreak = true;
            branchConfig.breakPoint = 0.5f + hashPosition(float(i), 6.0f, branchConfig.seed + 600) * 0.4f;
        }

        // Child branches for larger pieces
        if (branchConfig.length > 1.5f && hashPosition(float(i), 7.0f, branchConfig.seed + 700) > 0.4f) {
            branchConfig.childCount = 1 + static_cast<int>(hashPosition(float(i), 8.0f, branchConfig.seed + 800) * config_.maxChildren);
            branchConfig.childStart = 0.2f + hashPosition(float(i), 9.0f, branchConfig.seed + 900) * 0.4f;
            branchConfig.childAngle = 30.0f + hashPosition(float(i), 10.0f, branchConfig.seed + 1000) * 40.0f;
            branchConfig.childLengthRatio = 0.3f + hashPosition(float(i), 11.0f, branchConfig.seed + 1100) * 0.3f;
            branchConfig.childRadiusRatio = 0.4f + hashPosition(float(i), 12.0f, branchConfig.seed + 1200) * 0.3f;
        }

        // Generate the branch
        GeneratedBranch branchData = generator_.generate(branchConfig);

        if (!generateMeshFromBranches(branchData, meshes_[i], info)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DetritusSystem: Failed to generate mesh %d", i);
            return false;
        }
    }

    return true;
}

bool DetritusSystem::generateMeshFromBranches(const GeneratedBranch& branchData, Mesh& outMesh,
                                               const InitInfo& info) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Texture scale for bark
    glm::vec2 textureScale(1.0f, 0.25f);
    float vRepeat = 1.0f / textureScale.y;

    uint32_t indexOffset = 0;
    for (const auto& branch : branchData.branches) {
        int sectionCount = branch.sectionCount;
        int segmentCount = branch.segmentCount;

        for (size_t sectionIdx = 0; sectionIdx < branch.sections.size(); ++sectionIdx) {
            const SectionData& section = branch.sections[sectionIdx];

            // V coordinate alternates for tiling
            float vCoord = (sectionIdx % 2 == 0) ? 0.0f : vRepeat;

            for (int seg = 0; seg <= segmentCount; ++seg) {
                float angle = 2.0f * glm::pi<float>() * static_cast<float>(seg) / static_cast<float>(segmentCount);

                // Local position on unit circle
                glm::vec3 localPos(std::cos(angle), 0.0f, std::sin(angle));
                glm::vec3 localNormal = localPos;  // Outward facing normal

                // Transform by section orientation
                glm::vec3 worldOffset = section.orientation * (localPos * section.radius);
                glm::vec3 worldNormal = glm::normalize(section.orientation * localNormal);

                float uCoord = static_cast<float>(seg) / static_cast<float>(segmentCount) * textureScale.x;

                Vertex v{};
                v.position = section.origin + worldOffset;
                v.normal = worldNormal;
                v.texCoord = glm::vec2(uCoord, vCoord);
                v.tangent = glm::vec4(
                    glm::normalize(section.orientation * glm::vec3(0.0f, 1.0f, 0.0f)),
                    1.0f
                );
                // No wind animation for fallen branches
                v.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

                vertices.push_back(v);
            }
        }

        // Generate indices for this branch
        uint32_t vertsPerRing = static_cast<uint32_t>(segmentCount + 1);
        for (int section = 0; section < sectionCount; ++section) {
            for (int seg = 0; seg < segmentCount; ++seg) {
                uint32_t v0 = indexOffset + section * vertsPerRing + seg;
                uint32_t v1 = v0 + 1;
                uint32_t v2 = v0 + vertsPerRing;
                uint32_t v3 = v2 + 1;

                indices.push_back(v0);
                indices.push_back(v2);
                indices.push_back(v1);

                indices.push_back(v1);
                indices.push_back(v2);
                indices.push_back(v3);
            }
        }

        indexOffset += static_cast<uint32_t>(branch.sections.size()) * vertsPerRing;
    }

    if (vertices.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DetritusSystem: Generated empty mesh");
        return false;
    }

    outMesh.setCustomGeometry(vertices, indices);
    if (!outMesh.upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DetritusSystem: Failed to upload mesh");
        return false;
    }

    return true;
}

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

    const int totalPieces = config_.branchVariations * config_.branchesPerVariation;
    const float minDist = config_.minDistanceBetween;
    const float minDistSq = minDist * minDist;

    // Golden angle for spiral distribution
    const float goldenAngle = 2.39996323f;

    int placed = 0;
    int attempts = 0;
    const int maxAttempts = totalPieces * 30;

    while (placed < totalPieces && attempts < maxAttempts) {
        attempts++;

        float x, z;

        if (attempts % 3 == 0) {
            // Spiral distribution
            float radius = config_.placementRadius * std::sqrt(float(placed + 1) / float(totalPieces + 1));
            float angle = placed * goldenAngle;
            x = radius * std::cos(angle);
            z = radius * std::sin(angle);
        } else {
            // Random with hash
            float angle = hashPosition(float(attempts), 0.0f, 54321) * 2.0f * 3.14159f;
            float radius = std::sqrt(hashPosition(float(attempts), 1.0f, 54322)) * config_.placementRadius;
            x = radius * std::cos(angle);
            z = radius * std::sin(angle);
        }

        // Add jitter
        x += (hashPosition(x, z, 11111) - 0.5f) * minDist * 0.5f;
        z += (hashPosition(x, z, 22222) - 0.5f) * minDist * 0.5f;

        // Check bounds
        float halfTerrain = info.terrainSize * 0.48f;
        if (std::abs(x) > halfTerrain || std::abs(z) > halfTerrain) {
            continue;
        }

        // Check distance from existing pieces
        bool tooClose = false;
        for (const auto& existing : instances_) {
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

        // Get terrain height
        float y = 0.0f;
        if (info.getTerrainHeight) {
            y = info.getTerrainHeight(x, z);
        }

        // Skip very low areas (water level) or very steep slopes
        if (y < 1.0f) {
            continue;
        }

        // Create detritus instance
        DetritusInstance instance;
        instance.position = glm::vec3(x, y, z);

        // Rotation: fallen branches lie on the ground with random orientations
        float yaw = hashPosition(x, z, 33333) * 2.0f * 3.14159f;  // Random horizontal rotation

        // Slight random pitch and roll for natural look
        float pitch = (hashPosition(x, z, 44444) - 0.5f) * 0.3f;  // Small tilt
        float roll = glm::half_pi<float>() - 0.1f + hashPosition(x, z, 55555) * 0.2f;  // Mostly lying down

        instance.rotation = glm::vec3(pitch, yaw, roll);

        // Random scale
        float t = hashPosition(x, z, 66666);
        instance.scale = 0.7f + t * 0.6f;

        // Assign mesh variation
        instance.meshVariation = placed % config_.branchVariations;

        instances_.push_back(instance);
        placed++;
    }

    SDL_Log("DetritusSystem: Placed %d pieces in %d attempts", placed, attempts);
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
