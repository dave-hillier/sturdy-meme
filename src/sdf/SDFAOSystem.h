#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "SDFConfig.h"
#include "SDFAtlas.h"
#include "InitContext.h"
#include "DescriptorManager.h"

/**
 * SDFAOSystem - SDF-based Ambient Occlusion via Cone Tracing
 *
 * Traces cones against SDF atlas to compute sub-meter ambient occlusion
 * for buildings and other static geometry. This complements screen-space
 * GTAO by capturing off-screen and distant occluders.
 *
 * Based on UE4's Distance Field Ambient Occlusion technique.
 */
class SDFAOSystem {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue computeQueue;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkExtent2D extent;
        DescriptorManager::Pool* descriptorPool;
        SDFAtlas* sdfAtlas;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Push constants for SDF-AO compute shader
    struct SDFAOPushConstants {
        glm::mat4 invViewMatrix;
        glm::mat4 invProjMatrix;
        glm::vec4 screenParams;     // xy = resolution, zw = 1/resolution
        glm::vec4 aoParams;         // x = numCones, y = maxSteps, z = coneAngle, w = maxDistance
        glm::vec4 aoParams2;        // x = intensity, y = bias, z = atlasResolution, w = numInstances
        float nearPlane;
        float farPlane;
        float padding[2];
    };

    static std::unique_ptr<SDFAOSystem> create(const InitInfo& info);
    static std::unique_ptr<SDFAOSystem> create(const InitContext& ctx, SDFAtlas* atlas);

    ~SDFAOSystem();

    // Non-copyable
    SDFAOSystem(const SDFAOSystem&) = delete;
    SDFAOSystem& operator=(const SDFAOSystem&) = delete;

    void resize(VkExtent2D newExtent);

    /**
     * Record SDF-AO compute pass.
     * Call after depth pass, before final lighting.
     */
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex,
                       VkImageView depthView,
                       VkImageView normalView,
                       VkSampler depthSampler,
                       const glm::mat4& invView, const glm::mat4& invProj,
                       float nearPlane, float farPlane);

    // Get AO result for combining with GTAO
    VkImageView getAOResultView() const { return aoResultView_; }
    VkSampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    // Configuration
    void setEnabled(bool enable) { enabled_ = enable; }
    bool isEnabled() const { return enabled_; }

    void setIntensity(float i) { intensity_ = i; }
    float getIntensity() const { return intensity_; }

    void setMaxDistance(float d) { maxDistance_ = d; }
    float getMaxDistance() const { return maxDistance_; }

    SDFAtlas* getAtlas() { return sdfAtlas_; }
    const SDFAtlas* getAtlas() const { return sdfAtlas_; }

private:
    SDFAOSystem() = default;
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createAOBuffer();
    bool createComputePipeline();
    bool createDescriptorSets();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    std::string shaderPath_;
    uint32_t framesInFlight_ = 0;
    VkExtent2D extent_ = {0, 0};
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    SDFAtlas* sdfAtlas_ = nullptr;
    const vk::raii::Device* raiiDevice_ = nullptr;

    bool enabled_ = true;
    float intensity_ = 1.0f;
    float maxDistance_ = 10.0f;

    // AO output (R8_UNORM, half resolution)
    VkImage aoResult_ = VK_NULL_HANDLE;
    VkImageView aoResultView_ = VK_NULL_HANDLE;
    VmaAllocation aoAllocation_ = VK_NULL_HANDLE;
    std::optional<vk::raii::Sampler> sampler_;

    // Compute pipeline
    std::optional<vk::raii::Pipeline> computePipeline_;
    std::optional<vk::raii::PipelineLayout> computePipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::vector<VkDescriptorSet> descriptorSets_;
};
