#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "UBOs.h"
#include "PerFrameBuffer.h"
#include "DescriptorManager.h"
#include "InitContext.h"

// Bilateral Grid Local Tone Mapping System
// Ghost of Tsushima technique for detail-preserving contrast adjustment
//
// The bilateral grid is a 3D data structure indexed by (x, y, log_luminance)
// that enables efficient edge-aware filtering for local tone mapping.
//
// Pipeline:
// 1. Build pass: Splat log-luminance values into 3D grid
// 2. Blur passes: Apply separable Gaussian blur (X, Y, Z axes)
// 3. Sample pass: PostProcess shader samples blurred grid for local contrast

class BilateralGridSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit BilateralGridSystem(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Grid dimensions (GOT used 64x32x64)
    static constexpr uint32_t GRID_WIDTH = 64;
    static constexpr uint32_t GRID_HEIGHT = 32;
    static constexpr uint32_t GRID_DEPTH = 64;  // Luminance bins

    // Log luminance range
    static constexpr float MIN_LOG_LUMINANCE = -8.0f;  // Very dark
    static constexpr float MAX_LOG_LUMINANCE = 4.0f;   // Very bright

    /**
     * Factory: Create and initialize BilateralGridSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<BilateralGridSystem> create(const InitInfo& info);
    static std::unique_ptr<BilateralGridSystem> create(const InitContext& ctx);

    ~BilateralGridSystem();

    // Non-copyable, non-movable
    BilateralGridSystem(const BilateralGridSystem&) = delete;
    BilateralGridSystem& operator=(const BilateralGridSystem&) = delete;
    BilateralGridSystem(BilateralGridSystem&&) = delete;
    BilateralGridSystem& operator=(BilateralGridSystem&&) = delete;

    void resize(VkExtent2D newExtent);

    // Record bilateral grid compute passes
    // Call before post-process pass
    void recordBilateralGrid(VkCommandBuffer cmd, uint32_t frameIndex,
                             VkImageView hdrInputView);

    // Get the blurred grid for sampling in post-process
    VkImageView getGridView() const { return gridViews[0]; }
    VkSampler getGridSampler() const { return gridSampler_ ? **gridSampler_ : VK_NULL_HANDLE; }

    // Local tone mapping parameters
    void setEnabled(bool e) { enabled = e; }
    bool isEnabled() const { return enabled; }

    void setContrast(float c) { contrast = glm::clamp(c, 0.0f, 1.0f); }
    float getContrast() const { return contrast; }

    void setDetail(float d) { detail = glm::clamp(d, 0.5f, 2.0f); }
    float getDetail() const { return detail; }

    // Blend between bilateral filter and wide Gaussian (GOT used 0.4 bilateral)
    void setBilateralBlend(float b) { bilateralBlend = glm::clamp(b, 0.0f, 1.0f); }
    float getBilateralBlend() const { return bilateralBlend; }

    // Get parameters for postprocess shader
    float getMinLogLuminance() const { return MIN_LOG_LUMINANCE; }
    float getMaxLogLuminance() const { return MAX_LOG_LUMINANCE; }


private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createGridTextures();
    bool createSampler();
    bool createDescriptorSetLayout();
    bool createDescriptorSets();
    bool createUniformBuffers();
    bool createBuildPipeline();
    bool createBlurPipeline();

    void destroyGridResources();

    // Clear grid before each frame
    void recordClearGrid(VkCommandBuffer cmd);

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Format for bilateral grid (stores weighted log-lum + weight)
    static constexpr VkFormat GRID_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

    // Ping-pong grids for blur passes
    VkImage gridImages[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation gridAllocations[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView gridViews[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};

    std::optional<vk::raii::Sampler> gridSampler_;

    // Build pipeline (populates grid from HDR image)
    std::optional<vk::raii::DescriptorSetLayout> buildDescriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> buildPipelineLayout_;
    std::optional<vk::raii::Pipeline> buildPipeline_;
    std::vector<VkDescriptorSet> buildDescriptorSets;

    // Blur pipeline (separable Gaussian along each axis)
    std::optional<vk::raii::DescriptorSetLayout> blurDescriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> blurPipelineLayout_;
    std::optional<vk::raii::Pipeline> blurPipeline_;
    std::vector<VkDescriptorSet> blurDescriptorSetsX;  // X-axis blur
    std::vector<VkDescriptorSet> blurDescriptorSetsY;  // Y-axis blur
    std::vector<VkDescriptorSet> blurDescriptorSetsZ;  // Z-axis blur

    // Uniform buffers
    BufferUtils::PerFrameBufferSet buildUniformBuffers;
    BufferUtils::PerFrameBufferSet blurUniformBuffers;

    // Parameters
    bool enabled = true;
    float contrast = 0.5f;       // Contrast reduction (0=none, 0.5=typical, 1.0=flat)
    float detail = 1.0f;         // Detail boost (1.0=neutral, 1.5=punchy)
    float bilateralBlend = 0.4f; // GOT used 40% bilateral, 60% gaussian
};
