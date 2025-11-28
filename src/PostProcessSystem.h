#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

struct PostProcessUniforms {
    float exposure;
    float bloomThreshold;
    float bloomIntensity;
    float autoExposure;  // 0 = manual, 1 = auto (histogram-based)
    float previousExposure;
    float deltaTime;
    float adaptationSpeed;
    float bloomRadius;
    // God rays parameters (Phase 4.4)
    glm::vec2 sunScreenPos;   // Sun position in screen space [0,1]
    float godRayIntensity;    // God ray strength
    float godRayDecay;        // Falloff from sun position
    // Froxel volumetrics (Phase 4.3)
    float froxelEnabled;      // 1.0 = enabled, 0.0 = disabled
    float froxelFarPlane;     // Volumetric far plane
    float froxelDepthDist;    // Depth distribution factor
    float nearPlane;          // Camera near plane for depth linearization
    float farPlane;           // Camera far plane for depth linearization
    float padding1;
    float padding2;
    float padding3;
};

// Histogram build compute shader parameters
struct HistogramBuildParams {
    float minLogLum;      // Minimum log luminance (e.g., -8.0)
    float maxLogLum;      // Maximum log luminance (e.g., 4.0)
    float invLogLumRange; // 1.0 / (maxLogLum - minLogLum)
    uint32_t pixelCount;  // Total pixel count for normalization
};

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
        VkDescriptorPool descriptorPool;
        VkExtent2D extent;
        VkFormat swapchainFormat;
        std::string shaderPath;
        uint32_t framesInFlight;
    };

    PostProcessSystem() = default;
    ~PostProcessSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);
    void resize(VkDevice device, VmaAllocator allocator, VkExtent2D newExtent);

    VkRenderPass getHDRRenderPass() const { return hdrRenderPass; }
    VkFramebuffer getHDRFramebuffer() const { return hdrFramebuffer; }
    VkImageView getHDRColorView() const { return hdrColorView; }
    VkImageView getHDRDepthView() const { return hdrDepthView; }
    VkExtent2D getExtent() const { return extent; }

    void recordPostProcess(VkCommandBuffer cmd, uint32_t frameIndex,
                          VkFramebuffer swapchainFB, float deltaTime);

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

    // Froxel volumetrics (Phase 4.3)
    void setFroxelVolume(VkImageView volumeView, VkSampler volumeSampler);
    void setFroxelEnabled(bool enabled) { froxelEnabled = enabled; }

    // Bloom (multi-pass)
    void setBloomTexture(VkImageView bloomView, VkSampler bloomSampler);
    bool isFroxelEnabled() const { return froxelEnabled; }
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

    void destroyHDRResources();

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
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
    VkPipeline compositePipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> compositeDescriptorSets;

    // Uniform buffers (per frame)
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformAllocations;
    std::vector<void*> uniformMappedPtrs;

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

    // Froxel volumetrics (Phase 4.3)
    VkImageView froxelVolumeView = VK_NULL_HANDLE;
    VkSampler froxelSampler = VK_NULL_HANDLE;
    bool froxelEnabled = false;
    float froxelFarPlane = 200.0f;
    float froxelDepthDist = 1.2f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    // Auto-exposure parameters
    static constexpr float MIN_EXPOSURE = -4.0f;  // EV
    static constexpr float MAX_EXPOSURE = 0.0f;   // EV (no auto-brightening - preserve dark nights)
    static constexpr float ADAPTATION_SPEED_UP = 2.0f;    // Faster brightening
    static constexpr float ADAPTATION_SPEED_DOWN = 1.0f;  // Slower darkening
    static constexpr float TARGET_LUMINANCE = 0.05f;      // Dark target - preserve night atmosphere
    static constexpr float MIN_LOG_LUMINANCE = -8.0f;     // Log2 of minimum luminance
    static constexpr float MAX_LOG_LUMINANCE = 4.0f;      // Log2 of maximum luminance
    static constexpr float LOW_PERCENTILE = 0.05f;        // Include most dark pixels (only ignore 5%)
    static constexpr float HIGH_PERCENTILE = 0.95f;       // Ignore brightest 5%
    static constexpr uint32_t HISTOGRAM_BINS = 256;

    // Histogram-based exposure resources
    VkBuffer histogramBuffer = VK_NULL_HANDLE;
    VmaAllocation histogramAllocation = VK_NULL_HANDLE;

    std::vector<VkBuffer> exposureBuffers;          // Per-frame exposure output
    std::vector<VmaAllocation> exposureAllocations;
    std::vector<void*> exposureMappedPtrs;

    std::vector<VkBuffer> histogramParamsBuffers;     // Per-frame histogram params
    std::vector<VmaAllocation> histogramParamsAllocations;
    std::vector<void*> histogramParamsMappedPtrs;

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
