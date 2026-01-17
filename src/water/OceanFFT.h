#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "InitContext.h"
#include "VmaResources.h"

/**
 * OceanFFT - FFT-based Ocean Simulation (Tessendorf Method)
 *
 * Implements a physically-based ocean surface simulation using FFT.
 * Based on "Simulating Ocean Water" (Tessendorf, 2001).
 *
 * Pipeline:
 * 1. Generate initial spectrum H0(k) using Phillips spectrum (once at init)
 * 2. Each frame:
 *    a. Time evolution: H(k,t) from H0(k)
 *    b. Inverse FFT to get spatial displacement (Y, X, Z)
 *    c. Generate displacement, normal, and foam maps
 *
 * Supports cascaded FFT for multi-scale detail (large swells + small ripples).
 */

// Push constant structures for Ocean compute shaders
struct OceanTimeEvolutionPushConstants {
    float time;
    int resolution;
    float oceanSize;
    float choppiness;
};

struct OceanFFTPushConstants {
    int stage;
    int direction;
    int resolution;
    int inverse;
};

struct OceanDisplacementPushConstants {
    int resolution;
    float oceanSize;
    float heightScale;
    float foamThreshold;
    float foamDecay;
    float normalStrength;
};

class OceanFFT {
public:
    // Ocean simulation parameters
    struct OceanParams {
        int resolution = 256;           // FFT resolution (256 or 512)
        float oceanSize = 256.0f;       // Physical patch size in meters
        float windSpeed = 10.0f;        // Wind speed in m/s
        glm::vec2 windDirection = {0.8f, 0.6f};  // Wind direction (normalized)
        float amplitude = 0.0002f;      // Phillips spectrum amplitude (A constant)
        float gravity = 9.81f;          // Gravitational constant
        float smallWaveCutoff = 0.0001f; // Suppress waves smaller than this
        float alignment = 0.8f;         // Wind alignment (0=omni, 1=directional)
        float choppiness = 1.2f;        // Horizontal displacement scale (lambda)
        float heightScale = 1.0f;       // Height multiplier
        float foamThreshold = 0.0f;     // Jacobian threshold for foam
        float normalStrength = 1.0f;    // Normal map intensity
    };

