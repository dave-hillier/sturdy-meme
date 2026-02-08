#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <functional>

namespace material {

/**
 * MaterialLayer - System for blending multiple materials spatially
 *
 * Allows stacking materials with various blend modes:
 * - Height-based: Blend based on world Y position
 * - Slope-based: Blend based on surface angle
 * - Mask-based: Blend using an explicit texture mask
 * - Noise-based: Procedural blending with world-space noise
 * - Distance-based: Blend based on distance from a point
 *
 * Use cases:
 * - Terrain: Rock + Grass + Snow (height/slope-based)
 * - Roads: Asphalt + Puddles (mask-based)
 * - Shoreline: Sand + Wet Sand + Water (distance to water)
 */

enum class BlendMode : uint32_t {
    Height,      // Blend based on world Y position
    Slope,       // Blend based on surface normal (steepness)
    Mask,        // Blend using explicit mask texture
    Noise,       // Procedural noise-based blending
    Distance,    // Distance from a world position
    Directional, // Blend along a direction vector
    Altitude,    // Like height but with smooth transitions at thresholds
};

/**
 * BlendParams - Parameters controlling how layers blend
 */
struct BlendParams {
    BlendMode mode = BlendMode::Height;

    // Height/Altitude mode parameters
    float heightMin = 0.0f;        // Start blending at this height
    float heightMax = 100.0f;      // Full blend at this height
    float heightSoftness = 10.0f;  // Transition zone size

    // Slope mode parameters
    float slopeMin = 0.0f;         // Minimum slope angle (radians, 0 = flat)
    float slopeMax = 1.57f;        // Maximum slope angle (radians, π/2 = vertical)
    float slopeSoftness = 0.2f;    // Transition zone

    // Distance mode parameters
    glm::vec3 distanceCenter = glm::vec3(0.0f);
    float distanceMin = 0.0f;
    float distanceMax = 100.0f;

    // Directional mode parameters
    glm::vec3 direction = glm::vec3(1.0f, 0.0f, 0.0f);
    float directionalOffset = 0.0f;
    float directionalScale = 100.0f;

    // Noise mode parameters
    float noiseScale = 0.1f;       // World-space noise frequency
    float noiseThreshold = 0.5f;   // Blend threshold
    float noiseSoftness = 0.2f;    // Transition softness
    int noiseOctaves = 3;          // FBM octaves

    // General
    float opacity = 1.0f;          // Overall layer opacity
    bool invertBlend = false;      // Invert the blend factor

    // Factory methods for common configurations
    static BlendParams heightBased(float minH, float maxH, float softness = 10.0f) {
        BlendParams p;
        p.mode = BlendMode::Height;
        p.heightMin = minH;
        p.heightMax = maxH;
        p.heightSoftness = softness;
        return p;
    }

    static BlendParams slopeBased(float minAngle, float maxAngle, float softness = 0.2f) {
        BlendParams p;
        p.mode = BlendMode::Slope;
        p.slopeMin = minAngle;
        p.slopeMax = maxAngle;
        p.slopeSoftness = softness;
        return p;
    }

    static BlendParams distanceBased(const glm::vec3& center, float minDist, float maxDist) {
        BlendParams p;
        p.mode = BlendMode::Distance;
        p.distanceCenter = center;
        p.distanceMin = minDist;
        p.distanceMax = maxDist;
        return p;
    }

    static BlendParams noiseBased(float scale, float threshold = 0.5f, float softness = 0.2f) {
        BlendParams p;
        p.mode = BlendMode::Noise;
        p.noiseScale = scale;
        p.noiseThreshold = threshold;
        p.noiseSoftness = softness;
        return p;
    }
};

/**
 * MaterialLayerDef - Definition of a single material layer
 */
struct MaterialLayerDef {
    uint32_t materialId = 0;       // Reference to material in registry
    BlendParams blendParams;
    bool enabled = true;

    // Optional mask texture index (for Mask blend mode)
    int32_t maskTextureIndex = -1;

    MaterialLayerDef() = default;

    MaterialLayerDef(uint32_t matId, const BlendParams& params)
        : materialId(matId), blendParams(params) {}
};

/**
 * MaterialLayerStack - A stack of material layers to be blended
 *
 * Layers are blended bottom-to-top. The base layer (index 0) is always
 * fully opaque, subsequent layers blend on top based on their BlendParams.
 */
class MaterialLayerStack {
public:
    static constexpr size_t MAX_LAYERS = 8;

