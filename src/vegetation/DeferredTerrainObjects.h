#pragma once

#include "VegetationContentGenerator.h"
#include "ScatterSystemFactory.h"
#include "DescriptorManager.h"
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <functional>

class TreeSystem;
class TreeLODSystem;
class ImpostorCullSystem;
class TreeRenderer;
class ScatterSystem;

/**
 * DeferredTerrainObjects - Defers creation of trees, rocks, and detritus
 * until terrain tiles are fully loaded.
 *
 * Instead of generating vegetation content during initialization (blocking startup),
 * this class stores the configuration and generates content on the first frame
 * after the terrain system reports tiles are ready.
 *
 * Usage:
 *   1. Create during init with configuration
 *   2. Call tryGenerate() each frame from VegetationUpdater
 *   3. Once generated, the class becomes inactive
 */
class DeferredTerrainObjects {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit DeferredTerrainObjects(ConstructToken) {}

    struct Config {
        std::string resourcePath;
        float terrainSize = 16384.0f;
        std::function<float(float, float)> getTerrainHeight;

        // Scene positioning
        glm::vec2 sceneOrigin{0.0f, 0.0f};

        // Forest configuration
        glm::vec2 forestCenter{0.0f, 0.0f};
        float forestRadius = 80.0f;
        int maxTrees = 500;

        // Descriptor resources needed for finalizing tree systems
        std::vector<VkBuffer> uniformBuffers;
        VkImageView shadowView = VK_NULL_HANDLE;
        VkSampler shadowSampler = VK_NULL_HANDLE;

        // For creating detritus descriptor sets
        VkDevice device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        DescriptorManager::Pool* descriptorPool = nullptr;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        uint32_t framesInFlight = 3;
    };

    using GetCommonBindingsFunc = std::function<MaterialDescriptorFactory::CommonBindings(uint32_t)>;

    /**
     * Factory: Create DeferredTerrainObjects instance.
     */
    static std::unique_ptr<DeferredTerrainObjects> create(const Config& config);

    /**
     * Set the function to get common bindings for descriptor sets.
     * Must be called before tryGenerate() if detritus needs descriptor sets.
     */
    void setCommonBindingsFunc(GetCommonBindingsFunc func) { getCommonBindings_ = std::move(func); }

    /**
     * Attempt to generate terrain objects if not already done and terrain is ready.
     *
     * @param tree TreeSystem to populate with trees
     * @param treeLOD Optional TreeLODSystem for impostor generation
     * @param impostorCull Optional ImpostorCullSystem
     * @param treeRenderer Optional TreeRenderer
     * @param rocks ScatterSystem to check if generation is needed (rocks already exist)
     * @param detritus Output pointer for created detritus system
     * @param terrainReady True if terrain tiles are loaded and ready
     *
     * @return True if generation completed this frame, false otherwise
     */
    bool tryGenerate(
        TreeSystem* tree,
        TreeLODSystem* treeLOD,
        ImpostorCullSystem* impostorCull,
        TreeRenderer* treeRenderer,
        ScatterSystem* rocks,
        std::unique_ptr<ScatterSystem>& detritus,
        bool terrainReady
    );

    /**
     * Check if generation has been completed.
     */
    bool isGenerated() const { return generated_; }

    /**
     * Check if currently generating (for progress tracking).
     */
    bool isGenerating() const { return generating_; }

private:
    bool initInternal(const Config& config);

    Config config_;
    GetCommonBindingsFunc getCommonBindings_;
    bool generated_ = false;
    bool generating_ = false;
};