    // Cascade configuration for multi-scale waves
    struct CascadeConfig {
        float oceanSize;     // Patch size for this cascade
        float heightScale;   // Height scale for this cascade
        float choppiness;    // Choppiness for this cascade
    };

    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue computeQueue;
        std::string shaderPath;
        uint32_t framesInFlight;
        OceanParams params;
        bool useCascades = true;  // Enable multi-scale cascades
        const vk::raii::Device* raiiDevice = nullptr;
    };

    /**
     * Factory: Create and initialize OceanFFT.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<OceanFFT> create(const InitInfo& info);
    static std::unique_ptr<OceanFFT> create(const InitContext& ctx, const OceanParams& params, bool useCascades = true);

    ~OceanFFT();

    // Non-copyable, non-movable
    OceanFFT(const OceanFFT&) = delete;
    OceanFFT& operator=(const OceanFFT&) = delete;
    OceanFFT(OceanFFT&&) = delete;
    OceanFFT& operator=(OceanFFT&&) = delete;

    // Update ocean simulation (call each frame before water rendering)
    // Records compute commands to animate the ocean
    void update(VkCommandBuffer cmd, uint32_t frameIndex, float time);

    // Regenerate spectrum (call when parameters change)
    void regenerateSpectrum(VkCommandBuffer cmd);

    // Get output textures for water shader
    VkImageView getDisplacementView(int cascade = 0) const;
    VkImageView getNormalView(int cascade = 0) const;
    VkImageView getFoamView(int cascade = 0) const;
    VkSampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    // Parameter access
    void setParams(const OceanParams& newParams);
    const OceanParams& getParams() const { return params; }

    // Individual parameter setters
    void setWindSpeed(float speed);
    void setWindDirection(const glm::vec2& dir);
    void setAmplitude(float amp);
    void setChoppiness(float chop);
    void setHeightScale(float scale);
    void setFoamThreshold(float threshold);

    // Getters for UI
    float getWindSpeed() const { return params.windSpeed; }
    glm::vec2 getWindDirection() const { return params.windDirection; }
    float getAmplitude() const { return params.amplitude; }
    float getChoppiness() const { return params.choppiness; }
    float getHeightScale() const { return params.heightScale; }
    float getFoamThreshold() const { return params.foamThreshold; }
    int getResolution() const { return params.resolution; }
    float getOceanSize() const { return params.oceanSize; }
    int getCascadeCount() const { return cascadeCount; }

    bool isEnabled() const { return enabled; }
    void setEnabled(bool enable) { enabled = enable; }

    // Check if spectrum needs regeneration
    bool needsRegeneration() const { return spectrumDirty; }
    void markSpectrumDirty() { spectrumDirty = true; }

private:
    OceanFFT() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    bool initInternal(const InitContext& ctx, const OceanParams& params, bool useCascades);
    void cleanup();

    // Per-cascade data
    struct Cascade {
        // Spectrum textures (generated once)
        ManagedImage h0Spectrum;
        std::optional<vk::raii::ImageView> h0SpectrumView;

        ManagedImage omegaSpectrum;
        std::optional<vk::raii::ImageView> omegaSpectrumView;

        // Time-evolved spectrum (per frame)
        ManagedImage hktDy;
        std::optional<vk::raii::ImageView> hktDyView;

        ManagedImage hktDx;
        std::optional<vk::raii::ImageView> hktDxView;

        ManagedImage hktDz;
        std::optional<vk::raii::ImageView> hktDzView;

        // FFT ping-pong buffers (reused for all 3 components)
        ManagedImage fftPing;
        std::optional<vk::raii::ImageView> fftPingView;

        ManagedImage fftPong;
        std::optional<vk::raii::ImageView> fftPongView;

        // Output textures
        ManagedImage displacementMap;
        std::optional<vk::raii::ImageView> displacementMapView;

        ManagedImage normalMap;
        std::optional<vk::raii::ImageView> normalMapView;

        ManagedImage foamMap;
        std::optional<vk::raii::ImageView> foamMapView;

        // Cascade-specific config
        CascadeConfig config;
    };

    bool createComputePipelines();
    bool createCascade(Cascade& cascade, const CascadeConfig& config);
    bool createImage(ManagedImage& image, std::optional<vk::raii::ImageView>& view,
                     VkFormat format, uint32_t width, uint32_t height, VkImageUsageFlags usage);
    bool createDescriptorSets();

    void recordSpectrumGeneration(VkCommandBuffer cmd, Cascade& cascade, uint32_t seed);
    void recordTimeEvolution(VkCommandBuffer cmd, Cascade& cascade, float time);
    void recordFFT(VkCommandBuffer cmd, Cascade& cascade, VkImage input, VkImageView inputView,
                   VkImage output, VkImageView outputView);
    void recordDisplacementGeneration(VkCommandBuffer cmd, Cascade& cascade);

    // Device resources
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    // Parameters
    OceanParams params;
    bool enabled = true;
    bool spectrumDirty = true;

    // Cascades for multi-scale simulation
    static constexpr int MAX_CASCADES = 3;
    std::vector<Cascade> cascades;
    int cascadeCount = 1;  // Start with single cascade

    // Compute pipelines
    std::optional<vk::raii::Pipeline> spectrumPipeline_;
    std::optional<vk::raii::PipelineLayout> spectrumPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> spectrumDescLayout_;

    std::optional<vk::raii::Pipeline> timeEvolutionPipeline_;
    std::optional<vk::raii::PipelineLayout> timeEvolutionPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> timeEvolutionDescLayout_;

    std::optional<vk::raii::Pipeline> fftPipeline_;
    std::optional<vk::raii::PipelineLayout> fftPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> fftDescLayout_;

    std::optional<vk::raii::Pipeline> displacementPipeline_;
    std::optional<vk::raii::PipelineLayout> displacementPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> displacementDescLayout_;

    // Descriptor pool and sets
    std::optional<vk::raii::DescriptorPool> descriptorPool_;
    std::vector<VkDescriptorSet> spectrumDescSets;
    std::vector<VkDescriptorSet> timeEvolutionDescSets;
    std::vector<VkDescriptorSet> fftDescSets;       // Multiple for ping-pong
    std::vector<VkDescriptorSet> displacementDescSets;

    // Spectrum parameters UBO
    struct SpectrumUBO {
        int resolution;
        float oceanSize;
        float windSpeed;
        float padding1;
        glm::vec2 windDirection;
        float amplitude;
        float gravity;
        float smallWaveCutoff;
        float alignment;
        uint32_t seed;
        float padding2;
        float padding3;
        float padding4;
    };
    std::vector<ManagedBuffer> spectrumUBOs;
    std::vector<void*> spectrumUBOMapped;

    // Sampler for output textures
    std::optional<vk::raii::Sampler> sampler_;

    // RAII device pointer
    const vk::raii::Device* raiiDevice_ = nullptr;
};
