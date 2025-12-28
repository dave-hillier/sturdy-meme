#include "SettlementSystem.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>

std::unique_ptr<SettlementSystem> SettlementSystem::create(const InitInfo& info, const SettlementConfig& config) {
    std::unique_ptr<SettlementSystem> system(new SettlementSystem());
    if (!system->initInternal(info, config)) {
        return nullptr;
    }
    return system;
}

SettlementSystem::~SettlementSystem() {
    cleanup();
}

bool SettlementSystem::initInternal(const InitInfo& info, const SettlementConfig& cfg) {
    config_ = cfg;
    storedAllocator_ = info.allocator;
    storedDevice_ = info.device;
    getTerrainHeight_ = info.getTerrainHeight;

    if (!loadTextures(info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SettlementSystem: Failed to load textures");
        return false;
    }

    if (!createBuildingMeshes(info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SettlementSystem: Failed to create building meshes");
        return false;
    }

    // Try to load settlements from default location
    std::string settlementsPath = info.resourcePath + "/assets/terrain/settlements.json";
    if (!loadSettlements(settlementsPath)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SettlementSystem: No settlements.json found at %s, system will be empty until settlements are loaded",
            settlementsPath.c_str());
    }

    SDL_Log("SettlementSystem: Initialized with %zu settlements, %zu buildings",
            settlements_.size(), buildingInstances_.size());

    return true;
}

void SettlementSystem::cleanup() {
    if (storedDevice_ == VK_NULL_HANDLE) return;

    // RAII-managed textures
    buildingTexture_.reset();

    // Manually managed mesh
    buildingMesh_.destroy(storedAllocator_);

    settlements_.clear();
    buildingInstances_.clear();
    sceneObjects_.clear();
}

bool SettlementSystem::loadTextures(const InitInfo& info) {
    // Use a simple stone/plaster texture for buildings
    std::string texturePath = info.resourcePath + "/assets/textures/industrial/concrete_1.jpg";
    buildingTexture_ = RAIIAdapter<Texture>::create(
        [&](auto& t) {
            if (!t.load(texturePath, info.allocator, info.device, info.commandPool,
                        info.graphicsQueue, info.physicalDevice)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "SettlementSystem: Failed to load building texture: %s", texturePath.c_str());
                return false;
            }
            return true;
        },
        [this](auto& t) { t.destroy(storedAllocator_, storedDevice_); }
    );
    if (!buildingTexture_) return false;

    return true;
}

bool SettlementSystem::createBuildingMeshes(const InitInfo& info) {
    // Create a simple unit cube that will be scaled per-instance
    buildingMesh_.createCube();
    buildingMesh_.upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);
    return true;
}

float SettlementSystem::hashPosition(float x, float z, uint32_t seed) const {
    // Simple hash function for deterministic pseudo-random values
    uint32_t ix = *reinterpret_cast<uint32_t*>(&x);
    uint32_t iz = *reinterpret_cast<uint32_t*>(&z);
    uint32_t n = ix ^ (iz * 1597334673U) ^ seed;
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    return float(n & 0x7fffffffU) / float(0x7fffffff);
}

int SettlementSystem::getBuildingCount(SettlementType type) const {
    switch (type) {
        case SettlementType::Hamlet:         return config_.buildingsPerHamlet;
        case SettlementType::Village:        return config_.buildingsPerVillage;
        case SettlementType::Town:           return config_.buildingsPerTown;
        case SettlementType::FishingVillage: return config_.buildingsPerFishingVillage;
        default:                             return config_.buildingsPerHamlet;
    }
}

bool SettlementSystem::loadSettlements(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SettlementSystem: Could not open settlements file: %s", jsonPath.c_str());
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        settlements_.clear();

        for (const auto& settlement : j["settlements"]) {
            SettlementData data;
            data.id = settlement["id"].get<uint32_t>();

            std::string typeStr = settlement["type"].get<std::string>();
            if (typeStr == "hamlet") data.type = SettlementType::Hamlet;
            else if (typeStr == "village") data.type = SettlementType::Village;
            else if (typeStr == "town") data.type = SettlementType::Town;
            else if (typeStr == "fishing_village") data.type = SettlementType::FishingVillage;
            else data.type = SettlementType::Hamlet;

            data.position.x = settlement["position"]["x"].get<float>();
            data.position.y = settlement["position"]["y"].get<float>();
            data.score = settlement.value("score", 0.0f);

            if (settlement.contains("features")) {
                for (const auto& feature : settlement["features"]) {
                    data.features.push_back(feature.get<std::string>());
                }
            }

            settlements_.push_back(data);
        }

        SDL_Log("SettlementSystem: Loaded %zu settlements from %s",
                settlements_.size(), jsonPath.c_str());

        // Generate building placements for loaded settlements
        generateBuildingPlacements(InitInfo{
            storedDevice_, storedAllocator_, VK_NULL_HANDLE, VK_NULL_HANDLE,
            VK_NULL_HANDLE, "", getTerrainHeight_, 0.0f
        });
        createSceneObjects();

        return true;

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SettlementSystem: Failed to parse settlements JSON: %s", e.what());
        return false;
    }
}

