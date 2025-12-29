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

    SDL_Log("SettlementSystem: Initialized with %zu settlements, %zu streets, %zu lots, %zu buildings",
            settlements_.size(), streets_.size(), lots_.size(), buildingInstances_.size());

    return true;
}

void SettlementSystem::cleanup() {
    if (storedDevice_ == VK_NULL_HANDLE) return;

    // RAII-managed textures
    buildingTexture_.reset();

    // Manually managed mesh
    buildingMesh_.destroy(storedAllocator_);

    settlements_.clear();
    streets_.clear();
    lots_.clear();
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

int SettlementSystem::getLotCount(SettlementType type) const {
    switch (type) {
        case SettlementType::Hamlet:         return config_.lotsPerHamlet;
        case SettlementType::Village:        return config_.lotsPerVillage;
        case SettlementType::Town:           return config_.lotsPerTown;
        case SettlementType::FishingVillage: return config_.lotsPerFishingVillage;
        default:                             return config_.lotsPerHamlet;
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
        streets_.clear();
        lots_.clear();
        buildingInstances_.clear();

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

            // Generate entry points if not provided (simplified: 4 cardinal directions)
            // In a full implementation, these would come from the road network
            if (data.entryPoints.empty()) {
                float radius = config_.settlementRadius * 0.8f;
                data.entryPoints.push_back(data.position + glm::vec2(radius, 0.0f));   // East
                data.entryPoints.push_back(data.position + glm::vec2(-radius, 0.0f));  // West
                data.entryPoints.push_back(data.position + glm::vec2(0.0f, radius));   // South
                data.entryPoints.push_back(data.position + glm::vec2(0.0f, -radius));  // North
            }

            settlements_.push_back(data);
        }

        SDL_Log("SettlementSystem: Loaded %zu settlements from %s",
                settlements_.size(), jsonPath.c_str());

        // Generate layout for each settlement using M2.5 approach
        for (const auto& settlement : settlements_) {
            generateSettlementLayout(settlement);
        }

        createSceneObjects();

        return true;

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SettlementSystem: Failed to parse settlements JSON: %s", e.what());
        return false;
    }
}

// M2.5 Layout Generation Pipeline
void SettlementSystem::generateSettlementLayout(const SettlementData& settlement) {
    // Step 1: Generate street network
    generateStreetNetwork(settlement);

    // Step 2 & 3: Subdivide street frontage into lots and place buildings
    // Done in generateStreetNetwork -> subdivideFrontageIntoLots -> placeBuildingOnLot
}

void SettlementSystem::generateStreetNetwork(const SettlementData& settlement) {
    // Simplified space colonization: create main streets from center to entry points
    // A full implementation would use iterative growth toward attractors

    float radius = config_.settlementRadius;
    switch (settlement.type) {
        case SettlementType::Hamlet:         radius *= 0.4f; break;
        case SettlementType::Village:        radius *= 0.7f; break;
        case SettlementType::Town:           radius *= 1.2f; break;
        case SettlementType::FishingVillage: radius *= 0.5f; break;
    }

    int targetLots = getLotCount(settlement.type);
    uint32_t streetId = static_cast<uint32_t>(streets_.size());

    // Create main street through the settlement center
    // Direction based on hash for variety
    float mainAngle = hashPosition(settlement.position.x, settlement.position.y, 11111) * 3.14159f;
    glm::vec2 mainDir(std::cos(mainAngle), std::sin(mainAngle));

    // Main street extends from one side of settlement to the other
    StreetSegment mainStreet;
    mainStreet.start = settlement.position - mainDir * radius;
    mainStreet.end = settlement.position + mainDir * radius;
    mainStreet.width = config_.mainStreetWidth;
    mainStreet.settlementId = settlement.id;
    streets_.push_back(mainStreet);

    // Subdivide main street into lots on both sides
    subdivideFrontageIntoLots(mainStreet, true);   // Left side
    subdivideFrontageIntoLots(mainStreet, false);  // Right side

    // For larger settlements, add cross streets
    if (settlement.type == SettlementType::Village || settlement.type == SettlementType::Town) {
        glm::vec2 crossDir = mainStreet.getNormal();
        float crossLength = radius * 0.6f;

        // Cross street at settlement center
        StreetSegment crossStreet;
        crossStreet.start = settlement.position - crossDir * crossLength;
        crossStreet.end = settlement.position + crossDir * crossLength;
        crossStreet.width = config_.mainStreetWidth * 0.8f;
        crossStreet.settlementId = settlement.id;
        streets_.push_back(crossStreet);

        subdivideFrontageIntoLots(crossStreet, true);
        subdivideFrontageIntoLots(crossStreet, false);
    }

    // For towns, add additional parallel streets
    if (settlement.type == SettlementType::Town) {
        glm::vec2 perpDir = mainStreet.getNormal();
        float offset = config_.streetSpacing;

        // Parallel street on left
        StreetSegment parallelLeft;
        parallelLeft.start = mainStreet.start + perpDir * offset;
        parallelLeft.end = mainStreet.end + perpDir * offset;
        parallelLeft.width = config_.backLaneWidth;
        parallelLeft.settlementId = settlement.id;
        streets_.push_back(parallelLeft);

        subdivideFrontageIntoLots(parallelLeft, true);
        subdivideFrontageIntoLots(parallelLeft, false);

        // Parallel street on right
        StreetSegment parallelRight;
        parallelRight.start = mainStreet.start - perpDir * offset;
        parallelRight.end = mainStreet.end - perpDir * offset;
        parallelRight.width = config_.backLaneWidth;
        parallelRight.settlementId = settlement.id;
        streets_.push_back(parallelRight);

        subdivideFrontageIntoLots(parallelRight, true);
        subdivideFrontageIntoLots(parallelRight, false);
    }

    SDL_Log("SettlementSystem: Generated %zu streets for settlement %u (%s)",
            streets_.size() - streetId,
            settlement.id,
            settlement.type == SettlementType::Town ? "town" :
            settlement.type == SettlementType::Village ? "village" :
            settlement.type == SettlementType::FishingVillage ? "fishing_village" : "hamlet");
}

