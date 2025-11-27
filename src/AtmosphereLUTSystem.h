#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

// Atmosphere LUT system for physically-based sky rendering (Phase 4.1)
// Precomputes transmittance and multi-scatter LUTs for efficient atmospheric scattering

// Atmosphere parameters - layout must match GLSL std140 (see atmosphere_common.glsl)
struct AtmosphereParams {
    // Planet geometry (in kilometers to match sky.frag)
    float planetRadius = 6371.0f;          // Earth radius in km
    float atmosphereRadius = 6471.0f;      // Top of atmosphere in km
    float pad1 = 0.0f, pad2 = 0.0f;        // Padding to align vec3 to 16 bytes

    // Rayleigh scattering (air molecules) - per km coefficients
    glm::vec3 rayleighScatteringBase = glm::vec3(5.802e-3f, 13.558e-3f, 33.1e-3f);
    float rayleighScaleHeight = 8.0f;      // km

    // Mie scattering (aerosols/haze) - per km coefficients
    float mieScatteringBase = 3.996e-3f;
    float mieAbsorptionBase = 4.4e-3f;
    float mieScaleHeight = 1.2f;           // km
    float mieAnisotropy = 0.8f;            // Phase function asymmetry

    // Ozone absorption (affects blue channel at horizon) - per km
    glm::vec3 ozoneAbsorption = glm::vec3(0.65e-3f, 1.881e-3f, 0.085e-3f);
    float ozoneLayerCenter = 25.0f;        // km

    float ozoneLayerWidth = 15.0f;         // km
    float sunAngularRadius = 0.00935f / 2.0f;  // radians
    float pad3 = 0.0f, pad4 = 0.0f;        // Padding to align vec3 to 16 bytes

    glm::vec3 solarIrradiance = glm::vec3(1.474f, 1.8504f, 1.91198f);
    float pad5 = 0.0f;                     // Padding for struct alignment
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
    // Irradiance LUT dimensions (Phase 4.1.9)
    // Indexed by: altitude (Y) and sun zenith cosine (X)
    static constexpr uint32_t IRRADIANCE_WIDTH = 64;   // cos(sun zenith)
    static constexpr uint32_t IRRADIANCE_HEIGHT = 16;  // altitude

    AtmosphereLUTSystem() = default;
    ~AtmosphereLUTSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Compute LUTs (called at startup and when atmosphere parameters change)
    void computeTransmittanceLUT(VkCommandBuffer cmd);
    void computeMultiScatterLUT(VkCommandBuffer cmd);
    void computeIrradianceLUT(VkCommandBuffer cmd);
    void computeSkyViewLUT(VkCommandBuffer cmd, const glm::vec3& sunDir, const glm::vec3& cameraPos, float cameraAltitude);

    // Update sky-view LUT per frame (uses SHADER_READ_ONLY_OPTIMAL as old layout since LUT was already computed)
    void updateSkyViewLUT(VkCommandBuffer cmd, const glm::vec3& sunDir, const glm::vec3& cameraPos, float cameraAltitude);

    // Get LUT views for sampling in shaders
    VkImageView getTransmittanceLUTView() const { return transmittanceLUTView; }
    VkImageView getMultiScatterLUTView() const { return multiScatterLUTView; }
    VkImageView getSkyViewLUTView() const { return skyViewLUTView; }
    VkImageView getRayleighIrradianceLUTView() const { return rayleighIrradianceLUTView; }
    VkImageView getMieIrradianceLUTView() const { return mieIrradianceLUTView; }
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
    bool createIrradianceLUTs();
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

    // Rayleigh Irradiance LUT (64×16, RGBA16F) - Phase 4.1.9
    // Stores scattered Rayleigh light *before* phase function multiplication
    VkImage rayleighIrradianceLUT = VK_NULL_HANDLE;
    VmaAllocation rayleighIrradianceLUTAllocation = VK_NULL_HANDLE;
    VkImageView rayleighIrradianceLUTView = VK_NULL_HANDLE;

    // Mie Irradiance LUT (64×16, RGBA16F) - Phase 4.1.9
    // Stores scattered Mie light *before* phase function multiplication
    VkImage mieIrradianceLUT = VK_NULL_HANDLE;
    VmaAllocation mieIrradianceLUTAllocation = VK_NULL_HANDLE;
    VkImageView mieIrradianceLUTView = VK_NULL_HANDLE;

    // LUT sampler (bilinear filtering, clamp to edge)
    VkSampler lutSampler = VK_NULL_HANDLE;

    // Compute pipelines
    VkDescriptorSetLayout transmittanceDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout multiScatterDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skyViewDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout irradianceDescriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout transmittancePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout multiScatterPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout skyViewPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout irradiancePipelineLayout = VK_NULL_HANDLE;

    VkPipeline transmittancePipeline = VK_NULL_HANDLE;
    VkPipeline multiScatterPipeline = VK_NULL_HANDLE;
    VkPipeline skyViewPipeline = VK_NULL_HANDLE;
    VkPipeline irradiancePipeline = VK_NULL_HANDLE;

    VkDescriptorSet transmittanceDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet multiScatterDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet skyViewDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet irradianceDescriptorSet = VK_NULL_HANDLE;

    // Uniform buffer
    VkBuffer uniformBuffer = VK_NULL_HANDLE;
    VmaAllocation uniformAllocation = VK_NULL_HANDLE;
    void* uniformMappedPtr = nullptr;

    // Atmosphere parameters
    AtmosphereParams atmosphereParams;
};
