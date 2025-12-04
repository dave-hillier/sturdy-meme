#pragma once

#include <functional>
#include <string>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "DescriptorManager.h"

class SystemLifecycleHelper {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkRenderPass renderPass;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool (preferred)
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
    };

    struct PipelineHandles {
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    struct Hooks {
        std::function<bool()> createBuffers;
        std::function<bool()> createDescriptorSets;
        std::function<void(VmaAllocator)> destroyBuffers;

        std::function<bool()> createComputeDescriptorSetLayout = [] { return true; };
        std::function<bool()> createComputePipeline = [] { return true; };
        std::function<bool()> createGraphicsDescriptorSetLayout = [] { return true; };
        std::function<bool()> createGraphicsPipeline = [] { return true; };
        std::function<bool()> createExtraPipelines = [] { return true; };

        std::function<bool()> usesComputePipeline = [] { return true; };
        std::function<bool()> usesGraphicsPipeline = [] { return true; };
    };

    bool init(const InitInfo& info, const Hooks& hooks) {
        initInfo = info;
        callbacks = hooks;
        computeEnabled = callbacks.usesComputePipeline();
        graphicsEnabled = callbacks.usesGraphicsPipeline();

        if (!callbacks.createBuffers || !callbacks.createDescriptorSets || !callbacks.destroyBuffers) {
            return false;
        }

        if (!callbacks.createBuffers()) return false;

        if (computeEnabled) {
            if (!callbacks.createComputeDescriptorSetLayout()) return false;
            if (!callbacks.createComputePipeline()) return false;
        }

        if (graphicsEnabled) {
            if (!callbacks.createGraphicsDescriptorSetLayout()) return false;
            if (!callbacks.createGraphicsPipeline()) return false;
        }

        if (!callbacks.createExtraPipelines()) return false;
        if (!callbacks.createDescriptorSets()) return false;

        initialized = true;
        return true;
    }

    void destroy(VkDevice deviceOverride = VK_NULL_HANDLE, VmaAllocator allocatorOverride = VK_NULL_HANDLE) {
        if (!initialized) return;

        VkDevice dev = deviceOverride == VK_NULL_HANDLE ? initInfo.device : deviceOverride;
        VmaAllocator alloc = allocatorOverride == VK_NULL_HANDLE ? initInfo.allocator : allocatorOverride;

        if (graphicsEnabled) {
            destroyPipelineHandles(dev, graphicsPipeline);
        }

        if (computeEnabled) {
            destroyPipelineHandles(dev, computePipeline);
        }

        callbacks.destroyBuffers(alloc);
        initialized = false;
    }

    VkDevice getDevice() const { return initInfo.device; }
    VmaAllocator getAllocator() const { return initInfo.allocator; }
    VkRenderPass getRenderPass() const { return initInfo.renderPass; }
    DescriptorManager::Pool* getDescriptorPool() const { return initInfo.descriptorPool; }
    const VkExtent2D& getExtent() const { return initInfo.extent; }
    void setExtent(VkExtent2D newExtent) { initInfo.extent = newExtent; }
    const std::string& getShaderPath() const { return initInfo.shaderPath; }
    uint32_t getFramesInFlight() const { return initInfo.framesInFlight; }

    PipelineHandles& getComputePipeline() { return computePipeline; }
    PipelineHandles& getGraphicsPipeline() { return graphicsPipeline; }

private:
    void destroyPipelineHandles(VkDevice dev, PipelineHandles& handles) {
        vkDestroyPipeline(dev, handles.pipeline, nullptr);
        vkDestroyPipelineLayout(dev, handles.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(dev, handles.descriptorSetLayout, nullptr);
        handles.pipeline = VK_NULL_HANDLE;
        handles.pipelineLayout = VK_NULL_HANDLE;
        handles.descriptorSetLayout = VK_NULL_HANDLE;
    }

    InitInfo initInfo{};
    Hooks callbacks{};
    PipelineHandles computePipeline{};
    PipelineHandles graphicsPipeline{};
    bool computeEnabled = true;
    bool graphicsEnabled = true;
    bool initialized = false;
};

