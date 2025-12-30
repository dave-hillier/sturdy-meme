// RendererInitPhases.cpp - High-level initialization phases for Renderer
// Split from Renderer.cpp to keep file sizes manageable

#include <array>
#include <filesystem>
#include "Renderer.h"
#include "RendererInit.h"
#include "MaterialDescriptorFactory.h"
#include "VulkanResourceFactory.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "SkySystem.h"
#include "GlobalBufferManager.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "GrassSystem.h"
#include "WeatherSystem.h"
#include "SnowMaskSystem.h"
#include "VolumetricSnowSystem.h"
#include "LeafSystem.h"
#include "FroxelSystem.h"
#include "AtmosphereLUTSystem.h"
#include "CloudShadowSystem.h"
#include "HiZSystem.h"
#include "CatmullClarkSystem.h"
#include "RockSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "DetritusSystem.h"
#include "WaterSystem.h"
#include "WaterDisplacement.h"
#include "FlowMapGenerator.h"
#include "FoamBuffer.h"
#include "WaterTileCull.h"
#include "WaterGBuffer.h"
#include "SSRSystem.h"
#include "DebugLineSystem.h"
#include "Profiler.h"
#include "InitProfiler.h"
#include "SkinnedMeshRenderer.h"
#include "SceneManager.h"
#include "ErosionDataLoader.h"
#include "ResizeCoordinator.h"
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "EnvironmentSettings.h"
#include "UBOBuilder.h"
#include "WindSystem.h"
#include <SDL3/SDL.h>

bool Renderer::initCoreVulkanResources() {
    if (!createRenderPass()) return false;
    if (!createDepthResources()) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPool()) return false;
    return true;
}

bool Renderer::initDescriptorInfrastructure() {
    if (!createDescriptorSetLayout()) return false;
    if (!createDescriptorPool()) return false;
    return true;
}

