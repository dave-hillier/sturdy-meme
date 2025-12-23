#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

#include "TreeGPUData.h"
#include "TreeImpostorAtlas.h"
#include "core/VulkanRAII.h"
#include "core/DescriptorManager.h"

// Forward declarations
struct TreeInstanceData;

// GPU-driven tree LOD pipeline
// Moves all per-tree LOD calculations to GPU compute shaders
class TreeGPULODPipeline {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue computeQueue;
        DescriptorManager::Pool* descriptorPool;
        std::string resourcePath;
        uint32_t maxTrees;
        uint32_t maxFramesInFlight;
    };

    static std::unique_ptr<TreeGPULODPipeline> create(const InitInfo& info);
    ~TreeGPULODPipeline();

    // Non-copyable, non-movable
    TreeGPULODPipeline(const TreeGPULODPipeline&) = delete;
    TreeGPULODPipeline& operator=(const TreeGPULODPipeline&) = delete;
    TreeGPULODPipeline(TreeGPULODPipeline&&) = delete;
    TreeGPULODPipeline& operator=(TreeGPULODPipeline&&) = delete;

    // Upload tree instances when trees change (not per-frame)
    void uploadTreeInstances(const std::vector<TreeInstanceData>& trees,
                             const std::vector<glm::vec3>& boundingBoxHalfExtents,
                             const std::vector<float>& boundingSphereRadii);

    // Record compute passes for LOD selection
    // Call this before the main render pass
    void recordLODCompute(VkCommandBuffer cmd,
                          uint32_t frameIndex,
                          const glm::vec3& cameraPos,
                          const TreeLODSettings& settings);

    // Get LOD state buffer for use in rendering
    // Contains TreeLODStateGPU per tree after compute pass
    VkBuffer getLODStateBuffer() const { return lodStateBuffer_; }
    VkDeviceSize getLODStateBufferSize() const { return lodStateBufferSize_; }

    // Get draw counters (read back for debugging/stats)
    // Note: Reading this causes a GPU sync - only use for debugging
    TreeDrawCounters readDrawCounters();

    // Check if pipeline is ready
    bool isReady() const { return pipelinesReady_; }

    // Get current tree count
    uint32_t getTreeCount() const { return currentTreeCount_; }

private:
    TreeGPULODPipeline() = default;
    bool initInternal(const InitInfo& info);

    bool createDescriptorSetLayout();
    bool createDistancePipeline();
    bool createSortPipeline();
    bool createSelectPipeline();
    bool allocateDescriptorSets();
    bool createBuffers(uint32_t maxTrees);

    // Record bitonic sort dispatches
    void recordBitonicSort(VkCommandBuffer cmd, uint32_t numElements);

    // Calculate number of sort stages needed
    static uint32_t calculateSortStages(uint32_t n);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;
    uint32_t maxFramesInFlight_ = 0;

    // Compute pipelines
    ManagedPipeline distancePipeline_;
    ManagedPipeline sortPipeline_;
    ManagedPipeline selectPipeline_;

    // Pipeline layouts
    ManagedPipelineLayout distancePipelineLayout_;
    ManagedPipelineLayout sortPipelineLayout_;
    ManagedPipelineLayout selectPipelineLayout_;

    // Descriptor set layout (shared by all compute shaders)
    ManagedDescriptorSetLayout descriptorSetLayout_;

    // Per-frame descriptor sets
    std::vector<VkDescriptorSet> descriptorSets_;

    // GPU buffers
    VkBuffer treeInstanceBuffer_ = VK_NULL_HANDLE;      // TreeInstanceGPU[]
    VmaAllocation treeInstanceAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize treeInstanceBufferSize_ = 0;

    VkBuffer distanceKeyBuffer_ = VK_NULL_HANDLE;       // TreeDistanceKey[]
    VmaAllocation distanceKeyAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize distanceKeyBufferSize_ = 0;

    VkBuffer lodStateBuffer_ = VK_NULL_HANDLE;          // TreeLODStateGPU[]
    VmaAllocation lodStateAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize lodStateBufferSize_ = 0;

    VkBuffer counterBuffer_ = VK_NULL_HANDLE;           // TreeDrawCounters
    VmaAllocation counterAllocation_ = VK_NULL_HANDLE;

    // Per-frame uniform buffers
    std::vector<VkBuffer> uniformBuffers_;
    std::vector<VmaAllocation> uniformAllocations_;

    // State
    uint32_t maxTrees_ = 0;
    uint32_t currentTreeCount_ = 0;
    bool pipelinesReady_ = false;

    // Push constant for sort shader
    struct SortPushConstants {
        uint32_t numElements;
        uint32_t stage;
        uint32_t substage;
        uint32_t _pad;
    };
};
