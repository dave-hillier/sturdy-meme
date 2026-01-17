// VegetationContentGenerator.cpp - Vegetation content generation

#include "VegetationContentGenerator.h"
#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "TreeRenderer.h"
#include "ThreadedTreeGenerator.h"
#include "TreeOptions.h"
#include "DetritusSystem.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <cmath>

VegetationContentGenerator::VegetationContentGenerator(const Config& config)
    : config_(config)
    , presetDir_(config.resourcePath + "/assets/trees/presets/")
{
}

VegetationContentGenerator::~VegetationContentGenerator() = default;

TreeOptions VegetationContentGenerator::loadPresetOrDefault(
    const std::string& presetName,
    TreeOptions (*defaultFn)()
) const {
    std::string path = presetDir_ + presetName;
    if (std::filesystem::exists(path)) {
        return TreeOptions::loadFromJson(path);
    }
    return defaultFn();
}

void VegetationContentGenerator::generateDemoTrees(
    TreeSystem& treeSystem,
    const glm::vec2& sceneOrigin
) {
    const float demoTreeX = sceneOrigin.x;
    const float demoTreeZ = sceneOrigin.y;

    // Oak tree
    float oakX = demoTreeX + 35.0f, oakZ = demoTreeZ + 25.0f;
    glm::vec3 oakPos(oakX, config_.getTerrainHeight(oakX, oakZ), oakZ);
    treeSystem.addTree(oakPos, 0.0f, 1.0f, loadPresetOrDefault("oak_large.json", TreeOptions::defaultOak));

    // Pine tree
    float pineX = demoTreeX + 50.0f, pineZ = demoTreeZ - 30.0f;
    glm::vec3 pinePos(pineX, config_.getTerrainHeight(pineX, pineZ), pineZ);
    treeSystem.addTree(pinePos, 0.5f, 1.0f, loadPresetOrDefault("pine_large.json", TreeOptions::defaultPine));

    // Ash tree
    float ashX = demoTreeX - 40.0f, ashZ = demoTreeZ - 25.0f;
    glm::vec3 ashPos(ashX, config_.getTerrainHeight(ashX, ashZ), ashZ);
    treeSystem.addTree(ashPos, 1.0f, 1.0f, loadPresetOrDefault("ash_large.json", TreeOptions::defaultOak));

    // Aspen tree
    float aspenX = demoTreeX + 30.0f, aspenZ = demoTreeZ + 40.0f;
    glm::vec3 aspenPos(aspenX, config_.getTerrainHeight(aspenX, aspenZ), aspenZ);
    treeSystem.addTree(aspenPos, 1.5f, 1.0f, loadPresetOrDefault("aspen_large.json", TreeOptions::defaultOak));

    SDL_Log("VegetationContentGenerator: Added 4 demo trees");
}

