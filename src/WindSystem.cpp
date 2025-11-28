#include "WindSystem.h"
#include "BufferUtils.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <cmath>

bool WindSystem::init(const InitInfo& info) {
    framesInFlight = info.framesInFlight;

    BufferUtils::PerFrameBufferBuilder builder;
    if (!builder.setAllocator(info.allocator)
             .setFrameCount(framesInFlight)
             .setSize(sizeof(WindUniforms))
             .build(uniformBuffers)) {
        return false;
    }

    // Initialize permutation table for Perlin noise
    initPermutationTable();

    SDL_Log("Wind system initialized successfully");
    return true;
}

void WindSystem::destroy(VkDevice device, VmaAllocator allocator) {
    BufferUtils::destroyBuffers(allocator, uniformBuffers);
    uniformBuffers = {};
}

void WindSystem::initPermutationTable() {
    // Standard Perlin noise permutation table
    // Using a fixed seed for deterministic results that match GPU
    static const int p[] = {
        151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
        140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
        247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
        57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
        74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
        60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
        65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
        200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
        52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
        207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
        119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
        129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
        218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
        81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
        184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
        222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
    };

    for (int i = 0; i < PERM_SIZE; i++) {
        permutation[i] = p[i];
        permutation[PERM_SIZE + i] = p[i];
    }
}

void WindSystem::update(float deltaTime) {
    totalTime += deltaTime;
}

void WindSystem::updateUniforms(uint32_t frameIndex) {
    WindUniforms uniforms{};

    // Pack direction (xy), strength (z), and speed (w)
    uniforms.windDirectionAndStrength = glm::vec4(
        windDirection.x,
        windDirection.y,
        windStrength,
        windSpeed
    );

    // Pack gust parameters, noise scale, and time
    uniforms.windParams = glm::vec4(
        gustFrequency,
        gustAmplitude,
        noiseScale,
        totalTime
    );

    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(WindUniforms));
}

VkDescriptorBufferInfo WindSystem::getBufferInfo(uint32_t frameIndex) const {
    VkDescriptorBufferInfo info{};
    info.buffer = uniformBuffers.buffers[frameIndex];
    info.offset = 0;
    info.range = sizeof(WindUniforms);
    return info;
}

void WindSystem::setWindDirection(const glm::vec2& direction) {
    float len = glm::length(direction);
    if (len > 0.0001f) {
        windDirection = direction / len;
    }
}

void WindSystem::setWindStrength(float strength) {
    windStrength = glm::max(0.0f, strength);
}

void WindSystem::setWindSpeed(float speed) {
    windSpeed = glm::max(0.0f, speed);
}

void WindSystem::setGustFrequency(float frequency) {
    gustFrequency = glm::max(0.0f, frequency);
}

void WindSystem::setGustAmplitude(float amplitude) {
    gustAmplitude = glm::max(0.0f, amplitude);
}

void WindSystem::setNoiseScale(float scale) {
    noiseScale = glm::max(0.001f, scale);
}

float WindSystem::fade(float t) const {
    // 6t^5 - 15t^4 + 10t^3 (smoothstep improved)
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float WindSystem::lerp(float a, float b, float t) const {
    return a + t * (b - a);
}

float WindSystem::grad(int hash, float x, float y) const {
    // Convert low 4 bits of hash code into gradient direction
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

float WindSystem::perlinNoise(float x, float y) const {
    // Find unit grid cell containing point
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;

    // Get relative position within cell
    x -= std::floor(x);
    y -= std::floor(y);

    // Compute fade curves
    float u = fade(x);
    float v = fade(y);

    // Hash coordinates of 4 corners
    int A = permutation[X] + Y;
    int AA = permutation[A];
    int AB = permutation[A + 1];
    int B = permutation[X + 1] + Y;
    int BA = permutation[B];
    int BB = permutation[B + 1];

    // Interpolate gradients
    float res = lerp(
        lerp(grad(permutation[AA], x, y),
             grad(permutation[BA], x - 1.0f, y), u),
        lerp(grad(permutation[AB], x, y - 1.0f),
             grad(permutation[BB], x - 1.0f, y - 1.0f), u),
        v
    );

    // Return value in range [-1, 1], normalize to [0, 1]
    return (res + 1.0f) * 0.5f;
}

float WindSystem::sampleWindAtPosition(const glm::vec2& worldPos) const {
    // Scroll position in wind direction
    glm::vec2 scrolledPos = worldPos - windDirection * totalTime * windSpeed * 0.4f;

    // Three octaves: ~10m, ~5m, ~2.5m wavelengths
    float baseFreq = 0.1f;
    float n1 = perlinNoise(scrolledPos.x * baseFreq, scrolledPos.y * baseFreq);
    float n2 = perlinNoise(scrolledPos.x * baseFreq * 2.0f, scrolledPos.y * baseFreq * 2.0f);
    float n3 = perlinNoise(scrolledPos.x * baseFreq * 4.0f, scrolledPos.y * baseFreq * 4.0f);

    // Weighted sum dominated by first octave (like 0.7 + 0.2 + 0.1)
    float noise = n1 * 0.7f + n2 * 0.2f + n3 * 0.1f;

    // Add gust variation (time-based sine wave)
    float gust = (std::sin(totalTime * gustFrequency * 6.28318f) * 0.5f + 0.5f) * gustAmplitude;

    return (noise + gust) * windStrength;
}
