#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

struct PostProcessUniforms {
    glm::vec4 exposureParams;    // x: manual exposure, y: auto flag, z: previous exposure, w: delta time
    glm::vec4 histogramParams;   // x: adaptation speed, y: low percentile, z: high percentile, w: exposure bias
    glm::vec4 bloomParams;       // x: threshold, y: intensity, z: radius, w: unused
    glm::vec4 colorLift;         // xyz: lift, w: unused
    glm::vec4 colorGamma;        // xyz: gamma, w: saturation
    glm::vec4 colorGain;         // xyz: gain, w: Purkinje strength
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

    void setExposureCompensation(float bias) { exposureCompensation = bias; }
    void setHistogramPercentiles(float low, float high) {
        histogramLowPercentile = low;
        histogramHighPercentile = high;
    }
    void setColorLift(const glm::vec3& value) { colorLift = value; }
    void setColorGamma(const glm::vec3& value) { colorGamma = value; }
    void setColorGain(const glm::vec3& value) { colorGain = value; }
    void setSaturation(float value) { saturation = value; }
    void setPurkinjeStrength(float value) { purkinjeStrength = value; }

private:
    bool createHDRRenderTarget();
    bool createHDRRenderPass();
    bool createHDRFramebuffer();
    bool createSampler();
    bool createDescriptorSetLayout();
    bool createDescriptorSets();
    bool createUniformBuffers();
    bool createCompositePipeline();

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
    bool autoExposureEnabled = false;  // Disabled - fragment shader approach causes flickering
    float currentExposure = 0.0f;
    float lastAutoExposure = 0.0f;  // For temporal smoothing
    float adaptedLuminance = 0.18f;  // Middle gray target

    // Bloom parameters
    float bloomThreshold = 1.0f;   // Brightness threshold for bloom
    float bloomIntensity = 0.3f;   // Bloom strength
    float bloomRadius = 4.0f;      // Bloom sample radius

    // Histogram and exposure shaping
    float exposureCompensation = 0.0f;  // Extra EV offset
    float histogramLowPercentile = 0.35f;
    float histogramHighPercentile = 0.9f;

    // Color grading
    glm::vec3 colorLift = glm::vec3(0.0f);
    glm::vec3 colorGamma = glm::vec3(1.1f);
    glm::vec3 colorGain = glm::vec3(1.0f);
    float saturation = 1.0f;
    float purkinjeStrength = 0.65f;

    // Auto-exposure parameters
    static constexpr float MIN_EXPOSURE = -4.0f;  // EV
    static constexpr float MAX_EXPOSURE = 4.0f;   // EV
    static constexpr float ADAPTATION_SPEED_UP = 2.0f;    // Faster brightening
    static constexpr float ADAPTATION_SPEED_DOWN = 1.0f;  // Slower darkening
    static constexpr float TARGET_LUMINANCE = 0.18f;      // Middle gray

    float calculateAverageLuminance();
};