bool Renderer::initSubsystems(const InitContext& initCtx) {
    VkDevice device = vulkanContext_->getDevice();
    VmaAllocator allocator = vulkanContext_->getAllocator();
    VkPhysicalDevice physicalDevice = vulkanContext_->getPhysicalDevice();
    VkQueue graphicsQueue = vulkanContext_->getGraphicsQueue();
    VkFormat swapchainImageFormat = vulkanContext_->getSwapchainImageFormat();

    // Initialize post-processing systems (PostProcessSystem, BloomSystem, BilateralGridSystem)
    {
        INIT_PROFILE_PHASE("PostProcessing");
        auto bundle = PostProcessSystem::createWithDependencies(initCtx, renderPass.get(), swapchainImageFormat);
        if (!bundle) {
            return false;
        }
        systems_->setPostProcess(std::move(bundle->postProcess));
        systems_->setBloom(std::move(bundle->bloom));
        systems_->setBilateralGrid(std::move(bundle->bilateralGrid));
    }

    {
        INIT_PROFILE_PHASE("GraphicsPipeline");
        if (!createGraphicsPipeline()) return false;
    }

    // Initialize skinned mesh rendering (GPU skinning for animated characters)
    {
        INIT_PROFILE_PHASE("SkinnedMeshRenderer");
        if (!initSkinnedMeshRenderer()) return false;
    }

    // Initialize sky system via factory (needs HDR render pass from postProcessSystem)
    {
        INIT_PROFILE_PHASE("SkySystem");
        auto skySystem = SkySystem::create(initCtx, systems_->postProcess().getHDRRenderPass());
        if (!skySystem) return false;
        systems_->setSky(std::move(skySystem));
    }

    if (!createCommandBuffers()) return false;

    // Initialize global buffer manager for all per-frame shared buffers
    {
        INIT_PROFILE_PHASE("GlobalBufferManager");
        auto globalBuffers = GlobalBufferManager::create(allocator, physicalDevice, MAX_FRAMES_IN_FLIGHT);
        if (!globalBuffers) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GlobalBufferManager");
            return false;
        }
        systems_->setGlobalBuffers(std::move(globalBuffers));
    }

    // Initialize light buffers with empty data
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        LightBuffer emptyBuffer{};
        emptyBuffer.lightCount = glm::uvec4(0, 0, 0, 0);
        systems_->globalBuffers().updateLightBuffer(i, emptyBuffer);
    }

    // Initialize shadow system (needs descriptor set layouts for pipeline compatibility)
    {
        INIT_PROFILE_PHASE("ShadowSystem");
        auto shadowSystem = ShadowSystem::create(initCtx, descriptorSetLayout.get(), systems_->skinnedMesh().getDescriptorSetLayout());
        if (!shadowSystem) return false;
        systems_->setShadow(std::move(shadowSystem));
    }

    // Initialize terrain system BEFORE scene so scene objects can query terrain height
    std::string heightmapPath = resourcePath + "/assets/terrain/isleofwight-0m-200m.png";
    std::string terrainDataPath = resourcePath + "/terrain_data";

    // Initialize terrain system with CBT (loads heightmap directly)
    TerrainSystem::TerrainInitParams terrainParams{};
    terrainParams.renderPass = systems_->postProcess().getHDRRenderPass();
    terrainParams.shadowRenderPass = systems_->shadow().getShadowRenderPass();
    terrainParams.shadowMapSize = systems_->shadow().getShadowMapSize();
    terrainParams.texturePath = resourcePath + "/textures";

    TerrainConfig terrainConfig{};
    terrainConfig.size = 16384.0f;
    terrainConfig.maxDepth = 20;
    terrainConfig.minDepth = 5;
    terrainConfig.targetEdgePixels = 16.0f;
    terrainConfig.splitThreshold = 100.0f;
    terrainConfig.mergeThreshold = 50.0f;
    // Load Isle of Wight heightmap (-15m to 200m altitude range, includes beaches below sea level)
    terrainConfig.heightmapPath = resourcePath + "/assets/terrain/isleofwight-0m-200m.png";
    terrainConfig.minAltitude = -15.0f;
    terrainConfig.maxAltitude = 220.0f;
    // heightScale is computed from minAltitude/maxAltitude during init

    // Enable LOD-based tile streaming from preprocessed tile cache
    terrainConfig.tileCacheDir = resourcePath + "/terrain_data";
    terrainConfig.tileLoadRadius = 2000.0f;   // Load high-res tiles within 2km
    terrainConfig.tileUnloadRadius = 3000.0f; // Unload tiles beyond 3km

    // Enable virtual texturing (if tile directory exists)
    terrainConfig.virtualTextureTileDir = resourcePath + "/vt_tiles";
    terrainConfig.useVirtualTexture = true;

    std::unique_ptr<TerrainSystem> terrainSystem;
    {
        INIT_PROFILE_PHASE("TerrainSystem");
        terrainSystem = TerrainSystem::create(initCtx, terrainParams, terrainConfig);
        if (!terrainSystem) return false;
        systems_->setTerrain(std::move(terrainSystem));
    }

    // Collect resources from tier-1 systems for tier-2+ initialization
    // This decouples tier-2 systems from tier-1 systems - they depend on resources, not systems
    CoreResources core = CoreResources::collect(systems_->postProcess(), systems_->shadow(), systems_->terrain(), MAX_FRAMES_IN_FLIGHT);

    // Initialize scene (meshes, textures, objects, lights) via factory
    // Pass terrain height function so objects can be placed on terrain
    SceneBuilder::InitInfo sceneInfo{};
    sceneInfo.allocator = allocator;
    sceneInfo.device = device;
    sceneInfo.commandPool = commandPool.get();
    sceneInfo.graphicsQueue = graphicsQueue;
    sceneInfo.physicalDevice = physicalDevice;
    sceneInfo.resourcePath = resourcePath;
    sceneInfo.getTerrainHeight = [this](float x, float z) {
        return systems_->terrain().getHeightAt(x, z);
    };

    {
        INIT_PROFILE_PHASE("SceneManager");
        auto sceneManager = SceneManager::create(sceneInfo);
        if (!sceneManager) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create SceneManager");
            return false;
        }
        systems_->setScene(std::move(sceneManager));
    }

    // Initialize snow subsystems (SnowMaskSystem, VolumetricSnowSystem)
    {
        auto bundle = SnowMaskSystem::createWithDependencies(initCtx, core.hdr.renderPass);
        if (!bundle) {
            return false;
        }
        systems_->setSnowMask(std::move(bundle->snowMask));
        systems_->setVolumetricSnow(std::move(bundle->volumetricSnow));
    }

    if (!createDescriptorSets()) return false;
    if (!createSkinnedMeshRendererDescriptorSets()) return false;

    // Initialize grass and wind subsystems
    {
        auto bundle = GrassSystem::createWithDependencies(
            initCtx, core.hdr.renderPass, core.shadow.renderPass, core.shadow.mapSize);
        if (!bundle) {
            return false;
        }
        systems_->setWind(std::move(bundle->wind));
        systems_->setGrass(std::move(bundle->grass));
    }

    const EnvironmentSettings* envSettings = &systems_->wind().getEnvironmentSettings();

    // Get wind buffers for grass and other descriptor sets
    std::vector<VkBuffer> windBuffers(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        windBuffers[i] = systems_->wind().getBufferInfo(i).buffer;
    }
    // Note: grass.updateDescriptorSets is called later after CloudShadowSystem is created

    // Update terrain descriptor sets with shared resources
    systems_->terrain().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, systems_->shadow().getShadowImageView(), systems_->shadow().getShadowSampler(),
                                        systems_->globalBuffers().snowBuffers.buffers, systems_->globalBuffers().cloudShadowBuffers.buffers);

    // Initialize rock system via factory
    RockSystem::InitInfo rockInfo{};
    rockInfo.device = device;
    rockInfo.allocator = allocator;
    rockInfo.commandPool = commandPool.get();
    rockInfo.graphicsQueue = graphicsQueue;
    rockInfo.physicalDevice = physicalDevice;
    rockInfo.resourcePath = resourcePath;
    rockInfo.terrainSize = core.terrain.size;
    rockInfo.getTerrainHeight = core.terrain.getHeightAt;

    RockConfig rockConfig{};
    rockConfig.rockVariations = 6;
    rockConfig.rocksPerVariation = 10;
    rockConfig.minRadius = 0.4f;
    rockConfig.maxRadius = 2.0f;
    rockConfig.placementRadius = 100.0f;
    rockConfig.minDistanceBetween = 4.0f;
    rockConfig.roughness = 0.35f;
    rockConfig.asymmetry = 0.3f;
    rockConfig.subdivisions = 3;
    rockConfig.materialRoughness = 0.75f;
    rockConfig.materialMetallic = 0.0f;

    {
        INIT_PROFILE_PHASE("RockSystem");
        auto rockSystem = RockSystem::create(rockInfo, rockConfig);
        if (!rockSystem) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create RockSystem");
            return false;
        }
        systems_->setRock(std::move(rockSystem));
    }

    // Initialize tree system via factory
    TreeSystem::InitInfo treeInfo{};
    treeInfo.device = device;
    treeInfo.allocator = allocator;
    treeInfo.commandPool = commandPool.get();
    treeInfo.graphicsQueue = graphicsQueue;
    treeInfo.physicalDevice = physicalDevice;
    treeInfo.resourcePath = resourcePath;
    treeInfo.terrainSize = core.terrain.size;
    treeInfo.getTerrainHeight = core.terrain.getHeightAt;

    std::unique_ptr<TreeSystem> treeSystem;
    {
        INIT_PROFILE_PHASE("TreeSystem");
        treeSystem = TreeSystem::create(treeInfo);
        if (!treeSystem) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create TreeSystem");
            return false;
        }
    }

    // Add trees to the scene
    std::string presetDir = treeInfo.resourcePath + "/assets/trees/presets/";

    auto loadPresetOrDefault = [&](const std::string& presetName, TreeOptions (*defaultFn)()) {
        std::string path = presetDir + presetName;
        if (std::filesystem::exists(path)) {
            return TreeOptions::loadFromJson(path);
        }
        return defaultFn();
    };

    // Oak tree
    float oakX = 35.0f, oakZ = 25.0f;
    glm::vec3 oakPos(oakX, core.terrain.getHeightAt(oakX, oakZ), oakZ);
    treeSystem->addTree(oakPos, 0.0f, 1.0f, loadPresetOrDefault("oak_large.json", TreeOptions::defaultOak));

    // Pine tree
    float pineX = 50.0f, pineZ = -30.0f;
    glm::vec3 pinePos(pineX, core.terrain.getHeightAt(pineX, pineZ), pineZ);
    treeSystem->addTree(pinePos, 0.5f, 1.0f, loadPresetOrDefault("pine_large.json", TreeOptions::defaultPine));

    // Ash tree
    float ashX = -40.0f, ashZ = -25.0f;
    glm::vec3 ashPos(ashX, core.terrain.getHeightAt(ashX, ashZ), ashZ);
    treeSystem->addTree(ashPos, 1.0f, 1.0f, loadPresetOrDefault("ash_large.json", TreeOptions::defaultOak));

    // Aspen tree
    float aspenX = 30.0f, aspenZ = 40.0f;
    glm::vec3 aspenPos(aspenX, core.terrain.getHeightAt(aspenX, aspenZ), aspenZ);
    treeSystem->addTree(aspenPos, 1.5f, 1.0f, loadPresetOrDefault("aspen_large.json", TreeOptions::defaultOak));

    systems_->setTree(std::move(treeSystem));
    SDL_Log("Tree system initialized with %zu trees", systems_->tree()->getTreeCount());

    // Initialize TreeRenderer for dedicated tree shaders with wind animation
    {
        TreeRenderer::InitInfo treeRendererInfo{};
        treeRendererInfo.device = vk::Device(device);
        treeRendererInfo.physicalDevice = vk::PhysicalDevice(physicalDevice);
        treeRendererInfo.allocator = allocator;
        treeRendererInfo.hdrRenderPass = vk::RenderPass(systems_->postProcess().getHDRRenderPass());
        treeRendererInfo.shadowRenderPass = vk::RenderPass(systems_->shadow().getShadowRenderPass());
        treeRendererInfo.descriptorPool = &*descriptorManagerPool;
        treeRendererInfo.extent = vk::Extent2D{systems_->postProcess().getExtent().width, systems_->postProcess().getExtent().height};
        treeRendererInfo.shadowMapSize = systems_->shadow().getShadowMapSize();
        treeRendererInfo.resourcePath = resourcePath;
        treeRendererInfo.maxFramesInFlight = MAX_FRAMES_IN_FLIGHT;

        auto treeRenderer = TreeRenderer::create(treeRendererInfo);
        if (!treeRenderer) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create TreeRenderer");
            return false;
        }
        systems_->setTreeRenderer(std::move(treeRenderer));
        SDL_Log("TreeRenderer initialized for wind animation");
    }

    // Initialize TreeLODSystem for impostor rendering
    {
        TreeLODSystem::InitInfo treeLODInfo{};
        treeLODInfo.raiiDevice = &vulkanContext_->getRaiiDevice();
        treeLODInfo.device = device;
        treeLODInfo.physicalDevice = physicalDevice;
        treeLODInfo.allocator = allocator;
        treeLODInfo.hdrRenderPass = systems_->postProcess().getHDRRenderPass();
        treeLODInfo.shadowRenderPass = systems_->shadow().getShadowRenderPass();
        treeLODInfo.commandPool = commandPool.get();
        treeLODInfo.graphicsQueue = graphicsQueue;
        treeLODInfo.descriptorPool = &*descriptorManagerPool;
        treeLODInfo.extent = systems_->postProcess().getExtent();
        treeLODInfo.resourcePath = resourcePath;
        treeLODInfo.maxFramesInFlight = MAX_FRAMES_IN_FLIGHT;
        treeLODInfo.shadowMapSize = systems_->shadow().getShadowMapSize();

        auto treeLOD = TreeLODSystem::create(treeLODInfo);
        if (treeLOD) {
            systems_->setTreeLOD(std::move(treeLOD));
            SDL_Log("TreeLODSystem initialized for impostor rendering");
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TreeLODSystem creation failed (non-fatal)");
        }
    }

    // Initialize ImpostorCullSystem for GPU-driven impostor culling with Hi-Z
    {
        ImpostorCullSystem::InitInfo impostorCullInfo{};
        impostorCullInfo.device = device;
        impostorCullInfo.physicalDevice = physicalDevice;
        impostorCullInfo.allocator = allocator;
        impostorCullInfo.descriptorPool = &*descriptorManagerPool;
        impostorCullInfo.extent = systems_->postProcess().getExtent();
        impostorCullInfo.resourcePath = resourcePath;
        impostorCullInfo.maxFramesInFlight = MAX_FRAMES_IN_FLIGHT;
        impostorCullInfo.maxTrees = 100000;
        impostorCullInfo.maxArchetypes = 16;

        auto impostorCull = ImpostorCullSystem::create(impostorCullInfo);
        if (impostorCull) {
            systems_->setImpostorCull(std::move(impostorCull));
            SDL_Log("ImpostorCullSystem initialized for GPU-driven Hi-Z occlusion culling");
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ImpostorCullSystem creation failed (non-fatal)");
        }
    }

    // Add a forest 300 units away from the initial position (0, 0, 0)
    // The forest is placed away from spawn for load testing
    {
        auto* treeSystem = systems_->tree();
        if (treeSystem) {
            const float forestCenterX = 300.0f;
            const float forestCenterZ = 100.0f;
            const float forestRadius = 80.0f;
            const int numTrees = 500;

            std::string presetDir = resourcePath + "/assets/trees/presets/";
            std::vector<std::pair<std::string, TreeOptions(*)()>> treePresets = {
                {"oak_medium.json", TreeOptions::defaultOak},
                {"pine_medium.json", TreeOptions::defaultPine},
                {"ash_medium.json", TreeOptions::defaultOak},
                {"aspen_medium.json", TreeOptions::defaultOak}
            };

            auto loadPreset = [&](const std::string& presetName, TreeOptions (*defaultFn)()) {
                std::string path = presetDir + presetName;
                if (std::filesystem::exists(path)) {
                    return TreeOptions::loadFromJson(path);
                }
                return defaultFn();
            };

            // Poisson disk sampling for natural tree placement
            // Minimum distance between trees (adjusted for tree size variation)
            const float minDist = 8.0f;
            const int maxAttempts = 30;  // Attempts to place each new point

            // Simple LCG pseudo-random generator for deterministic results
            uint32_t seed = 12345;
            auto nextRand = [&seed]() -> float {
                seed = seed * 1103515245 + 12345;
                return static_cast<float>((seed >> 16) & 0x7FFF) / 32767.0f;
            };

            std::vector<glm::vec2> placedTrees;
            placedTrees.reserve(numTrees);

            // Start with a random point in the forest area
            placedTrees.push_back(glm::vec2(forestCenterX, forestCenterZ));

            // Active list for Poisson disk sampling
            std::vector<size_t> activeList;
            activeList.push_back(0);

            int treesPlaced = 0;
            while (!activeList.empty() && treesPlaced < numTrees) {
                // Pick a random active point
                size_t activeIdx = static_cast<size_t>(nextRand() * activeList.size());
                if (activeIdx >= activeList.size()) activeIdx = activeList.size() - 1;
                glm::vec2 activePoint = placedTrees[activeList[activeIdx]];

                bool foundValid = false;
                for (int attempt = 0; attempt < maxAttempts; attempt++) {
                    // Generate random point in annulus [minDist, 2*minDist] around active point
                    float angle = nextRand() * 2.0f * 3.14159265f;
                    float dist = minDist + nextRand() * minDist;
                    glm::vec2 newPoint = activePoint + glm::vec2(std::cos(angle), std::sin(angle)) * dist;

                    // Check if within forest bounds (circular)
                    float distFromCenter = glm::length(newPoint - glm::vec2(forestCenterX, forestCenterZ));
                    if (distFromCenter > forestRadius) continue;

                    // Check distance from all existing points
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
                    // Remove from active list if no valid point found
                    activeList.erase(activeList.begin() + activeIdx);
                }
            }

            // Add trees at Poisson-distributed positions
            for (size_t i = 0; i < placedTrees.size() && treesPlaced < numTrees; i++) {
                float x = placedTrees[i].x;
                float z = placedTrees[i].y;
                float y = core.terrain.getHeightAt(x, z);

                // Random rotation and scale variation
                float rotation = nextRand() * 2.0f * 3.14159265f;
                float scale = 0.7f + 0.6f * nextRand();

                // Select tree type pseudo-randomly
                size_t presetIdx = static_cast<size_t>(nextRand() * treePresets.size());
                if (presetIdx >= treePresets.size()) presetIdx = treePresets.size() - 1;
                auto opts = loadPreset(treePresets[presetIdx].first, treePresets[presetIdx].second);

                treeSystem->addTree(glm::vec3(x, y, z), rotation, scale, opts);
                treesPlaced++;
            }

            SDL_Log("Forest added: %d trees (Poisson disk) at distance 300 units", treesPlaced);

            // Generate impostor archetypes for each unique tree type
            // The first 4 trees (display trees) define the archetypes: oak, pine, ash, aspen
            auto* treeLOD = systems_->treeLOD();
            if (treeLOD) {
                // Archetype definitions: mesh index -> (name, bark, leaves)
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
                    if (info.meshIndex >= treeSystem->getMeshCount()) continue;

                    const auto& branchMesh = treeSystem->getBranchMesh(info.meshIndex);
                    const auto& leafInstances = treeSystem->getLeafInstances(info.meshIndex);
                    const auto& treeOpts = treeSystem->getTreeOptions(info.meshIndex);

                    auto* barkTex = treeSystem->getBarkTexture(info.bark);
                    auto* barkNorm = treeSystem->getBarkNormalMap(info.bark);
                    auto* leafTex = treeSystem->getLeafTexture(info.leaves);

                    if (barkTex && barkNorm && leafTex) {
                        int32_t archetypeIdx = treeLOD->generateImpostor(
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
                            SDL_Log("Generated impostor archetype %d: %s", archetypeIdx, info.name.c_str());
                        } else {
                            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to generate %s impostor", info.name.c_str());
                        }
                    } else {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Missing textures for %s impostor", info.name.c_str());
                    }
                }
            }

            // Update ImpostorCullSystem with tree data for GPU-driven culling
            auto* impostorCull = systems_->impostorCull();
            if (impostorCull && treeLOD) {
                impostorCull->updateTreeData(*treeSystem, treeLOD->getImpostorAtlas());
                impostorCull->updateArchetypeData(treeLOD->getImpostorAtlas());
                impostorCull->initializeDescriptorSets();
                SDL_Log("ImpostorCullSystem: Updated with %u trees", impostorCull->getTreeCount());
            }

            // Initialize TreeLODSystem descriptor sets now that impostors are generated
            if (treeLOD) {
                treeLOD->initializeDescriptorSets(
                    systems_->globalBuffers().uniformBuffers.buffers,
                    systems_->shadow().getShadowImageView(),
                    systems_->shadow().getShadowSampler());

                // If GPU culling is available, update instance buffer binding
                if (impostorCull) {
                    treeLOD->initializeGPUCulledDescriptors(impostorCull->getVisibleImpostorBuffer());
                }
            }
        }
    }

    // Initialize detritus system (fallen branches scattered near trees)
    {
        DetritusSystem::InitInfo detritusInfo{};
        detritusInfo.device = device;
        detritusInfo.allocator = allocator;
        detritusInfo.commandPool = commandPool.get();
        detritusInfo.graphicsQueue = graphicsQueue;
        detritusInfo.physicalDevice = physicalDevice;
        detritusInfo.resourcePath = resourcePath;
        detritusInfo.terrainSize = core.terrain.size;
        detritusInfo.getTerrainHeight = core.terrain.getHeightAt;

        // Gather tree positions for scattering detritus nearby
        if (systems_->tree()) {
            const auto& treeInstances = systems_->tree()->getTreeInstances();
            detritusInfo.treePositions.reserve(treeInstances.size());
            for (const auto& tree : treeInstances) {
                detritusInfo.treePositions.push_back(tree.position);
            }
        }

        DetritusConfig detritusConfig{};
        detritusConfig.branchVariations = 8;
        detritusConfig.branchesPerVariation = 4;
        detritusConfig.minLength = 0.5f;
        detritusConfig.maxLength = 2.5f;
        detritusConfig.minRadius = 0.03f;
        detritusConfig.maxRadius = 0.12f;
        detritusConfig.placementRadius = 8.0f;  // Scatter within 8m of each tree
        detritusConfig.minDistanceBetween = 1.5f;
        detritusConfig.breakChance = 0.7f;
        detritusConfig.maxChildren = 3;
        detritusConfig.materialRoughness = 0.85f;
        detritusConfig.materialMetallic = 0.0f;

        auto detritusSystem = DetritusSystem::create(detritusInfo, detritusConfig);
        if (detritusSystem) {
            systems_->setDetritus(std::move(detritusSystem));
            SDL_Log("DetritusSystem initialized with %zu fallen branches near %zu trees",
                    systems_->detritus()->getDetritusCount(), detritusInfo.treePositions.size());
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "DetritusSystem creation failed (non-fatal)");
        }
    }

    // Update rock descriptor sets now that rock textures are loaded
    {
        MaterialDescriptorFactory factory(device);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            MaterialDescriptorFactory::CommonBindings common{};
            common.uniformBuffer = systems_->globalBuffers().uniformBuffers.buffers[i];
            common.uniformBufferSize = sizeof(UniformBufferObject);
            common.shadowMapView = systems_->shadow().getShadowImageView();
            common.shadowMapSampler = systems_->shadow().getShadowSampler();
            common.lightBuffer = systems_->globalBuffers().lightBuffers.buffers[i];
            common.lightBufferSize = sizeof(LightBuffer);
            common.emissiveMapView = systems_->scene().getSceneBuilder().getDefaultEmissiveMap().getImageView();
            common.emissiveMapSampler = systems_->scene().getSceneBuilder().getDefaultEmissiveMap().getSampler();
            common.pointShadowView = systems_->shadow().getPointShadowArrayView(i);
            common.pointShadowSampler = systems_->shadow().getPointShadowSampler();
            common.spotShadowView = systems_->shadow().getSpotShadowArrayView(i);
            common.spotShadowSampler = systems_->shadow().getSpotShadowSampler();
            common.snowMaskView = systems_->snowMask().getSnowMaskView();
            common.snowMaskSampler = systems_->snowMask().getSnowMaskSampler();
            // Placeholder texture for unused PBR bindings (13-16)
            common.placeholderTextureView = systems_->scene().getSceneBuilder().getWhiteTexture().getImageView();
            common.placeholderTextureSampler = systems_->scene().getSceneBuilder().getWhiteTexture().getSampler();

            MaterialDescriptorFactory::MaterialTextures mat{};
            mat.diffuseView = systems_->rock().getRockTexture().getImageView();
            mat.diffuseSampler = systems_->rock().getRockTexture().getSampler();
            mat.normalView = systems_->rock().getRockNormalMap().getImageView();
            mat.normalSampler = systems_->rock().getRockNormalMap().getSampler();
            factory.writeDescriptorSet(rockDescriptorSets[i], common, mat);
        }
    }

    // Update detritus descriptor sets now that detritus textures are loaded
    if (systems_->detritus()) {
        MaterialDescriptorFactory factory(device);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            MaterialDescriptorFactory::CommonBindings common{};
            common.uniformBuffer = systems_->globalBuffers().uniformBuffers.buffers[i];
            common.uniformBufferSize = sizeof(UniformBufferObject);
            common.shadowMapView = systems_->shadow().getShadowImageView();
            common.shadowMapSampler = systems_->shadow().getShadowSampler();
            common.lightBuffer = systems_->globalBuffers().lightBuffers.buffers[i];
            common.lightBufferSize = sizeof(LightBuffer);
            common.emissiveMapView = systems_->scene().getSceneBuilder().getDefaultEmissiveMap().getImageView();
            common.emissiveMapSampler = systems_->scene().getSceneBuilder().getDefaultEmissiveMap().getSampler();
            common.pointShadowView = systems_->shadow().getPointShadowArrayView(i);
            common.pointShadowSampler = systems_->shadow().getPointShadowSampler();
            common.spotShadowView = systems_->shadow().getSpotShadowArrayView(i);
            common.spotShadowSampler = systems_->shadow().getSpotShadowSampler();
            common.snowMaskView = systems_->snowMask().getSnowMaskView();
            common.snowMaskSampler = systems_->snowMask().getSnowMaskSampler();
            common.placeholderTextureView = systems_->scene().getSceneBuilder().getWhiteTexture().getImageView();
            common.placeholderTextureSampler = systems_->scene().getSceneBuilder().getWhiteTexture().getSampler();

            MaterialDescriptorFactory::MaterialTextures mat{};
            mat.diffuseView = systems_->detritus()->getBarkTexture().getImageView();
            mat.diffuseSampler = systems_->detritus()->getBarkTexture().getSampler();
            mat.normalView = systems_->detritus()->getBarkNormalMap().getImageView();
            mat.normalSampler = systems_->detritus()->getBarkNormalMap().getSampler();
            factory.writeDescriptorSet(detritusDescriptorSets[i], common, mat);
        }
    }

    // Allocate and update tree descriptor sets for all texture types (string-based maps)
    if (systems_->tree()) {
        MaterialDescriptorFactory factory(device);

        // Allocate descriptor sets for each bark type
        for (const auto& typeName : systems_->tree()->getBarkTextureTypes()) {
            auto sets = descriptorManagerPool->allocate(descriptorSetLayout.get(), MAX_FRAMES_IN_FLIGHT);
            if (sets.empty()) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate bark descriptor sets for type: %s", typeName.c_str());
                return false;
            }
            treeBarkDescriptorSets[typeName] = std::move(sets);
        }

        // Allocate descriptor sets for each leaf type
        for (const auto& typeName : systems_->tree()->getLeafTextureTypes()) {
            auto sets = descriptorManagerPool->allocate(descriptorSetLayout.get(), MAX_FRAMES_IN_FLIGHT);
            if (sets.empty()) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate leaf descriptor sets for type: %s", typeName.c_str());
                return false;
            }
            treeLeafDescriptorSets[typeName] = std::move(sets);
        }

        // Write descriptor sets for each frame
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            MaterialDescriptorFactory::CommonBindings common{};
            common.uniformBuffer = systems_->globalBuffers().uniformBuffers.buffers[i];
            common.uniformBufferSize = sizeof(UniformBufferObject);
            common.shadowMapView = systems_->shadow().getShadowImageView();
            common.shadowMapSampler = systems_->shadow().getShadowSampler();
            common.lightBuffer = systems_->globalBuffers().lightBuffers.buffers[i];
            common.lightBufferSize = sizeof(LightBuffer);
            common.emissiveMapView = systems_->scene().getSceneBuilder().getDefaultEmissiveMap().getImageView();
            common.emissiveMapSampler = systems_->scene().getSceneBuilder().getDefaultEmissiveMap().getSampler();
            common.pointShadowView = systems_->shadow().getPointShadowArrayView(i);
            common.pointShadowSampler = systems_->shadow().getPointShadowSampler();
            common.spotShadowView = systems_->shadow().getSpotShadowArrayView(i);
            common.spotShadowSampler = systems_->shadow().getSpotShadowSampler();
            common.snowMaskView = systems_->snowMask().getSnowMaskView();
            common.snowMaskSampler = systems_->snowMask().getSnowMaskSampler();
            common.placeholderTextureView = systems_->scene().getSceneBuilder().getWhiteTexture().getImageView();
            common.placeholderTextureSampler = systems_->scene().getSceneBuilder().getWhiteTexture().getSampler();
            // Wind buffer for vegetation animation
            common.windBuffer = windBuffers[i];
            common.windBufferSize = 32;  // sizeof(WindUBO): 2 x vec4

            // Write descriptor sets for each bark type
            for (const auto& typeName : systems_->tree()->getBarkTextureTypes()) {
                Texture* barkTex = systems_->tree()->getBarkTexture(typeName);
                Texture* barkNormal = systems_->tree()->getBarkNormalMap(typeName);

                MaterialDescriptorFactory::MaterialTextures branchMat{};
                branchMat.diffuseView = barkTex->getImageView();
                branchMat.diffuseSampler = barkTex->getSampler();
                branchMat.normalView = barkNormal->getImageView();
                branchMat.normalSampler = barkNormal->getSampler();
                factory.writeDescriptorSet(treeBarkDescriptorSets[typeName][i], common, branchMat);
            }

            // Write descriptor sets for each leaf type
            for (const auto& typeName : systems_->tree()->getLeafTextureTypes()) {
                Texture* leafTex = systems_->tree()->getLeafTexture(typeName);
                // Use oak bark normal as placeholder for leaves
                Texture* barkNormal = systems_->tree()->getBarkNormalMap("oak");

                MaterialDescriptorFactory::MaterialTextures leafMat{};
                leafMat.diffuseView = leafTex->getImageView();
                leafMat.diffuseSampler = leafTex->getSampler();
                leafMat.normalView = barkNormal->getImageView();
                leafMat.normalSampler = barkNormal->getSampler();
                factory.writeDescriptorSet(treeLeafDescriptorSets[typeName][i], common, leafMat);
            }
        }
    }

    // Initialize weather and leaf subsystems
    {
        auto bundle = WeatherSystem::createWithDependencies(initCtx, core.hdr.renderPass);
        if (!bundle) {
            return false;
        }
        systems_->setWeather(std::move(bundle->weather));
        systems_->setLeaf(std::move(bundle->leaf));
    }

    // Connect leaf system to environment settings
    systems_->leaf().setEnvironmentSettings(envSettings);

    // Update weather system descriptor sets
    systems_->weather().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, windBuffers,
                                        systems_->postProcess().getHDRDepthView(), systems_->shadow().getShadowSampler(),
                                        &systems_->globalBuffers().dynamicRendererUBO);

    // Connect snow to environment settings and systems
    systems_->snowMask().setEnvironmentSettings(envSettings);
    systems_->volumetricSnow().setEnvironmentSettings(envSettings);
    systems_->terrain().setSnowMask(device, systems_->snowMask().getSnowMaskView(), systems_->snowMask().getSnowMaskSampler());
    systems_->terrain().setVolumetricSnowCascades(device,
        systems_->volumetricSnow().getCascadeView(0), systems_->volumetricSnow().getCascadeView(1),
        systems_->volumetricSnow().getCascadeView(2), systems_->volumetricSnow().getCascadeSampler());
    systems_->grass().setSnowMask(device, systems_->snowMask().getSnowMaskView(), systems_->snowMask().getSnowMaskSampler());

    // Update leaf system descriptor sets
    // Pass triple-buffered tile info buffers to avoid CPU-GPU sync issues
    std::array<VkBuffer, 3> leafTileInfoBuffers = {
        systems_->terrain().getTileInfoBuffer(0),
        systems_->terrain().getTileInfoBuffer(1),
        systems_->terrain().getTileInfoBuffer(2)
    };
    systems_->leaf().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, windBuffers,
                                     systems_->terrain().getHeightMapView(), systems_->terrain().getHeightMapSampler(),
                                     systems_->grass().getDisplacementImageView(), systems_->grass().getDisplacementSampler(),
                                     systems_->terrain().getTileArrayView(), systems_->terrain().getTileSampler(),
                                     leafTileInfoBuffers, &systems_->globalBuffers().dynamicRendererUBO);

    // Initialize atmosphere subsystems (Froxel, AtmosphereLUT, CloudShadow)
    if (!RendererInit::initAtmosphereSubsystems(*systems_, initCtx, core.shadow,
                                                 systems_->globalBuffers().lightBuffers.buffers)) return false;

    // Update grass descriptor sets (now that CloudShadowSystem exists)
    // Pass triple-buffered tile info buffers to avoid CPU-GPU sync issues
    std::array<VkBuffer, 3> tileInfoBuffers = {
        systems_->terrain().getTileInfoBuffer(0),
        systems_->terrain().getTileInfoBuffer(1),
        systems_->terrain().getTileInfoBuffer(2)
    };
    systems_->grass().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers, systems_->shadow().getShadowImageView(), systems_->shadow().getShadowSampler(), windBuffers, systems_->globalBuffers().lightBuffers.buffers,
                                      systems_->terrain().getHeightMapView(), systems_->terrain().getHeightMapSampler(),
                                      systems_->globalBuffers().snowBuffers.buffers, systems_->globalBuffers().cloudShadowBuffers.buffers,
                                      systems_->cloudShadow().getShadowMapView(), systems_->cloudShadow().getShadowMapSampler(),
                                      systems_->terrain().getTileArrayView(), systems_->terrain().getTileSampler(),
                                      tileInfoBuffers, &systems_->globalBuffers().dynamicRendererUBO);

    // Connect froxel volume to weather system
    systems_->weather().setFroxelVolume(systems_->froxel().getScatteringVolumeView(), systems_->froxel().getVolumeSampler(),
                                   systems_->froxel().getVolumetricFarPlane(), FroxelSystem::DEPTH_DISTRIBUTION);

    // Connect cloud shadow map to terrain system
    systems_->terrain().setCloudShadowMap(device, systems_->cloudShadow().getShadowMapView(), systems_->cloudShadow().getShadowMapSampler());

    // Note: Caustics setup moved to initPhase4Complete after water system is created

    // Update cloud shadow bindings across all descriptor sets
    RendererInit::updateCloudShadowBindings(device, systems_->scene().getSceneBuilder().getMaterialRegistry(),
                                            rockDescriptorSets, detritusDescriptorSets, systems_->skinnedMesh(),
                                            systems_->cloudShadow().getShadowMapView(), systems_->cloudShadow().getShadowMapSampler());

    // Initialize Catmull-Clark subdivision system via factory
    float suzanneX = 5.0f, suzanneZ = -5.0f;
    glm::vec3 suzannePos(suzanneX, core.terrain.getHeightAt(suzanneX, suzanneZ) + 2.0f, suzanneZ);

    CatmullClarkSystem::InitInfo catmullClarkInfo{};
    catmullClarkInfo.device = device;
    catmullClarkInfo.physicalDevice = physicalDevice;
    catmullClarkInfo.allocator = allocator;
    catmullClarkInfo.renderPass = core.hdr.renderPass;
    catmullClarkInfo.descriptorPool = initCtx.descriptorPool;
    catmullClarkInfo.extent = initCtx.extent;
    catmullClarkInfo.shaderPath = initCtx.shaderPath;
    catmullClarkInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    catmullClarkInfo.graphicsQueue = graphicsQueue;
    catmullClarkInfo.commandPool = commandPool.get();

    CatmullClarkConfig catmullClarkConfig{};
    catmullClarkConfig.position = suzannePos;
    catmullClarkConfig.scale = glm::vec3(2.0f);
    catmullClarkConfig.targetEdgePixels = 12.0f;
    catmullClarkConfig.maxDepth = 16;
    catmullClarkConfig.splitThreshold = 18.0f;
    catmullClarkConfig.mergeThreshold = 6.0f;
    catmullClarkConfig.objPath = resourcePath + "/assets/suzanne.obj";

    auto catmullClarkSystem = CatmullClarkSystem::create(catmullClarkInfo, catmullClarkConfig);
    if (!catmullClarkSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create CatmullClarkSystem");
        return false;
    }
    systems_->setCatmullClark(std::move(catmullClarkSystem));
    systems_->catmullClark().updateDescriptorSets(device, systems_->globalBuffers().uniformBuffers.buffers);

    // Create sky descriptor sets now that uniform buffers and LUTs are ready
    if (!systems_->sky().createDescriptorSets(systems_->globalBuffers().uniformBuffers.buffers, sizeof(UniformBufferObject), systems_->atmosphereLUT())) return false;

    // Initialize Hi-Z occlusion culling system via factory
    auto hiZSystem = HiZSystem::create(initCtx, depthFormat);
    if (!hiZSystem) {
        SDL_Log("Warning: Hi-Z system initialization failed, occlusion culling disabled");
        // Continue without Hi-Z - it's an optional optimization
    } else {
        systems_->setHiZ(std::move(hiZSystem));
        // Connect depth buffer to Hi-Z system - use HDR depth where scene is rendered
        systems_->hiZ().setDepthBuffer(core.hdr.depthView, depthSampler.get());

        // Initialize object data for culling
        updateHiZObjectData();
    }

    // Initialize profiler for GPU and CPU timing
    // Factory always returns valid profiler - GPU may be disabled if init fails
    systems_->setProfiler(Profiler::create(device, physicalDevice, MAX_FRAMES_IN_FLIGHT));

    // Create WaterSystem via factory before initializing other water subsystems
    {
        INIT_PROFILE_PHASE("WaterSystem");
        WaterSystem::InitInfo waterInfo{};
        waterInfo.device = device;
        waterInfo.physicalDevice = physicalDevice;
        waterInfo.allocator = allocator;
        waterInfo.descriptorPool = initCtx.descriptorPool;
        waterInfo.hdrRenderPass = core.hdr.renderPass;
        waterInfo.shaderPath = initCtx.shaderPath;
        waterInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
        waterInfo.extent = initCtx.extent;
        waterInfo.commandPool = commandPool.get();
        waterInfo.graphicsQueue = graphicsQueue;
        waterInfo.waterSize = 65536.0f;  // Extend well beyond terrain for horizon
        waterInfo.assetPath = resourcePath;

        auto waterSystem = WaterSystem::create(waterInfo);
        if (!waterSystem) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create WaterSystem");
            return false;
        }
        systems_->setWater(std::move(waterSystem));
    }

    // Create water subsystems via factories before constructing WaterSubsystems struct
    // FlowMapGenerator
    FlowMapGenerator::InitInfo flowInfo{};
    flowInfo.device = device;
    flowInfo.allocator = allocator;
    flowInfo.commandPool = commandPool.get();
    flowInfo.queue = graphicsQueue;

    auto flowMapGenerator = FlowMapGenerator::create(flowInfo);
    if (!flowMapGenerator) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create FlowMapGenerator");
        return false;
    }
    systems_->setFlowMap(std::move(flowMapGenerator));

    // WaterDisplacement
    WaterDisplacement::InitInfo dispInfo{};
    dispInfo.device = device;
    dispInfo.physicalDevice = physicalDevice;
    dispInfo.allocator = allocator;
    dispInfo.commandPool = commandPool.get();
    dispInfo.computeQueue = graphicsQueue;
    dispInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    dispInfo.displacementResolution = 512;
    dispInfo.worldSize = 65536.0f;

    auto waterDisplacement = WaterDisplacement::create(dispInfo);
    if (!waterDisplacement) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create WaterDisplacement");
        return false;
    }
    systems_->setWaterDisplacement(std::move(waterDisplacement));

    // FoamBuffer
    FoamBuffer::InitInfo foamInfo{};
    foamInfo.device = device;
    foamInfo.physicalDevice = physicalDevice;
    foamInfo.allocator = allocator;
    foamInfo.commandPool = commandPool.get();
    foamInfo.computeQueue = graphicsQueue;
    foamInfo.shaderPath = initCtx.shaderPath;
    foamInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    foamInfo.resolution = 512;
    foamInfo.worldSize = 65536.0f;

    auto foamBuffer = FoamBuffer::create(foamInfo);
    if (!foamBuffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create FoamBuffer");
        return false;
    }
    systems_->setFoam(std::move(foamBuffer));

    // WaterTileCull
    WaterTileCull::InitInfo tileCullInfo{};
    tileCullInfo.device = device;
    tileCullInfo.physicalDevice = physicalDevice;
    tileCullInfo.allocator = allocator;
    tileCullInfo.commandPool = commandPool.get();
    tileCullInfo.computeQueue = graphicsQueue;
    tileCullInfo.shaderPath = initCtx.shaderPath;
    tileCullInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    tileCullInfo.extent = initCtx.extent;
    tileCullInfo.tileSize = 32;

    auto waterTileCull = WaterTileCull::create(tileCullInfo);
    if (waterTileCull) {
        systems_->setWaterTileCull(std::move(waterTileCull));
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create WaterTileCull - continuing without");
    }

    // WaterGBuffer
    WaterGBuffer::InitInfo gbufferInfo{};
    gbufferInfo.device = device;
    gbufferInfo.physicalDevice = physicalDevice;
    gbufferInfo.allocator = allocator;
    gbufferInfo.fullResExtent = initCtx.extent;
    gbufferInfo.resolutionScale = 0.5f;
    gbufferInfo.framesInFlight = MAX_FRAMES_IN_FLIGHT;
    gbufferInfo.shaderPath = initCtx.shaderPath;
    gbufferInfo.descriptorPool = initCtx.descriptorPool;

    auto waterGBuffer = WaterGBuffer::create(gbufferInfo);
    if (waterGBuffer) {
        systems_->setWaterGBuffer(std::move(waterGBuffer));
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to create WaterGBuffer - continuing without");
    }

    // Initialize water subsystems (configure WaterSystem, generate flow map, create SSR)
    WaterSubsystems waterSubs{systems_->water(), systems_->waterDisplacement(), systems_->flowMap(), systems_->foam(), *systems_, systems_->waterTileCull(), systems_->waterGBuffer()};
    if (!RendererInit::initWaterSubsystems(waterSubs, initCtx, core.hdr.renderPass,
                                            systems_->shadow(), systems_->terrain(), terrainConfig, systems_->postProcess(), depthSampler.get())) return false;

    // Create water descriptor sets
    if (!RendererInit::createWaterDescriptorSets(waterSubs, systems_->globalBuffers().uniformBuffers.buffers,
                                                  sizeof(UniformBufferObject), systems_->shadow(), systems_->terrain(),
                                                  systems_->postProcess(), depthSampler.get())) return false;

    // Connect underwater caustics to terrain system (use foam texture as caustics pattern)
    // Must happen after water system is fully initialized
    if (systems_->water().getFoamTextureView() != VK_NULL_HANDLE) {
        systems_->terrain().setCaustics(device,
                                         systems_->water().getFoamTextureView(),
                                         systems_->water().getFoamTextureSampler(),
                                         systems_->water().getWaterLevel(),
                                         true);  // Enable caustics
    }

    if (!createSyncObjects()) return false;

    // Create debug line system via factory
    auto debugLineSystem = DebugLineSystem::create(initCtx, core.hdr.renderPass);
    if (!debugLineSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create debug line system");
        return false;
    }
    systems_->setDebugLineSystem(std::move(debugLineSystem));
    SDL_Log("Debug line system initialized");

    // Initialize UBO builder with system references
    UBOBuilder::Systems uboSystems{};
    uboSystems.timeSystem = &systems_->time();
    uboSystems.celestialCalculator = &systems_->celestial();
    uboSystems.shadowSystem = &systems_->shadow();
    uboSystems.windSystem = &systems_->wind();
    uboSystems.atmosphereLUTSystem = &systems_->atmosphereLUT();
    uboSystems.froxelSystem = &systems_->froxel();
    uboSystems.sceneManager = &systems_->scene();
    uboSystems.snowMaskSystem = &systems_->snowMask();
    uboSystems.volumetricSnowSystem = &systems_->volumetricSnow();
    uboSystems.cloudShadowSystem = &systems_->cloudShadow();
    uboSystems.environmentSettings = &systems_->environmentSettings();
    systems_->uboBuilder().setSystems(uboSystems);

    return true;
}