int VegetationContentGenerator::generateForest(
    TreeSystem& treeSystem,
    const glm::vec2& center,
    float radius,
    int maxTrees,
    uint32_t seed
) {
    // Tree presets for forest
    std::vector<std::pair<std::string, TreeOptions(*)()>> treePresets = {
        {"oak_medium.json", TreeOptions::defaultOak},
        {"pine_medium.json", TreeOptions::defaultPine},
        {"ash_medium.json", TreeOptions::defaultOak},
        {"aspen_medium.json", TreeOptions::defaultOak}
    };

    // Poisson disk sampling parameters
    const float minDist = 8.0f;
    const int maxAttempts = 30;

    // Simple LCG for deterministic results
    auto nextRand = [&seed]() -> float {
        seed = seed * 1103515245 + 12345;
        return static_cast<float>((seed >> 16) & 0x7FFF) / 32767.0f;
    };

    // Poisson disk sampling
    std::vector<glm::vec2> placedTrees;
    placedTrees.reserve(maxTrees);
    placedTrees.push_back(center);

    std::vector<size_t> activeList;
    activeList.push_back(0);

    while (!activeList.empty() && static_cast<int>(placedTrees.size()) < maxTrees) {
        size_t activeIdx = static_cast<size_t>(nextRand() * activeList.size());
        if (activeIdx >= activeList.size()) activeIdx = activeList.size() - 1;
        glm::vec2 activePoint = placedTrees[activeList[activeIdx]];

        bool foundValid = false;
        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            float angle = nextRand() * 2.0f * 3.14159265f;
            float dist = minDist + nextRand() * minDist;
            glm::vec2 newPoint = activePoint + glm::vec2(std::cos(angle), std::sin(angle)) * dist;

            float distFromCenter = glm::length(newPoint - center);
            if (distFromCenter > radius) continue;

            bool tooClose = false;
            for (const auto& p : placedTrees) {
                if (glm::length(newPoint - p) < minDist) {
                    tooClose = true;
                    break;
                }
            }

            if (!tooClose) {
                placedTrees.push_back(newPoint);
                activeList.push_back(placedTrees.size() - 1);
                foundValid = true;
                break;
            }
        }

        if (!foundValid) {
            activeList.erase(activeList.begin() + activeIdx);
        }
    }

    // Use threaded generation for large forests
    auto threadedGen = ThreadedTreeGenerator::create(4);
    int treesPlaced = 0;

    if (threadedGen) {
        std::vector<ThreadedTreeGenerator::TreeRequest> requests;
        requests.reserve(placedTrees.size());

        for (size_t i = 0; i < placedTrees.size() && treesPlaced < maxTrees; i++) {
            float x = placedTrees[i].x;
            float z = placedTrees[i].y;
            float y = config_.getTerrainHeight(x, z);

            float rotation = nextRand() * 2.0f * 3.14159265f;
            float scale = 0.7f + 0.6f * nextRand();

            size_t presetIdx = static_cast<size_t>(nextRand() * treePresets.size());
            if (presetIdx >= treePresets.size()) presetIdx = treePresets.size() - 1;
            auto opts = loadPresetOrDefault(treePresets[presetIdx].first, treePresets[presetIdx].second);

            // Determine archetype index
            uint32_t archetypeIndex = 0;
            const std::string& leafType = opts.leaves.type;
            if (leafType == "oak") archetypeIndex = 0;
            else if (leafType == "pine") archetypeIndex = 1;
            else if (leafType == "ash") archetypeIndex = 2;
            else if (leafType == "aspen") archetypeIndex = 3;

            ThreadedTreeGenerator::TreeRequest req;
            req.position = glm::vec3(x, y, z);
            req.rotation = rotation;
            req.scale = scale;
            req.options = opts;
            req.archetypeIndex = archetypeIndex;
            requests.push_back(req);
            treesPlaced++;
        }

        threadedGen->queueTrees(requests);
        SDL_Log("VegetationContentGenerator: Queued %d trees for parallel generation", treesPlaced);

        threadedGen->waitForAll();

        auto stagedTrees = threadedGen->getCompletedTrees();
        int uploadedCount = 0;
        for (auto& staged : stagedTrees) {
            uint32_t treeIdx = treeSystem.addTreeFromStagedData(
                staged.position, staged.rotation, staged.scale,
                staged.options,
                staged.branchVertexData, staged.branchVertexCount,
                staged.branchIndices,
                staged.leafInstanceData, staged.leafInstanceCount,
                staged.archetypeIndex);

            if (treeIdx != UINT32_MAX) {
                uploadedCount++;
            }
        }

        treeSystem.finalizeLeafInstanceBuffer();
        SDL_Log("VegetationContentGenerator: Uploaded %d/%zu trees to GPU", uploadedCount, stagedTrees.size());
    } else {
        // Fallback to serial generation
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Threaded tree generator unavailable, using serial");
        for (size_t i = 0; i < placedTrees.size() && treesPlaced < maxTrees; i++) {
            float x = placedTrees[i].x;
            float z = placedTrees[i].y;
            float y = config_.getTerrainHeight(x, z);

            float rotation = nextRand() * 2.0f * 3.14159265f;
            float scale = 0.7f + 0.6f * nextRand();

            size_t presetIdx = static_cast<size_t>(nextRand() * treePresets.size());
            if (presetIdx >= treePresets.size()) presetIdx = treePresets.size() - 1;
            auto opts = loadPresetOrDefault(treePresets[presetIdx].first, treePresets[presetIdx].second);

            treeSystem.addTree(glm::vec3(x, y, z), rotation, scale, opts);
            treesPlaced++;
        }
    }

    SDL_Log("VegetationContentGenerator: Generated forest with %d trees", treesPlaced);
    return treesPlaced;
}

void VegetationContentGenerator::generateImpostorArchetypes(
    TreeSystem& treeSystem,
    TreeLODSystem& treeLOD
) {
    struct ArchetypeInfo {
        uint32_t meshIndex;
        std::string name;
        std::string bark;
        std::string leaves;
    };

    std::vector<ArchetypeInfo> archetypeInfos = {
        {0, "oak",   "oak",   "oak"},
        {1, "pine",  "pine",  "pine"},
        {2, "ash",   "oak",   "ash"},
        {3, "aspen", "birch", "aspen"}
    };

    for (const auto& info : archetypeInfos) {
        if (info.meshIndex >= treeSystem.getMeshCount()) continue;

        const auto& branchMesh = treeSystem.getBranchMesh(info.meshIndex);
        const auto& leafInstances = treeSystem.getLeafInstances(info.meshIndex);
        const auto& treeOpts = treeSystem.getTreeOptions(info.meshIndex);

        auto* barkTex = treeSystem.getBarkTexture(info.bark);
        auto* barkNorm = treeSystem.getBarkNormalMap(info.bark);
        auto* leafTex = treeSystem.getLeafTexture(info.leaves);

        if (barkTex && barkNorm && leafTex) {
            int32_t archetypeIdx = treeLOD.generateImpostor(
                info.name,
                treeOpts,
                branchMesh,
                leafInstances,
                barkTex->getImageView(),
                barkNorm->getImageView(),
                leafTex->getImageView(),
                barkTex->getSampler()
            );
            if (archetypeIdx >= 0) {
                SDL_Log("VegetationContentGenerator: Generated impostor archetype %d: %s", archetypeIdx, info.name.c_str());
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to generate %s impostor", info.name.c_str());
            }
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Missing textures for %s impostor", info.name.c_str());
        }
    }
}

