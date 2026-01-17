#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <string>
#include <memory>
#include <optional>
#include <glm/glm.hpp>
#include "DescriptorManager.h"
#include "InitContext.h"

/**
 * Quarter-resolution god rays system.
 *
 * Renders light shafts at 1/4 resolution (1/16th the pixels) for massive
 * performance improvement while maintaining visual quality.
 *
 * The expensive radial blur loop with 32-64 samples per pixel now runs
 * on far fewer pixels, then the result is bilinearly upsampled.
 */
class GodRaysSystem {
public:
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit GodRaysSystem(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    static std::unique_ptr<GodRaysSystem> create(const InitInfo& info);
    static std::unique_ptr<GodRaysSystem> create(const InitContext& ctx);

    ~GodRaysSystem();

    GodRaysSystem(GodRaysSystem&&) = delete;
    GodRaysSystem& operator=(GodRaysSystem&&) = delete;
    GodRaysSystem(const GodRaysSystem&) = delete;
    GodRaysSystem& operator=(const GodRaysSystem&) = delete;

    void resize(VkExtent2D newExtent);

    /**
     * Record god rays compute pass.
     * @param cmd Command buffer (outside render pass)
     * @param hdrView HDR input image view
     * @param depthView Depth buffer image view
     */
    void recordGodRaysPass(VkCommandBuffer cmd, VkImageView hdrView, VkImageView depthView);

    VkImageView getGodRaysOutput() const { return outputImageView_; }
    VkSampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    // Parameters
    void setSunScreenPos(glm::vec2 pos) { sunScreenPos_ = pos; }
    void setIntensity(float i) { intensity_ = i; }
    void setDecay(float d) { decay_ = d; }
    void setBloomThreshold(float t) { bloomThreshold_ = t; }
    void setSampleCount(int c) { sampleCount_ = c; }
    void setNearFarPlanes(float near, float far) { nearPlane_ = near; farPlane_ = far; }

    float getIntensity() const { return intensity_; }
    float getDecay() const { return decay_; }
    int getSampleCount() const { return sampleCount_; }

private:
    bool initInternal(const InitInfo& info);
    void cleanup();
    bool createResources();
    bool createPipeline();
    bool createDescriptorSets();
    void destroyResources();

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    VkExtent2D extent_ = {0, 0};
    std::string shaderPath_;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Quarter-resolution output
    VkImage outputImage_ = VK_NULL_HANDLE;
    VmaAllocation outputAllocation_ = VK_NULL_HANDLE;
    VkImageView outputImageView_ = VK_NULL_HANDLE;
    VkExtent2D quarterExtent_ = {0, 0};

    std::optional<vk::raii::Sampler> sampler_;
    std::optional<vk::raii::DescriptorSetLayout> descSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;
    VkDescriptorSet descSet_ = VK_NULL_HANDLE;

    // Parameters
    glm::vec2 sunScreenPos_ = glm::vec2(0.5f, 0.5f);
    float intensity_ = 0.5f;
    float decay_ = 0.96f;
    float bloomThreshold_ = 1.0f;
    int sampleCount_ = 32;
    float nearPlane_ = 0.1f;
    float farPlane_ = 1000.0f;

    struct PushConstants {
        float sunScreenPosX;
        float sunScreenPosY;
        float intensity;
        float decay;
        float nearPlane;
        float farPlane;
        float bloomThreshold;
        int sampleCount;
    };
};
