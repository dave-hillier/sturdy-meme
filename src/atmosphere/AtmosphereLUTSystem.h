#pragma once

#include <vulkan/vulkan.hpp>  // Vulkan-Hpp for type-safe enums and structs
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include "UBOs.h"
#include "BufferUtils.h"
#include "DescriptorManager.h"
#include "InitContext.h"
#include "VulkanRAII.h"

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

// AtmosphereUniforms struct (manually defined since it contains nested AtmosphereParams)
struct AtmosphereUniforms {
    AtmosphereParams params;
    glm::vec4 sunDirection;  // xyz = sun dir, w = unused
    glm::vec4 cameraPosition; // xyz = camera pos, w = camera altitude
    float padding[2];
};


// Cloud map uniform parameters (must match GLSL layout)
struct CloudMapUniforms {
    glm::vec4 windOffset;    // xyz = wind offset for animation, w = time
    float coverage;          // 0-1 cloud coverage amount
    float density;           // Base density multiplier
    float sharpness;         // Coverage threshold sharpness
    float detailScale;       // Scale for detail noise
};

class AtmosphereLUTSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
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

    // Cloud Map LUT dimensions (Paraboloid projection)
    // Stores procedural cloud density mapped to hemisphere directions
    static constexpr uint32_t CLOUDMAP_SIZE = 256;     // Square texture for paraboloid map

    /**
     * Factory: Create and initialize AtmosphereLUTSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<AtmosphereLUTSystem> create(const InitInfo& info);
    static std::unique_ptr<AtmosphereLUTSystem> create(const InitContext& ctx);

    ~AtmosphereLUTSystem();

    // Non-copyable, non-movable
    AtmosphereLUTSystem(const AtmosphereLUTSystem&) = delete;
    AtmosphereLUTSystem& operator=(const AtmosphereLUTSystem&) = delete;
    AtmosphereLUTSystem(AtmosphereLUTSystem&&) = delete;
    AtmosphereLUTSystem& operator=(AtmosphereLUTSystem&&) = delete;

    // Compute LUTs (called at startup and when atmosphere parameters change)
    void computeTransmittanceLUT(VkCommandBuffer cmd);
    void computeMultiScatterLUT(VkCommandBuffer cmd);
    void computeIrradianceLUT(VkCommandBuffer cmd);
    void computeSkyViewLUT(VkCommandBuffer cmd, const glm::vec3& sunDir, const glm::vec3& cameraPos, float cameraAltitude);
    void computeCloudMapLUT(VkCommandBuffer cmd, const glm::vec3& windOffset, float time);

    // Update sky-view LUT per frame (uses SHADER_READ_ONLY_OPTIMAL as old layout since LUT was already computed)
    // frameIndex is required for proper double-buffering of uniform buffers and descriptor sets
    void updateSkyViewLUT(VkCommandBuffer cmd, uint32_t frameIndex, const glm::vec3& sunDir, const glm::vec3& cameraPos, float cameraAltitude);
    void updateCloudMapLUT(VkCommandBuffer cmd, uint32_t frameIndex, const glm::vec3& windOffset, float time);

    // Get LUT views for sampling in shaders
    VkImageView getTransmittanceLUTView() const { return transmittanceLUTView; }
    VkImageView getMultiScatterLUTView() const { return multiScatterLUTView; }
    VkImageView getSkyViewLUTView() const { return skyViewLUTView; }
    VkImageView getRayleighIrradianceLUTView() const { return rayleighIrradianceLUTView; }
    VkImageView getMieIrradianceLUTView() const { return mieIrradianceLUTView; }
    VkImageView getCloudMapLUTView() const { return cloudMapLUTView; }
    VkSampler getLUTSampler() const { return lutSampler.get(); }

    // Export LUTs as PNG files (for debugging/visualization)
    bool exportLUTsAsPNG(const std::string& outputDir);

    // Atmosphere parameters
    void setAtmosphereParams(const AtmosphereParams& params) {
        atmosphereParams = params;
        paramsDirty = true;  // Mark for LUT recomputation
    }
    const AtmosphereParams& getAtmosphereParams() const { return atmosphereParams; }

    // Cloud map parameters (used by updateCloudMapLUT)
    void setCloudCoverage(float coverage) { cloudCoverage = glm::clamp(coverage, 0.0f, 1.0f); }
    float getCloudCoverage() const { return cloudCoverage; }
    void setCloudDensity(float density) { cloudDensity = glm::clamp(density, 0.0f, 2.0f); }
    float getCloudDensity() const { return cloudDensity; }

    // Check if LUTs need recomputation due to parameter changes
    bool needsRecompute() const { return paramsDirty; }

    // Recompute static LUTs (transmittance, multi-scatter, irradiance) when params change
    // Call this from the render loop when needsRecompute() returns true
    void recomputeStaticLUTs(VkCommandBuffer cmd);

private:
    bool createTransmittanceLUT();
    bool createMultiScatterLUT();
    bool createSkyViewLUT();
    bool createIrradianceLUTs();
    bool createCloudMapLUT();
    bool createLUTSampler();
    bool createDescriptorSetLayouts();
    bool createDescriptorSets();
    bool createUniformBuffer();
    bool createComputePipelines();

    void destroyLUTResources();

    // Transition irradiance LUTs for compute write
    void barrierIrradianceLUTsForCompute(VkCommandBuffer cmd);

    // Transition irradiance LUTs for fragment shader sampling
    void barrierIrradianceLUTsForSampling(VkCommandBuffer cmd);

    // Helper to export a 2D image to PNG
    bool exportImageToPNG(VkImage image, VkFormat format, uint32_t width, uint32_t height, const std::string& filename);

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
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

    // Cloud Map LUT (256×256, RGBA16F) - Paraboloid projection
    // R = base density, G = detail noise, B = coverage mask, A = height gradient
    VkImage cloudMapLUT = VK_NULL_HANDLE;
    VmaAllocation cloudMapLUTAllocation = VK_NULL_HANDLE;
    VkImageView cloudMapLUTView = VK_NULL_HANDLE;

    // LUT sampler (bilinear filtering, clamp to edge)
    ManagedSampler lutSampler;

    // Compute pipelines
    VkDescriptorSetLayout transmittanceDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout multiScatterDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout skyViewDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout irradianceDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout cloudMapDescriptorSetLayout = VK_NULL_HANDLE;

    VkPipelineLayout transmittancePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout multiScatterPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout skyViewPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout irradiancePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout cloudMapPipelineLayout = VK_NULL_HANDLE;

    VkPipeline transmittancePipeline = VK_NULL_HANDLE;
    VkPipeline multiScatterPipeline = VK_NULL_HANDLE;
    VkPipeline skyViewPipeline = VK_NULL_HANDLE;
    VkPipeline irradiancePipeline = VK_NULL_HANDLE;
    VkPipeline cloudMapPipeline = VK_NULL_HANDLE;

    // Single descriptor sets for one-time LUT computation (at startup)
    VkDescriptorSet transmittanceDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet multiScatterDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet irradianceDescriptorSet = VK_NULL_HANDLE;

    // Per-frame descriptor sets for per-frame LUT updates (double-buffered)
    std::vector<VkDescriptorSet> skyViewDescriptorSets;
    std::vector<VkDescriptorSet> cloudMapDescriptorSets;

    // Uniform buffers for one-time LUT computation (at startup)
    // Uses PerFrameBufferSet with frame count of 1 for consistency
    BufferUtils::PerFrameBufferSet staticUniformBuffers;

    // Per-frame uniform buffers for per-frame updates (double-buffered)
    BufferUtils::PerFrameBufferSet skyViewUniformBuffers;
    BufferUtils::PerFrameBufferSet cloudMapUniformBuffers;

    // Atmosphere parameters
    AtmosphereParams atmosphereParams;

    // Cloud map parameters
    float cloudCoverage = 0.5f;  // 0-1 cloud coverage
    float cloudDensity = 0.3f;   // Base density multiplier

    // Dirty flag for LUT recomputation
    bool paramsDirty = false;

    AtmosphereLUTSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();
};