    MaterialLayerStack() = default;

    // Add a layer to the stack
    bool addLayer(const MaterialLayerDef& layer) {
        if (layers_.size() >= MAX_LAYERS) {
            return false;
        }
        layers_.push_back(layer);
        return true;
    }

    // Add layer with fluent interface
    MaterialLayerStack& withLayer(uint32_t materialId, const BlendParams& params) {
        addLayer(MaterialLayerDef(materialId, params));
        return *this;
    }

    // Set base layer (always fully visible)
    MaterialLayerStack& withBase(uint32_t materialId) {
        if (layers_.empty()) {
            MaterialLayerDef base;
            base.materialId = materialId;
            base.blendParams.opacity = 1.0f;
            layers_.push_back(base);
        } else {
            layers_[0].materialId = materialId;
        }
        return *this;
    }

    // Get layer at index
    const MaterialLayerDef* getLayer(size_t index) const {
        return index < layers_.size() ? &layers_[index] : nullptr;
    }

    MaterialLayerDef* getLayer(size_t index) {
        return index < layers_.size() ? &layers_[index] : nullptr;
    }

    // Get number of layers
    size_t getLayerCount() const { return layers_.size(); }

    // Clear all layers
    void clear() { layers_.clear(); }

    // Remove layer at index
    bool removeLayer(size_t index) {
        if (index >= layers_.size()) return false;
        layers_.erase(layers_.begin() + index);
        return true;
    }

    // Enable/disable layer
    void setLayerEnabled(size_t index, bool enabled) {
        if (index < layers_.size()) {
            layers_[index].enabled = enabled;
        }
    }

    // Get all layers
    const std::vector<MaterialLayerDef>& getLayers() const { return layers_; }

private:
    std::vector<MaterialLayerDef> layers_;
};

/**
 * MaterialLayerUBO - GPU-compatible uniform buffer for layer blending
 *
 * This is uploaded to the GPU for shader-side layer evaluation.
 * Supports up to 4 layers for real-time performance.
 */
struct MaterialLayerUBO {
    static constexpr int MAX_GPU_LAYERS = 4;

    struct LayerData {
        glm::vec4 params0;  // mode, heightMin, heightMax, heightSoftness
        glm::vec4 params1;  // slopeMin, slopeMax, slopeSoftness, opacity
        glm::vec4 params2;  // noiseScale, noiseThreshold, noiseSoftness, invertBlend
        glm::vec4 center;   // distanceCenter.xyz, distanceMin
        glm::vec4 direction; // direction.xyz, distanceMax/directionalScale
    };

    LayerData layers[MAX_GPU_LAYERS];
    int numLayers = 0;
    // Note: Individual ints instead of int[3] array to match GLSL std140
    // layout (arrays get 16-byte stride per element in std140, scalars don't)
    int padding0 = 0;
    int padding1 = 0;
    int padding2 = 0;

    // Pack a MaterialLayerStack into the UBO
    void packFromStack(const MaterialLayerStack& stack) {
        numLayers = static_cast<int>(std::min(stack.getLayerCount(), static_cast<size_t>(MAX_GPU_LAYERS)));

        for (int i = 0; i < numLayers; ++i) {
            const MaterialLayerDef* layer = stack.getLayer(i);
            if (!layer || !layer->enabled) continue;

            const BlendParams& bp = layer->blendParams;

            layers[i].params0 = glm::vec4(
                static_cast<float>(bp.mode),
                bp.heightMin,
                bp.heightMax,
                bp.heightSoftness
            );

            layers[i].params1 = glm::vec4(
                bp.slopeMin,
                bp.slopeMax,
                bp.slopeSoftness,
                bp.opacity
            );

            layers[i].params2 = glm::vec4(
                bp.noiseScale,
                bp.noiseThreshold,
                bp.noiseSoftness,
                bp.invertBlend ? 1.0f : 0.0f
            );

            layers[i].center = glm::vec4(
                bp.distanceCenter,
                bp.distanceMin
            );

            layers[i].direction = glm::vec4(
                bp.direction,
                bp.mode == BlendMode::Distance ? bp.distanceMax : bp.directionalScale
            );
        }
    }
};

// Verify std140 alignment
static_assert(sizeof(MaterialLayerUBO::LayerData) % 16 == 0, "LayerData must be 16-byte aligned");
static_assert(sizeof(MaterialLayerUBO) % 16 == 0, "MaterialLayerUBO must be 16-byte aligned");

} // namespace material