void SettlementSystem::subdivideFrontageIntoLots(const StreetSegment& street, bool leftSide) {
    // Subdivide the street frontage into medieval burgage plots
    // Perpendicular to street, 5-10m wide, 30-60m deep

    float streetLength = street.getLength();
    if (streetLength < config_.minLotWidth) return;

    glm::vec2 streetDir = street.getDirection();
    glm::vec2 streetNormal = street.getNormal();

    // Depth direction points away from street into the lot
    glm::vec2 depthDir = leftSide ? streetNormal : -streetNormal;

    // Start from the beginning of the street with a small offset
    float offset = street.width * 0.5f;
    float currentPos = config_.minLotWidth * 0.5f;

    uint32_t streetSegmentId = static_cast<uint32_t>(&street - streets_.data());

    while (currentPos < streetLength - config_.minLotWidth * 0.5f) {
        // Determine lot width (random within range)
        float t = hashPosition(currentPos, street.start.x + street.start.y, 22222 + streetSegmentId);
        float lotWidth = config_.minLotWidth + t * (config_.maxLotWidth - config_.minLotWidth);

        // Make sure we don't exceed street length
        if (currentPos + lotWidth * 0.5f > streetLength) break;

        // Determine lot depth
        float t2 = hashPosition(currentPos, street.end.x + street.end.y, 33333 + streetSegmentId);
        float lotDepth = config_.minLotDepth + t2 * (config_.maxLotDepth - config_.minLotDepth);

        // Calculate frontage center position
        glm::vec2 frontageCenter = street.start + streetDir * currentPos;
        // Offset from street centerline by half street width + small setback
        frontageCenter += depthDir * (offset + 1.0f);

        BuildingLot lot;
        lot.frontageCenter = frontageCenter;
        lot.frontageDir = streetDir;
        lot.depthDir = depthDir;
        lot.frontageWidth = lotWidth;
        lot.depth = lotDepth;
        lot.settlementId = street.settlementId;
        lot.streetSegmentId = streetSegmentId;

        lots_.push_back(lot);

        // Place building on this lot
        placeBuildingOnLot(lot);

        // Move to next lot position
        currentPos += lotWidth;
    }
}

void SettlementSystem::placeBuildingOnLot(const BuildingLot& lot) {
    // Place a building on the lot, aligned to frontage
    // Building sits at front of lot, facing the street

    uint32_t lotId = static_cast<uint32_t>(lots_.size() - 1);

    // Building dimensions constrained by lot
    float t1 = hashPosition(lot.frontageCenter.x, lot.frontageCenter.y, 44444);
    float t2 = hashPosition(lot.frontageCenter.x, lot.frontageCenter.y, 55555);
    float t3 = hashPosition(lot.frontageCenter.x, lot.frontageCenter.y, 66666);

    // Building width <= lot frontage width (with small margin)
    float maxBuildWidth = std::min(lot.frontageWidth - 1.0f, config_.maxBuildingWidth);
    float buildingWidth = config_.minBuildingWidth + t1 * (maxBuildWidth - config_.minBuildingWidth);

    // Building depth << lot depth (building at front, yard at back)
    float maxBuildDepth = std::min(lot.depth * 0.4f, config_.maxBuildingDepth);
    float buildingDepth = config_.minBuildingDepth + t3 * (maxBuildDepth - config_.minBuildingDepth);

    float buildingHeight = config_.minBuildingHeight + t2 * (config_.maxBuildingHeight - config_.minBuildingHeight);

    // Position building at front of lot (small setback from frontage)
    float setback = 1.0f;  // 1m setback from street
    glm::vec2 buildingPos2D = lot.frontageCenter + lot.depthDir * (setback + buildingDepth * 0.5f);

    // Get terrain height
    float y = 0.0f;
    if (getTerrainHeight_) {
        y = getTerrainHeight_(buildingPos2D.x, buildingPos2D.y);
    }

    // Skip if underwater
    if (y < 1.0f) return;

    // Calculate rotation to face the street (perpendicular to depth direction)
    // depthDir points into lot, so building front faces opposite direction
    float rotation = std::atan2(-lot.depthDir.x, -lot.depthDir.y);

    BuildingInstance building;
    building.position = glm::vec3(buildingPos2D.x, y, buildingPos2D.y);
    building.rotation = rotation;
    building.scale = glm::vec3(buildingWidth, buildingHeight, buildingDepth);
    building.meshVariation = 0;
    building.settlementId = lot.settlementId;
    building.lotId = lotId;

    buildingInstances_.push_back(building);
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

    SDL_Log("SettlementSystem: Created %zu scene objects from %zu lots",
            sceneObjects_.size(), lots_.size());
}
