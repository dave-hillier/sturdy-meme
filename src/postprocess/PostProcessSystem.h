#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <array>
#include "UBOs.h"
#include "BufferUtils.h"
#include "DescriptorManager.h"
#include "InitContext.h"

// Histogram reduce compute shader parameters
struct HistogramReduceParams {
    float minLogLum;      // Minimum log luminance
    float maxLogLum;      // Maximum log luminance
    float invLogLumRange; // 1.0 / (maxLogLum - minLogLum)
    uint32_t pixelCount;  // Total pixel count
    float lowPercentile;  // Ignore darkest N% (e.g., 0.4 = 40%)
    float highPercentile; // Ignore brightest N% (e.g., 0.95 = keep up to 95%)
    float targetLuminance;// Target middle gray (0.18)
    float deltaTime;      // Frame delta time for temporal adaptation
    float adaptSpeedUp;   // Adaptation speed when brightening
    float adaptSpeedDown; // Adaptation speed when darkening
    float minExposure;    // Minimum exposure EV
    float maxExposure;    // Maximum exposure EV
};

// Exposure buffer structure (matches shader)
struct ExposureData {
    float averageLuminance;
    float exposureValue;
    float previousExposure;
    float adaptedExposure;
};

class PostProcessSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkRenderPass outputRenderPass;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkExtent2D extent;
        VkFormat swapchainFormat;
        std::string shaderPath;
        uint32_t framesInFlight;
    };

    PostProcessSystem() = default;
    ~PostProcessSystem() = default;

    bool init(const InitInfo& info);
    bool init(const InitContext& ctx, VkRenderPass outputRenderPass, VkFormat swapchainFormat);
    void destroy(VkDevice device, VmaAllocator allocator);
    void resize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent);

    VkRenderPass getHDRRenderPass() const { return hdrRenderPass; }
    VkFramebuffer getHDRFramebuffer() const { return hdrFramebuffer; }
    VkImageView getHDRColorView() const { return hdrColorView; }
    VkImageView getHDRDepthView() const { return hdrDepthView; }
    VkExtent2D getExtent() const { return extent; }

    // Pre-end callback is called after post-process draw but before ending render pass (for GUI overlay)
    using PreEndCallback = std::function<void(VkCommandBuffer)>;
    void recordPostProcess(VkCommandBuffer cmd, uint32_t frameIndex,
                          VkFramebuffer swapchainFB, float deltaTime,
                          PreEndCallback preEndCallback = nullptr);

    void setExposure(float ev) { manualExposure = ev; }
    float getExposure() const { return manualExposure; }
    void setAutoExposure(bool enabled) { autoExposureEnabled = enabled; }
    bool isAutoExposureEnabled() const { return autoExposureEnabled; }
    float getCurrentExposure() const { return currentExposure; }

    void setBloomThreshold(float t) { bloomThreshold = t; }
    float getBloomThreshold() const { return bloomThreshold; }
    void setBloomIntensity(float i) { bloomIntensity = i; }
    float getBloomIntensity() const { return bloomIntensity; }
    void setBloomRadius(float r) { bloomRadius = r; }
    float getBloomRadius() const { return bloomRadius; }

    // God rays (Phase 4.4)
    void setSunScreenPos(glm::vec2 pos) { sunScreenPos = pos; }
    glm::vec2 getSunScreenPos() const { return sunScreenPos; }
    void setGodRayIntensity(float i) { godRayIntensity = i; }
    float getGodRayIntensity() const { return godRayIntensity; }
    void setGodRayDecay(float d) { godRayDecay = d; }
    float getGodRayDecay() const { return godRayDecay; }
    void setGodRaysEnabled(bool enabled) { godRaysEnabled = enabled; }
    bool isGodRaysEnabled() const { return godRaysEnabled; }

    // God ray quality (sample count): 0=Low(16), 1=Medium(32), 2=High(64)
    enum class GodRayQuality { Low = 0, Medium = 1, High = 2 };
    void setGodRayQuality(GodRayQuality quality);
    GodRayQuality getGodRayQuality() const { return godRayQuality; }

    // Froxel filter quality: false=trilinear (fast), true=tricubic (quality)
    void setFroxelFilterQuality(bool highQuality) { froxelFilterHighQuality = highQuality; }
    bool isFroxelFilterHighQuality() const { return froxelFilterHighQuality; }

    // Froxel volumetrics (Phase 4.3)
    void setFroxelVolume(VkImageView volumeView, VkSampler volumeSampler);
    void setFroxelEnabled(bool enabled) { froxelEnabled = enabled; }

    // Bloom (multi-pass)
    void setBloomTexture(VkImageView bloomView, VkSampler bloomSampler);
    bool isFroxelEnabled() const { return froxelEnabled; }

    // HDR tonemapping bypass (for comparison/debugging)
    void setHDREnabled(bool enabled) { hdrEnabled = enabled; }
    bool isHDREnabled() const { return hdrEnabled; }
    void setFroxelParams(float farPlane, float depthDist) {
        froxelFarPlane = farPlane;
        froxelDepthDist = depthDist;
    }
    void setCameraPlanes(float near, float far) { nearPlane = near; farPlane = far; }

