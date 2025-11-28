#pragma once

#include "EnvironmentSettings.h"
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

// Wind uniform data passed to GPU shaders
// Must match the GLSL WindUniforms struct exactly
struct WindUniforms {
    glm::vec4 windDirectionAndStrength;  // xy = normalized direction, z = strength, w = speed
    glm::vec4 windParams;                 // x = gustFrequency, y = gustAmplitude, z = noiseScale, w = time
};

// Wind system for CPU-side wind management and GPU uniform updates
// Implements a scrolling Perlin noise wind model as described in Ghost of Tsushima's wind system
class WindSystem {
public:
    WindSystem() = default;
    ~WindSystem() = default;

    // Initialization parameters
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkDescriptorPool descriptorPool;
        uint32_t framesInFlight;
    };

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Update wind state each frame
    void update(float deltaTime);

    // Update the GPU uniform buffer for a specific frame
    void updateUniforms(uint32_t frameIndex);

    // Get descriptor buffer info for binding
    VkDescriptorBufferInfo getBufferInfo(uint32_t frameIndex) const;

    // Shared environment settings
    const EnvironmentSettings& getEnvironmentSettings() const { return environmentSettings; }

    // Wind direction control (normalized 2D direction)
    void setWindDirection(const glm::vec2& direction);
    glm::vec2 getWindDirection() const { return environmentSettings.windDirection; }

    // Wind strength (0 = calm, 1 = normal, 2+ = storm)
    void setWindStrength(float strength);
    float getWindStrength() const { return environmentSettings.windStrength; }

    // Wind speed (how fast the noise pattern scrolls in the wind direction)
    void setWindSpeed(float speed);
    float getWindSpeed() const { return environmentSettings.windSpeed; }

    // Gust parameters
    void setGustFrequency(float frequency);
    void setGustAmplitude(float amplitude);
    float getGustFrequency() const { return environmentSettings.gustFrequency; }
    float getGustAmplitude() const { return environmentSettings.gustAmplitude; }

    // Noise scale (controls the size of wind waves in world units)
    void setNoiseScale(float scale);
    float getNoiseScale() const { return environmentSettings.noiseScale; }

    // Sample wind strength at a world position (for CPU-side gameplay)
    // Returns wind strength multiplier at that position
    float sampleWindAtPosition(const glm::vec2& worldPos) const;

    // Get the total elapsed time (for shader synchronization)
    float getTime() const { return totalTime; }

private:
    // CPU-side Perlin noise for gameplay sampling
    float perlinNoise(float x, float y) const;
    float fade(float t) const;
    float lerp(float a, float b, float t) const;
    float grad(int hash, float x, float y) const;

    // Wind parameters
    EnvironmentSettings environmentSettings{};

    // Time tracking
    float totalTime = 0.0f;

    // Vulkan resources
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformAllocations;
    std::vector<void*> uniformMappedPtrs;
    uint32_t framesInFlight = 0;

    // Pre-computed gradient table for Perlin noise (same seed as GPU)
    static constexpr int PERM_SIZE = 256;
    int permutation[PERM_SIZE * 2];

    void initPermutationTable();
};
