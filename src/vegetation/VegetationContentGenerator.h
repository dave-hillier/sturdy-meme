#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

// Forward declarations
class TreeSystem;
class TreeLODSystem;
class ImpostorCullSystem;
class TreeRenderer;
class DetritusSystem;
class ThreadedTreeGenerator;
struct TreeOptions;
struct DetritusConfig;

/**
 * VegetationContentGenerator - Generates vegetation content (trees, forests, detritus)
 *
 * This separates content generation from system initialization.
 * RendererInitPhases creates the systems, then this class populates them.
 *
 * Usage:
 *   VegetationContentGenerator gen(resourcePath, getHeightAt);
 *   gen.generateDemoTrees(treeSystem, sceneOrigin);
 *   gen.generateForest(treeSystem, forestCenter, radius, numTrees);
 *   gen.generateImpostorArchetypes(treeSystem, treeLOD);
 *   gen.finalizeTreeSystems(treeSystem, treeLOD, impostorCull, treeRenderer, uniformBuffers, shadowView, shadowSampler);
 */
class VegetationContentGenerator {
public:
    using HeightFunc = std::function<float(float, float)>;

    struct Config {
        std::string resourcePath;
        HeightFunc getTerrainHeight;
        float terrainSize = 65536.0f;
    };

    explicit VegetationContentGenerator(const Config& config);
    ~VegetationContentGenerator();

    // Non-copyable
    VegetationContentGenerator(const VegetationContentGenerator&) = delete;
    VegetationContentGenerator& operator=(const VegetationContentGenerator&) = delete;

    /**
     * Generate demo trees near the scene origin.
     * Places 4 showcase trees (oak, pine, ash, aspen).
     */
    void generateDemoTrees(TreeSystem& treeSystem, const glm::vec2& sceneOrigin);

    /**
     * Generate a forest using Poisson disk sampling.
     * Uses threaded generation for large forests.
     * Returns number of trees placed.
     */
    int generateForest(
        TreeSystem& treeSystem,
        const glm::vec2& center,
        float radius,
        int maxTrees,
        uint32_t seed = 12345);

    /**
     * Generate impostor archetypes from the first 4 unique tree types.
     * Should be called after trees are added to the system.
     */
    void generateImpostorArchetypes(TreeSystem& treeSystem, TreeLODSystem& treeLOD);

    /**
     * Finalize tree systems after content generation.
     * Updates spatial indices, culling data, and descriptor sets.
     */
    void finalizeTreeSystems(
        TreeSystem& treeSystem,
        TreeLODSystem* treeLOD,
        ImpostorCullSystem* impostorCull,
        TreeRenderer* treeRenderer,
        const std::vector<VkBuffer>& uniformBuffers,
        VkImageView shadowView,
        VkSampler shadowSampler);

    /**
     * Create detritus system with fallen branches near trees.
     * Call after trees are generated.
     */
    struct DetritusCreateInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        VkPhysicalDevice physicalDevice;
    };
    std::unique_ptr<DetritusSystem> createDetritusSystem(
        const DetritusCreateInfo& info,
        const TreeSystem& treeSystem);

    /**
     * Get detritus configuration based on tree positions.
     * Call after trees are placed.
     */
    DetritusConfig getDetritusConfig() const;

    /**
     * Get tree positions for detritus scattering.
     */
    std::vector<glm::vec3> getTreePositionsForDetritus(const TreeSystem& treeSystem) const;

private:
    TreeOptions loadPresetOrDefault(const std::string& presetName, TreeOptions (*defaultFn)()) const;

    Config config_;
    std::string presetDir_;
};
