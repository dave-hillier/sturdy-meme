#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

struct EnvironmentSettings;
struct FrameData;
class RendererSystems;

/**
 * VegetationRenderContext - Per-frame render state for vegetation systems
 *
 * Bundles together all the per-frame data that vegetation systems need
 * for rendering. This reduces parameter passing and makes dependencies
 * explicit at the call site.
 *
 * All resource references are non-owning - the context is a lightweight
 * value type that can be passed by const reference.
 *
 * Usage:
 *   // Preferred: build from systems
 *   auto ctx = VegetationRenderContext::fromSystems(systems, frame);
 *
 *   // Or use builder for fine-grained control:
 *   auto ctx = VegetationRenderContext::Builder()
 *       .setFrameIndex(frameIndex)
 *       .setTime(time)
 *       .setCameraPosition(cameraPos)
 *       .build();
 */
struct VegetationRenderContext {
    // Frame identification
    uint32_t frameIndex = 0;
    float time = 0.0f;
    float deltaTime = 0.0f;

    // Camera state
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);
    glm::mat4 viewProjectionMatrix = glm::mat4(1.0f);

    // Terrain info
    float terrainSize = 0.0f;
    float terrainHeightScale = 0.0f;

    // Wind uniform buffer (for animation)
    vk::Buffer windUBO;
    vk::DeviceSize windUBOOffset = 0;

    // Displacement texture (for player interaction)
    vk::ImageView displacementView;
    vk::Sampler displacementSampler;
    glm::vec4 displacementRegion;  // xy = center, z = size, w = texel size

    // Shadow map (for receiving shadows)
    vk::ImageView shadowMapView;
    vk::Sampler shadowMapSampler;

    // Cloud shadow (for atmospheric shadows)
    vk::ImageView cloudShadowView;
    vk::Sampler cloudShadowSampler;

    // Environment settings (non-owning)
    const EnvironmentSettings* environment = nullptr;

    // Dynamic UBO offset for renderer uniforms (if using dynamic UBO)
    uint32_t rendererUBOOffset = 0;

    /**
     * Factory: Build context from RendererSystems and FrameData.
     * This is the preferred way to create a context for rendering.
     */
    static VegetationRenderContext fromSystems(
        RendererSystems& systems,
        const FrameData& frame);
};

/**
 * Builder for constructing VegetationRenderContext with fluent API.
 * Use when you need fine-grained control over context construction.
 * Defined outside VegetationRenderContext to avoid incomplete type issues.
 */
class VegetationRenderContextBuilder {
public:
    VegetationRenderContextBuilder& setFrameIndex(uint32_t index) { ctx_.frameIndex = index; return *this; }
    VegetationRenderContextBuilder& setTime(float t) { ctx_.time = t; return *this; }
    VegetationRenderContextBuilder& setDeltaTime(float dt) { ctx_.deltaTime = dt; return *this; }

    VegetationRenderContextBuilder& setCameraPosition(const glm::vec3& pos) { ctx_.cameraPosition = pos; return *this; }
    VegetationRenderContextBuilder& setViewMatrix(const glm::mat4& view) { ctx_.viewMatrix = view; return *this; }
    VegetationRenderContextBuilder& setProjectionMatrix(const glm::mat4& proj) { ctx_.projectionMatrix = proj; return *this; }
    VegetationRenderContextBuilder& setViewProjection(const glm::mat4& view, const glm::mat4& proj) {
        ctx_.viewMatrix = view;
        ctx_.projectionMatrix = proj;
        ctx_.viewProjectionMatrix = proj * view;
        return *this;
    }
    VegetationRenderContextBuilder& setViewProjectionMatrix(const glm::mat4& viewProj) {
        ctx_.viewProjectionMatrix = viewProj;
        return *this;
    }

    VegetationRenderContextBuilder& setTerrainInfo(float size, float heightScale) {
        ctx_.terrainSize = size;
        ctx_.terrainHeightScale = heightScale;
        return *this;
    }

    VegetationRenderContextBuilder& setWindUBO(vk::Buffer buffer, vk::DeviceSize offset = 0) {
        ctx_.windUBO = buffer;
        ctx_.windUBOOffset = offset;
        return *this;
    }

    VegetationRenderContextBuilder& setDisplacement(vk::ImageView view, vk::Sampler sampler, const glm::vec4& region) {
        ctx_.displacementView = view;
        ctx_.displacementSampler = sampler;
        ctx_.displacementRegion = region;
        return *this;
    }

    VegetationRenderContextBuilder& setShadowMap(vk::ImageView view, vk::Sampler sampler) {
        ctx_.shadowMapView = view;
        ctx_.shadowMapSampler = sampler;
        return *this;
    }

    VegetationRenderContextBuilder& setCloudShadow(vk::ImageView view, vk::Sampler sampler) {
        ctx_.cloudShadowView = view;
        ctx_.cloudShadowSampler = sampler;
        return *this;
    }

    VegetationRenderContextBuilder& setEnvironment(const EnvironmentSettings* env) {
        ctx_.environment = env;
        return *this;
    }

    VegetationRenderContextBuilder& setRendererUBOOffset(uint32_t offset) {
        ctx_.rendererUBOOffset = offset;
        return *this;
    }

    VegetationRenderContext build() const { return ctx_; }

private:
    VegetationRenderContext ctx_;
};