void Renderer::initResizeCoordinator() {
    // Register systems with resize coordinator
    // Order matters: render targets first, then systems that depend on them, then viewport-only

    // Render targets that need full resize (device/allocator/extent)
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->postProcess(), "PostProcessSystem", ResizePriority::RenderTarget);
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->bloom(), "BloomSystem", ResizePriority::RenderTarget);
    systems_->resizeCoordinator().registerWithResize(systems_->froxel(), "FroxelSystem", ResizePriority::RenderTarget);

    // Culling systems with simple resize (extent only, but reallocates)
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->hiZ(), "HiZSystem", ResizePriority::Culling);
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->ssr(), "SSRSystem", ResizePriority::Culling);
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->waterTileCull(), "WaterTileCull", ResizePriority::Culling);

    // G-buffer systems
    systems_->resizeCoordinator().registerWithSimpleResize(systems_->waterGBuffer(), "WaterGBuffer", ResizePriority::GBuffer);

    // Viewport-only systems (setExtent)
    systems_->resizeCoordinator().registerWithExtent(systems_->terrain(), "TerrainSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->sky(), "SkySystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->water(), "WaterSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->grass(), "GrassSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->weather(), "WeatherSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->leaf(), "LeafSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->catmullClark(), "CatmullClarkSystem");
    systems_->resizeCoordinator().registerWithExtent(systems_->skinnedMesh(), "SkinnedMeshRenderer");

    // Register callback for bloom texture rebinding (needed after bloom resize)
    systems_->resizeCoordinator().registerCallback("BloomRebind",
        [this](VkDevice, VmaAllocator, VkExtent2D) {
            systems_->postProcess().setBloomTexture(systems_->bloom().getBloomOutput(), systems_->bloom().getBloomSampler());
        },
        nullptr,
        ResizePriority::RenderTarget);

    // Register core resize handler for swapchain, depth buffer, and framebuffers
    systems_->resizeCoordinator().setCoreResizeHandler([this](VkDevice, VmaAllocator) -> VkExtent2D {
        // Recreate swapchain
        if (!vulkanContext_->recreateSwapchain()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate swapchain");
            return {0, 0};
        }

        VkExtent2D newExtent = vulkanContext_->getSwapchainExtent();

        // Handle minimized window (extent = 0)
        if (newExtent.width == 0 || newExtent.height == 0) {
            return {0, 0};
        }

        SDL_Log("Window resized to %ux%u", newExtent.width, newExtent.height);

        // Recreate depth resources
        if (!recreateDepthResources(newExtent)) {
            return {0, 0};
        }

        // Recreate framebuffers
        destroyFramebuffers();
        if (!createFramebuffers()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to recreate framebuffers during resize");
            return {0, 0};
        }

        return newExtent;
    });

    SDL_Log("Resize coordinator configured with %zu systems", 17UL);
}

void Renderer::initControlSubsystems() {
    // Initialize control subsystems in RendererSystems
    // These subsystems implement GUI-facing interfaces directly
    systems_->initControlSubsystems(*vulkanContext_, perfToggles);

    // Set up the performance sync callback
    systems_->setPerformanceSyncCallback([this]() { syncPerformanceToggles(); });
}