void VegetationContentGenerator::finalizeTreeSystems(
    TreeSystem& treeSystem,
    TreeLODSystem* treeLOD,
    ImpostorCullSystem* impostorCull,
    TreeRenderer* treeRenderer,
    const std::vector<VkBuffer>& uniformBuffers,
    VkImageView shadowView,
    VkSampler shadowSampler
) {
    // Update ImpostorCullSystem with tree data
    if (impostorCull && treeLOD) {
        impostorCull->updateTreeData(treeSystem, treeLOD->getImpostorAtlas());
        impostorCull->updateArchetypeData(treeLOD->getImpostorAtlas());
        impostorCull->initializeDescriptorSets();
        SDL_Log("VegetationContentGenerator: ImpostorCullSystem updated with %u trees", impostorCull->getTreeCount());
    }

    // Update TreeRenderer spatial index
    if (treeRenderer) {
        treeRenderer->updateSpatialIndex(treeSystem);
    }

    // Initialize TreeLODSystem descriptor sets
    if (treeLOD) {
        treeLOD->initializeDescriptorSets(uniformBuffers, shadowView, shadowSampler);

        if (impostorCull) {
            treeLOD->initializeGPUCulledDescriptors(impostorCull->getVisibleImpostorBuffer());
        }
    }
}

std::unique_ptr<DetritusSystem> VegetationContentGenerator::createDetritusSystem(
    const DetritusCreateInfo& info,
    const TreeSystem& treeSystem
) {
    DetritusSystem::InitInfo detritusInfo{};
    detritusInfo.device = info.device;
    detritusInfo.allocator = info.allocator;
    detritusInfo.commandPool = info.commandPool;
    detritusInfo.graphicsQueue = info.graphicsQueue;
    detritusInfo.physicalDevice = info.physicalDevice;
    detritusInfo.resourcePath = config_.resourcePath;
    detritusInfo.terrainSize = config_.terrainSize;
    detritusInfo.getTerrainHeight = config_.getTerrainHeight;

    // Gather tree positions for scattering detritus nearby
    const auto& treeInstances = treeSystem.getTreeInstances();
    detritusInfo.treePositions.reserve(treeInstances.size());
    for (const auto& tree : treeInstances) {
        detritusInfo.treePositions.push_back(tree.position());
    }

    DetritusConfig detritusConfig{};
    detritusConfig.branchVariations = 8;
    detritusConfig.branchesPerVariation = 4;
    detritusConfig.minLength = 0.5f;
    detritusConfig.maxLength = 2.5f;
    detritusConfig.minRadius = 0.03f;
    detritusConfig.maxRadius = 0.12f;
    detritusConfig.placementRadius = 8.0f;
    detritusConfig.minDistanceBetween = 1.5f;
    detritusConfig.breakChance = 0.7f;
    detritusConfig.maxChildren = 3;
    detritusConfig.materialRoughness = 0.85f;
    detritusConfig.materialMetallic = 0.0f;

    auto detritusSystem = DetritusSystem::create(detritusInfo, detritusConfig);
    if (detritusSystem) {
        SDL_Log("VegetationContentGenerator: Created detritus with %zu branches near %zu trees",
                detritusSystem->getDetritusCount(), detritusInfo.treePositions.size());
    }
    return detritusSystem;
}

DetritusConfig VegetationContentGenerator::getDetritusConfig() const {
    DetritusConfig config{};
    config.branchVariations = 8;
    config.branchesPerVariation = 4;
    config.minLength = 0.5f;
    config.maxLength = 2.5f;
    return config;
}

std::vector<glm::vec3> VegetationContentGenerator::getTreePositionsForDetritus(
    const TreeSystem& treeSystem
) const {
    const auto& treeInstances = treeSystem.getTreeInstances();
    std::vector<glm::vec3> positions;
    positions.reserve(treeInstances.size());
    for (const auto& tree : treeInstances) {
        positions.push_back(tree.position());
    }
    return positions;
}
