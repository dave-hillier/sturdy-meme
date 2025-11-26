#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

// Atmosphere LUT system for physically-based sky rendering (Phase 4.1)
// Precomputes transmittance and multi-scatter LUTs for efficient atmospheric scattering

struct AtmosphereParams {
    // Planet geometry
    float planetRadius = 6371000.0f;       // Earth radius in meters
    float atmosphereRadius = 6471000.0f;   // Top of atmosphere

    // Rayleigh scattering (air molecules)
    glm::vec3 rayleighScatteringBase = glm::vec3(5.802e-6f, 13.558e-6f, 33.1e-6f);
    float rayleighScaleHeight = 8000.0f;   // Density falloff

    // Mie scattering (aerosols/haze)
    float mieScatteringBase = 3.996e-6f;
    float mieAbsorptionBase = 4.4e-6f;
    float mieScaleHeight = 1200.0f;
    float mieAnisotropy = 0.8f;            // Phase function asymmetry

    // Ozone absorption (affects blue channel at horizon)
    glm::vec3 ozoneAbsorption = glm::vec3(0.65e-6f, 1.881e-6f, 0.085e-6f);
    float ozoneLayerCenter = 25000.0f;     // meters
    float ozoneLayerWidth = 15000.0f;

    // Sun
    float sunAngularRadius = 0.00935f / 2.0f;  // radians
    glm::vec3 solarIrradiance = glm::vec3(1.474f, 1.8504f, 1.91198f);  // W/m²
};

struct AtmosphereLUTUniforms {
    AtmosphereParams params;
    glm::vec4 sunDirection;  // xyz = sun dir, w = unused
    glm::vec4 cameraPosition; // xyz = camera pos, w = camera altitude
    float padding[2];
};

class AtmosphereLUTSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkDescriptorPool descriptorPool;
        std::string shaderPath;
        uint32_t framesInFlight;
    };

    // LUT dimensions (from Phase 4.1)
    static constexpr uint32_t TRANSMITTANCE_WIDTH = 256;
    static constexpr uint32_t TRANSMITTANCE_HEIGHT = 64;
    static constexpr uint32_t MULTISCATTER_SIZE = 32;
    static constexpr uint32_t SKYVIEW_WIDTH = 192;
    static constexpr uint32_t SKYVIEW_HEIGHT = 108;

    AtmosphereLUTSystem() = default;
    ~AtmosphereLUTSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Compute LUTs (called at startup and when atmosphere parameters change)
    void computeTransmittanceLUT(VkCommandBuffer cmd);
    void computeMultiScatterLUT(VkCommandBuffer cmd);
    void computeSkyViewLUT(VkCommandBuffer cmd, const glm::vec3& sunDir, const glm::vec3& cameraPos, float cameraAltitude);

    // Get LUT views for sampling in shaders
    VkImageView getTransmittanceLUTView() const { return transmittanceLUTView; }
    VkImageView getMultiScatterLUTView() const { return multiScatterLUTView; }
    VkImageView getSkyViewLUTView() const { return skyViewLUTView; }
    VkSampler getLUTSampler() const { return lutSampler; }

    // Export LUTs as PNG files (for debugging/visualization)
    bool exportLUTsAsPNG(const std::string& outputDir);

    // Atmosphere parameters
    void setAtmosphereParams(const AtmosphereParams& params) { atmosphereParams = params; }
    const AtmosphereParams& getAtmosphereParams() const { return atmosphereParams; }

private:
    bool createTransmittanceLUT();
    bool createMultiScatterLUT();
    bool createSkyViewLUT();
    bool createLUTSampler();
    bool createDescriptorSetLayouts();
    bool createDescriptorSets();
    bool createUniformBuffer();
    bool createComputePipelines();

    void destroyLUTResources();

    // Helper to export a 2D image to PNG
    bool exportImageToPNG(VkImage image, VkFormat format, uint32_t width, uint32_t height, const std::string& filename);

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    // Transmittance LUT (256×64, RGBA16F)
    VkImage transmittanceLUT = VK_NULL_HANDLE;
    VmaAllocation transmittanceLUTAllocation = VK_NULL_HANDLE;
    VkImageView transmittanceLUTView = VK_NULL_HANDLE;

    // Multi-scatter LUT (32×32, RG16F)
    VkImage multiScatterLUT = VK_NULL_HANDLE;
    VmaAllocation multiScatterLUTAllocation = VK_NULL_HANDLE;
    VkImageView multiScatterLUTView = VK_NULL_HANDLE;

    // Sky-View LUT (192×108, RGBA16F)
    VkImage skyViewLUT = VK_NULL_HANDLE;
    VmaAllocation skyViewLUTAllocation = VK_NULL_HANDLE;
    VkImageView skyViewLUTView = VK_NULL_HANDLE;

    // LUT sampler (bilinear filtering, clamp to edge)
    VkSampler lutSampler = VK_NULL_HANDLE;

    // Compute pipelines
    VkDescriptorSetLayout transmittanceDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout multiScatterDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skyViewDescriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout transmittancePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout multiScatterPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout skyViewPipelineLayout = VK_NULL_HANDLE;

    VkPipeline transmittancePipeline = VK_NULL_HANDLE;
    VkPipeline multiScatterPipeline = VK_NULL_HANDLE;
    VkPipeline skyViewPipeline = VK_NULL_HANDLE;

    VkDescriptorSet transmittanceDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet multiScatterDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet skyViewDescriptorSet = VK_NULL_HANDLE;

    // Uniform buffer
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VmaAllocation uniformAllocation = VK_NULL_HANDLE;
    void* uniformMappedPtr = nullptr;

    // Atmosphere parameters
    AtmosphereParams atmosphereParams;
};