private:
    bool createHDRRenderTarget();
    bool createHDRRenderPass();
    bool createHDRFramebuffer();
    bool createSampler();
    bool createDescriptorSetLayout();
    bool createDescriptorSets();
    bool createUniformBuffers();
    bool createCompositePipeline();

    // Histogram-based exposure
    bool createHistogramResources();
    bool createHistogramPipelines();
    bool createHistogramDescriptorSets();
    void destroyHistogramResources();
    void recordHistogramCompute(VkCommandBuffer cmd, uint32_t frameIndex, float deltaTime);

    // Synchronize histogram build output for reduce pass
    void barrierHistogramBuildToReduce(VkCommandBuffer cmd);

    // Synchronize histogram reduce output for CPU read and HDR image for sampling
    void barrierHistogramReduceComplete(VkCommandBuffer cmd, uint32_t frameIndex);

    void destroyHDRResources();

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkRenderPass outputRenderPass = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    static constexpr VkFormat HDR_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

    // HDR render target
    VkImage hdrColorImage = VK_NULL_HANDLE;
    VmaAllocation hdrColorAllocation = VK_NULL_HANDLE;
    VkImageView hdrColorView = VK_NULL_HANDLE;

    VkImage hdrDepthImage = VK_NULL_HANDLE;
    VmaAllocation hdrDepthAllocation = VK_NULL_HANDLE;
    VkImageView hdrDepthView = VK_NULL_HANDLE;

    VkSampler hdrSampler = VK_NULL_HANDLE;
    VkRenderPass hdrRenderPass = VK_NULL_HANDLE;
    VkFramebuffer hdrFramebuffer = VK_NULL_HANDLE;

    // Final composite pipeline
    VkDescriptorSetLayout compositeDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout compositePipelineLayout = VK_NULL_HANDLE;
    // Pipeline variants for different god ray sample counts
    // Index 0=Low(16), 1=Medium(32), 2=High(64)
    std::array<VkPipeline, 3> compositePipelines = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    std::vector<VkDescriptorSet> compositeDescriptorSets;

    // Uniform buffers (per frame)
    BufferUtils::PerFrameBufferSet uniformBuffers;

    // Exposure control
    float manualExposure = 0.0f;
    bool autoExposureEnabled = true;  // Enabled - histogram compute shader approach is stable
    float currentExposure = 0.0f;
    float lastAutoExposure = 0.0f;  // For temporal smoothing
    float adaptedLuminance = 0.18f;  // Middle gray target

    // Bloom parameters
    float bloomThreshold = 0.8f;   // Brightness threshold for bloom
    float bloomIntensity = 0.7f;   // Bloom strength
    float bloomRadius = 4.0f;      // Bloom sample radius

    // God ray parameters (Phase 4.4)
    glm::vec2 sunScreenPos = glm::vec2(0.5f, 0.5f);  // Default to center
    float godRayIntensity = 0.25f;  // God ray strength (subtle)
    float godRayDecay = 0.92f;      // Falloff per sample (faster falloff = less extreme)
    bool godRaysEnabled = true;     // Enable/disable god rays
    GodRayQuality godRayQuality = GodRayQuality::High;  // Sample count quality level
    bool froxelFilterHighQuality = true;  // Tricubic (true) vs trilinear (false)

    // Froxel volumetrics (Phase 4.3)
    VkImageView froxelVolumeView = VK_NULL_HANDLE;
    VkSampler froxelSampler = VK_NULL_HANDLE;
    VkImageView bloomView = VK_NULL_HANDLE;
    VkSampler bloomSampler = VK_NULL_HANDLE;
    bool froxelEnabled = false;
    bool hdrEnabled = true;  // HDR tonemapping enabled by default
    float froxelFarPlane = 200.0f;
    float froxelDepthDist = 1.2f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    // Auto-exposure parameters
    static constexpr float MIN_EXPOSURE = -4.0f;  // EV (darkening limit)
    static constexpr float MAX_EXPOSURE = 4.0f;   // EV (brightening limit)
    static constexpr float ADAPTATION_SPEED_UP = 2.0f;    // Faster brightening
    static constexpr float ADAPTATION_SPEED_DOWN = 1.0f;  // Slower darkening
    static constexpr float TARGET_LUMINANCE = 0.18f;      // Standard middle gray
    static constexpr float MIN_LOG_LUMINANCE = -8.0f;     // Log2 of minimum luminance
    static constexpr float MAX_LOG_LUMINANCE = 4.0f;      // Log2 of maximum luminance
    static constexpr float LOW_PERCENTILE = 0.05f;        // Include most dark pixels (only ignore 5%)
    static constexpr float HIGH_PERCENTILE = 0.95f;       // Ignore brightest 5%
    static constexpr uint32_t HISTOGRAM_BINS = 256;

    // Histogram-based exposure resources
    VkBuffer histogramBuffer = VK_NULL_HANDLE;
    VmaAllocation histogramAllocation = VK_NULL_HANDLE;

    BufferUtils::PerFrameBufferSet exposureBuffers;  // Per-frame exposure output

    BufferUtils::PerFrameBufferSet histogramParamsBuffers;  // Per-frame histogram params

    // Histogram compute pipelines
    VkDescriptorSetLayout histogramBuildDescLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout histogramReduceDescLayout = VK_NULL_HANDLE;
    VkPipelineLayout histogramBuildPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout histogramReducePipelineLayout = VK_NULL_HANDLE;
    VkPipeline histogramBuildPipeline = VK_NULL_HANDLE;
    VkPipeline histogramReducePipeline = VK_NULL_HANDLE;

    std::vector<VkDescriptorSet> histogramBuildDescSets;
    std::vector<VkDescriptorSet> histogramReduceDescSets;

    float calculateAverageLuminance();
};
