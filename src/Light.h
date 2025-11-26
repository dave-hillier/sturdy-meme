#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <cstdint>

// Maximum number of lights supported in the shader
static constexpr uint32_t MAX_LIGHTS = 16;

// Light types
enum class LightType : uint32_t {
    Point = 0,
    Spot = 1
};

// GPU-side light data structure (std430 layout compatible)
// Must match the shader struct exactly
struct GPULight {
    glm::vec4 positionAndType;    // xyz = position, w = type (0=point, 1=spot)
    glm::vec4 directionAndCone;   // xyz = direction (for spot), w = outer cone angle (cos)
    glm::vec4 colorAndIntensity;  // rgb = color, a = intensity
    glm::vec4 radiusAndInnerCone; // x = radius, y = inner cone angle (cos), zw = padding
};

// Light buffer sent to GPU (header + array)
struct LightBuffer {
    glm::uvec4 lightCount;        // x = active light count, yzw = padding
    GPULight lights[MAX_LIGHTS];
};

// CPU-side light representation with additional metadata
struct Light {
    LightType type = LightType::Point;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);  // For spots
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float radius = 10.0f;         // Falloff radius
    float innerConeAngle = 30.0f; // Degrees, for spots
    float outerConeAngle = 45.0f; // Degrees, for spots

    // Priority/culling metadata
    float priority = 1.0f;        // Higher = more important, less likely to be culled
    bool enabled = true;

    // Convert to GPU format
    GPULight toGPU() const {
        GPULight gpu{};
        gpu.positionAndType = glm::vec4(position, static_cast<float>(type));
        gpu.directionAndCone = glm::vec4(
            glm::normalize(direction),
            glm::cos(glm::radians(outerConeAngle))
        );
        gpu.colorAndIntensity = glm::vec4(color, intensity);
        gpu.radiusAndInnerCone = glm::vec4(
            radius,
            glm::cos(glm::radians(innerConeAngle)),
            0.0f, 0.0f
        );
        return gpu;
    }
};

// Frustum culling helper - tests if a sphere is inside the view frustum
// Returns true if the sphere (light) is potentially visible
inline bool isSphereInFrustum(const glm::vec3& center, float radius, const glm::mat4& viewProj) {
    // Transform the sphere center to clip space
    glm::vec4 clipPos = viewProj * glm::vec4(center, 1.0f);

    // Perspective divide to get NDC coordinates
    if (clipPos.w <= 0.0f) {
        // Behind the camera
        return false;
    }

    glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

    // Calculate the radius in NDC space (conservative approximation)
    // We test the radius against the clip space w-coordinate
    float ndcRadius = radius / clipPos.w;

    // Test against all 6 frustum planes in NDC space (range: -1 to 1 for x,y and 0 to 1 for z in Vulkan)
    // Add radius margin to account for the sphere's size
    if (ndc.x + ndcRadius < -1.0f || ndc.x - ndcRadius > 1.0f) return false;  // Left/Right
    if (ndc.y + ndcRadius < -1.0f || ndc.y - ndcRadius > 1.0f) return false;  // Bottom/Top
    if (ndc.z + ndcRadius < 0.0f || ndc.z - ndcRadius > 1.0f) return false;   // Near/Far (Vulkan depth range)

    return true;
}

// Manages a collection of lights with culling and prioritization
class LightManager {
public:
    LightManager() = default;

    // Add a light, returns handle/index for later reference
    size_t addLight(const Light& light) {
        lights.push_back(light);
        return lights.size() - 1;
    }

    // Remove light by index
    void removeLight(size_t index) {
        if (index < lights.size()) {
            lights.erase(lights.begin() + index);
        }
    }

    // Clear all lights
    void clear() {
        lights.clear();
    }

    // Get/set light by index
    Light& getLight(size_t index) { return lights[index]; }
    const Light& getLight(size_t index) const { return lights[index]; }

    size_t getLightCount() const { return lights.size(); }

    // Build GPU buffer with culling based on camera position, frustum, and view direction
    // Returns the number of active lights after culling
    uint32_t buildLightBuffer(LightBuffer& buffer, const glm::vec3& cameraPos, const glm::vec3& cameraFront,
                              const glm::mat4& viewProjMatrix, float cullRadius = 100.0f) const {
        // Collect enabled lights with their distances
        struct LightDistance {
            size_t index;
            float distance;
            float effectiveWeight; // priority / distance for sorting
        };

        std::vector<LightDistance> candidates;
        candidates.reserve(lights.size());

        for (size_t i = 0; i < lights.size(); i++) {
            const Light& light = lights[i];
            if (!light.enabled) continue;

            // Test against frustum first (cheap rejection)
            if (!isSphereInFrustum(light.position, light.radius, viewProjMatrix)) {
                continue;
            }

            float dist = glm::length(light.position - cameraPos);

            // Skip lights too far from camera (outside cull radius + light radius)
            if (dist > cullRadius + light.radius) continue;

            // Calculate angular weighting based on alignment with view direction
            // Lights in front of the camera get higher weight than those behind
            glm::vec3 toLight = glm::normalize(light.position - cameraPos);
            float angleFactor = glm::max(0.0f, glm::dot(toLight, cameraFront));
            // Bias towards forward-facing lights: range from 0.25 (behind) to 1.0 (front)
            angleFactor = 0.25f + 0.75f * angleFactor;

            // Calculate effective weight for prioritization
            // Higher priority, closer distance, and better alignment with view = higher weight
            float effectiveWeight = (light.priority * angleFactor) / (dist + 1.0f);

            candidates.push_back({i, dist, effectiveWeight});
        }

        // Sort by effective weight (descending) to keep most important lights
        std::sort(candidates.begin(), candidates.end(),
            [](const LightDistance& a, const LightDistance& b) {
                return a.effectiveWeight > b.effectiveWeight;
            });

        // Copy up to MAX_LIGHTS to buffer
        uint32_t count = static_cast<uint32_t>(std::min(candidates.size(), static_cast<size_t>(MAX_LIGHTS)));
        buffer.lightCount = glm::uvec4(count, 0, 0, 0);

        for (uint32_t i = 0; i < count; i++) {
            buffer.lights[i] = lights[candidates[i].index].toGPU();
        }

        // Zero out unused slots (optional but clean)
        for (uint32_t i = count; i < MAX_LIGHTS; i++) {
            buffer.lights[i] = GPULight{};
        }

        return count;
    }

    // Simple build without culling (for testing)
    uint32_t buildLightBufferSimple(LightBuffer& buffer) const {
        uint32_t count = static_cast<uint32_t>(std::min(lights.size(), static_cast<size_t>(MAX_LIGHTS)));
        buffer.lightCount = glm::uvec4(count, 0, 0, 0);

        uint32_t written = 0;
        for (size_t i = 0; i < lights.size() && written < MAX_LIGHTS; i++) {
            if (lights[i].enabled) {
                buffer.lights[written++] = lights[i].toGPU();
            }
        }

        buffer.lightCount.x = written;
        return written;
    }

private:
    std::vector<Light> lights;
};
