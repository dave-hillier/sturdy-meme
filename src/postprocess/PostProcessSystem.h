#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <array>
#include <memory>
#include <optional>
#include "UBOs.h"
#include "BufferUtils.h"
#include "DescriptorManager.h"
#include "InitContext.h"
#include "VmaResources.h"
#include <vulkan/vulkan_raii.hpp>
#include "interfaces/IPostProcessState.h"

// Forward declarations
class BloomSystem;
class BilateralGridSystem;

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

class PostProcessSystem : public IPostProcessState {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit PostProcessSystem(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkRenderPass outputRenderPass;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkExtent2D extent;
        VkFormat swapchainFormat;
        std::string shaderPath;
        uint32_t framesInFlight;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    /**
     * Factory: Create and initialize PostProcessSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<PostProcessSystem> create(const InitInfo& info);
    static std::unique_ptr<PostProcessSystem> create(const InitContext& ctx, VkRenderPass outputRenderPass, VkFormat swapchainFormat);


    /**
     * Bundle of all post-processing related systems
     */
    struct Bundle {
        std::unique_ptr<PostProcessSystem> postProcess;
        std::unique_ptr<BloomSystem> bloom;
        std::unique_ptr<BilateralGridSystem> bilateralGrid;
    };

    /**
     * Factory: Create PostProcessSystem with all dependencies (Bloom, BilateralGrid).
     * Systems are already wired together. Returns nullopt on failure.
     */
    static std::optional<Bundle> createWithDependencies(
        const InitContext& ctx,
        VkRenderPass finalRenderPass,
        VkFormat swapchainImageFormat
    );

    ~PostProcessSystem();

    // Non-copyable, non-movable (stored via unique_ptr only)
    PostProcessSystem(PostProcessSystem&&) = delete;
    PostProcessSystem& operator=(PostProcessSystem&&) = delete;
    PostProcessSystem(const PostProcessSystem&) = delete;
    PostProcessSystem& operator=(const PostProcessSystem&) = delete;

    void resize(VkExtent2D newExtent);

    // Render target accessors (vulkan-hpp)
    vk::ImageView getHDRColorView() const { return vk::ImageView(hdrColorView); }
    vk::ImageView getHDRDepthView() const { return vk::ImageView(hdrDepthView); }
    vk::RenderPass getHDRRenderPass() const { return vk::RenderPass(hdrRenderPass); }
    vk::Framebuffer getHDRFramebuffer() const { return vk::Framebuffer(hdrFramebuffer); }
    vk::Extent2D getRenderExtent() const { return vk::Extent2D{}.setWidth(extent.width).setHeight(extent.height); }

    // Legacy raw handle accessors (for existing code)
    VkExtent2D getExtent() const { return extent; }

    // Pre-end callback is called after post-process draw but before ending render pass (for GUI overlay)
    using PreEndCallback = std::function<void(VkCommandBuffer)>;
    void recordPostProcess(VkCommandBuffer cmd, uint32_t frameIndex,
                          VkFramebuffer swapchainFB, float deltaTime,
                          PreEndCallback preEndCallback = nullptr);

    // IPostProcessState exposure controls
    void setManualExposure(float ev) override { manualExposure = ev; }
    float getManualExposure() const override { return manualExposure; }
    void setAutoExposureEnabled(bool enabled) override { autoExposureEnabled = enabled; }
    bool isAutoExposureEnabled() const override { return autoExposureEnabled; }
    float getCurrentExposure() const override { return currentExposure; }

    // Legacy aliases for internal use
    void setExposure(float ev) { manualExposure = ev; }
    float getExposure() const { return manualExposure; }
    void setAutoExposure(bool enabled) { autoExposureEnabled = enabled; }

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
    void setGodRaysEnabled(bool enabled) override { godRaysEnabled = enabled; }
    bool isGodRaysEnabled() const override { return godRaysEnabled; }

    // God ray quality (sample count): 0=Low(16), 1=Medium(32), 2=High(64)
    enum class GodRayQuality { Low = 0, Medium = 1, High = 2 };
    void setGodRayQuality(int quality) override { setGodRayQuality(static_cast<GodRayQuality>(quality)); }
    int getGodRayQuality() const override { return static_cast<int>(godRayQuality); }
    void setGodRayQuality(GodRayQuality quality);
    GodRayQuality getGodRayQualityEnum() const { return godRayQuality; }

    // Froxel filter quality: false=trilinear (fast), true=tricubic (quality)
    void setFroxelFilterQuality(bool highQuality) override { froxelFilterHighQuality = highQuality; }
    bool isFroxelFilterHighQuality() const override { return froxelFilterHighQuality; }

    // Froxel debug visualization mode
    void setFroxelDebugMode(int mode) override { froxelDebugMode = glm::clamp(mode, 0, 6); }
    int getFroxelDebugMode() const override { return froxelDebugMode; }

    // Froxel volumetrics (Phase 4.3)
    void setFroxelVolume(VkImageView volumeView, VkSampler volumeSampler);
    void setFroxelEnabled(bool enabled) { froxelEnabled = enabled; }

    // Bloom (multi-pass)
    void setBloomTexture(VkImageView bloomView, VkSampler bloomSampler);
    void setBloomEnabled(bool enabled) override { bloomEnabled = enabled; }
    bool isBloomEnabled() const override { return bloomEnabled; }
    bool isFroxelEnabled() const { return froxelEnabled; }

    // Local tone mapping (bilateral grid) - Ghost of Tsushima technique
    void setBilateralGrid(VkImageView gridView, VkSampler gridSampler);
    // Quarter-resolution god rays texture (compute-based optimization)
    void setGodRaysTexture(VkImageView godRaysView, VkSampler godRaysSampler);
    void setLocalToneMapEnabled(bool enabled) override { localToneMapEnabled = enabled; }
    bool isLocalToneMapEnabled() const override { return localToneMapEnabled; }
    void setLocalToneMapContrast(float c) override { localToneMapContrast = glm::clamp(c, 0.0f, 1.0f); }
    float getLocalToneMapContrast() const override { return localToneMapContrast; }
    void setLocalToneMapDetail(float d) override { localToneMapDetail = glm::clamp(d, 0.5f, 2.0f); }
    float getLocalToneMapDetail() const override { return localToneMapDetail; }
    void setBilateralBlend(float b) override { bilateralBlend = glm::clamp(b, 0.0f, 1.0f); }
    float getBilateralBlend() const override { return bilateralBlend; }
    void setLocalToneMapLumRange(float minLog, float maxLog) {
        minLogLuminance = minLog;
        maxLogLuminance = maxLog;
    }

    // HDR tonemapping bypass (for comparison/debugging)
    void setHDREnabled(bool enabled) override { hdrEnabled = enabled; }
    bool isHDREnabled() const override { return hdrEnabled; }

    // HDR pass (whether to render to HDR target at all)
    void setHDRPassEnabled(bool enabled) override { hdrPassEnabled = enabled; }
    bool isHDRPassEnabled() const override { return hdrPassEnabled; }
    void setFroxelParams(float farPlane, float depthDist) {
        froxelFarPlane = farPlane;
        froxelDepthDist = depthDist;
    }
    void setCameraPlanes(float near, float far) { nearPlane = near; farPlane = far; }

    // =========================================================================
    // Water Volume Renderer - Underwater Effects (Phase 2)
    // =========================================================================

    // Set underwater state (called each frame based on camera position)
    void setUnderwaterState(bool underwater, float depth,
                            const glm::vec3& absorption, float turbidity,
                            const glm::vec4& waterColor, float waterLevel) {
        isUnderwater_ = underwater;
        underwaterDepth_ = depth;
        underwaterAbsorption_ = absorption;
        underwaterTurbidity_ = turbidity;
        underwaterColor_ = waterColor;
        underwaterWaterLevel_ = waterLevel;
    }

    // Individual setters for underwater parameters
    void setUnderwaterEnabled(bool enabled) { isUnderwater_ = enabled; }
    bool isUnderwater() const { return isUnderwater_; }
    void setUnderwaterDepth(float depth) { underwaterDepth_ = depth; }
    float getUnderwaterDepth() const { return underwaterDepth_; }
    void setUnderwaterAbsorption(const glm::vec3& absorption) { underwaterAbsorption_ = absorption; }
    glm::vec3 getUnderwaterAbsorption() const { return underwaterAbsorption_; }
    void setUnderwaterTurbidity(float turbidity) { underwaterTurbidity_ = turbidity; }
    float getUnderwaterTurbidity() const { return underwaterTurbidity_; }
    void setUnderwaterColor(const glm::vec4& color) { underwaterColor_ = color; }
    glm::vec4 getUnderwaterColor() const { return underwaterColor_; }
    void setUnderwaterWaterLevel(float level) { underwaterWaterLevel_ = level; }
    float getUnderwaterWaterLevel() const { return underwaterWaterLevel_; }

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

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

    const vk::raii::Device* raiiDevice_ = nullptr;
    std::optional<vk::raii::Sampler> hdrSampler_;
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
    bool hdrPassEnabled = true;  // HDR pass enabled by default
    bool bloomEnabled = true;  // Bloom enabled by default
    float froxelFarPlane = 200.0f;
    float froxelDepthDist = 1.2f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    int froxelDebugMode = 0;  // 0=Normal, 1=Depth slices, 2=Density, 3=Transmittance, 4=Grid cells

    // Local tone mapping (bilateral grid)
    VkImageView bilateralGridView = VK_NULL_HANDLE;
    VkSampler bilateralGridSampler = VK_NULL_HANDLE;
    bool localToneMapEnabled = false;  // Disabled by default

    // Quarter-resolution god rays (compute optimization)
    VkImageView godRaysView_ = VK_NULL_HANDLE;
    VkSampler godRaysSampler_ = VK_NULL_HANDLE;
    float localToneMapContrast = 0.5f; // 0=none, 0.5=typical, 1.0=flat
    float localToneMapDetail = 1.0f;   // 1.0=neutral, 1.5=punchy
    float bilateralBlend = 0.4f;       // GOT used 40% bilateral, 60% gaussian
    float minLogLuminance = -8.0f;
    float maxLogLuminance = 4.0f;

    // Underwater rendering parameters (Water Volume Renderer Phase 2)
    bool isUnderwater_ = false;                              // Is camera underwater
    float underwaterDepth_ = 0.0f;                           // Depth below water surface
    glm::vec3 underwaterAbsorption_ = glm::vec3(0.4f, 0.03f, 0.01f);  // Beer-Lambert absorption (default coastal)
    float underwaterTurbidity_ = 0.5f;                       // Scattering factor
    glm::vec4 underwaterColor_ = glm::vec4(0.1f, 0.3f, 0.4f, 0.8f);   // Water tint color
    float underwaterWaterLevel_ = 0.0f;                      // Water surface Y position

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
    ManagedBuffer histogramBuffer;

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