void SettlementSystem::generateBuildingPlacements(const InitInfo& info) {
    buildingInstances_.clear();

    const float minDistSq = config_.buildingSpacing * config_.buildingSpacing;

    for (const auto& settlement : settlements_) {
        int numBuildings = getBuildingCount(settlement.type);
        float radius = config_.settlementRadius;

        // Scale radius based on settlement type
        switch (settlement.type) {
            case SettlementType::Hamlet:         radius *= 0.5f; break;
            case SettlementType::Village:        radius *= 0.8f; break;
            case SettlementType::Town:           radius *= 1.5f; break;
            case SettlementType::FishingVillage: radius *= 0.6f; break;
        }

        int placed = 0;
        int attempts = 0;
        const int maxAttempts = numBuildings * 50;

        while (placed < numBuildings && attempts < maxAttempts) {
            attempts++;

            // Generate candidate position within settlement radius
            float angle = hashPosition(float(attempts), float(settlement.id), 12345) * 2.0f * 3.14159f;
            float dist = std::sqrt(hashPosition(float(attempts), float(settlement.id), 54321)) * radius;

            float x = settlement.position.x + dist * std::cos(angle);
            float z = settlement.position.y + dist * std::sin(angle);

            // Check distance from existing buildings in this settlement
            bool tooClose = false;
            for (const auto& existing : buildingInstances_) {
                if (existing.settlementId != settlement.id) continue;
                float dx = x - existing.position.x;
                float dz = z - existing.position.z;
                if (dx * dx + dz * dz < minDistSq) {
                    tooClose = true;
                    break;
                }
            }

            if (tooClose) continue;

            // Get terrain height
            float y = 0.0f;
            if (getTerrainHeight_) {
                y = getTerrainHeight_(x, z);
            }

            // Skip if underwater
            if (y < 1.0f) continue;

            // Create building instance
            BuildingInstance building;
            building.position = glm::vec3(x, y, z);
            building.rotation = hashPosition(x, z, 33333) * 2.0f * 3.14159f;
            building.settlementId = settlement.id;
            building.meshVariation = 0;  // Single mesh for now

            // Random building dimensions
            float t1 = hashPosition(x, z, 44444);
            float t2 = hashPosition(x, z, 55555);
            float t3 = hashPosition(x, z, 66666);
            building.scale.x = config_.minBuildingWidth + t1 * (config_.maxBuildingWidth - config_.minBuildingWidth);
            building.scale.y = config_.minBuildingHeight + t2 * (config_.maxBuildingHeight - config_.minBuildingHeight);
            building.scale.z = config_.minBuildingDepth + t3 * (config_.maxBuildingDepth - config_.minBuildingDepth);

            // Make town buildings taller on average
            if (settlement.type == SettlementType::Town) {
                building.scale.y *= 1.3f;
            }

            buildingInstances_.push_back(building);
            placed++;
        }

        SDL_Log("SettlementSystem: Placed %d/%d buildings for %s (id=%u)",
                placed, numBuildings,
                settlement.type == SettlementType::Town ? "town" :
                settlement.type == SettlementType::Village ? "village" :
                settlement.type == SettlementType::FishingVillage ? "fishing_village" : "hamlet",
                settlement.id);
    }
}

void SettlementSystem::createSceneObjects() {
    sceneObjects_.clear();
    sceneObjects_.reserve(buildingInstances_.size());

    for (const auto& building : buildingInstances_) {
        // Build transform matrix: translate, rotate, scale
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), building.position);
        transform = glm::rotate(transform, building.rotation, glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::scale(transform, building.scale);

        // The cube mesh is centered at origin with size 1, so we need to offset it up
        // so the bottom of the building sits on the terrain
        transform = glm::translate(transform, glm::vec3(0.0f, 0.5f, 0.0f));

        sceneObjects_.push_back(RenderableBuilder()
            .withTransform(transform)
            .withMesh(&buildingMesh_)
            .withTexture(&**buildingTexture_)
            .withRoughness(config_.materialRoughness)
            .withMetallic(config_.materialMetallic)
            .withCastsShadow(true)
            .build());
    }
}
