#pragma once

#include <glm/glm.hpp>
#include <cstdint>

/**
 * Material Components - Composable building blocks for the unified material system
 *
 * Design Philosophy:
 * - Small, focused structs that describe one aspect of a material
 * - Can be composed together to create complex materials
 * - Each component can be applied to any surface type
 * - Feature flags control which components are active in shaders
 *
 * See docs/MATERIAL_COMPOSABILITY.md for the full design document.
 */

namespace material {

// Feature flags for enabling material components
// These map to shader specialization constants
enum class FeatureFlags : uint32_t {
    None = 0,
    Liquid = 1 << 0,       // Water/liquid effects (flow, caustics, foam)
    Weathering = 1 << 1,   // Environmental accumulation (snow, wetness, dirt)
    Subsurface = 1 << 2,   // Subsurface scattering
    Displacement = 1 << 3, // Height/displacement mapping
    Emissive = 1 << 4,     // Emissive/glow effects
};

inline FeatureFlags operator|(FeatureFlags a, FeatureFlags b) {
    return static_cast<FeatureFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline FeatureFlags operator&(FeatureFlags a, FeatureFlags b) {
    return static_cast<FeatureFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool hasFeature(FeatureFlags flags, FeatureFlags feature) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(feature)) != 0;
}

// Liquid-specific feature flags
enum class LiquidFlags : uint32_t {
    None = 0,
    Caustics = 1 << 0,     // Underwater caustics patterns
    Foam = 1 << 1,         // Surface foam (shore, turbulence)
    Reflection = 1 << 2,   // Screen-space or cubemap reflections
    Refraction = 1 << 3,   // Refraction through transparent liquid
    Flow = 1 << 4,         // Animated flow using flow maps
    Waves = 1 << 5,        // Wave animation (Gerstner or FFT)
    Subsurface = 1 << 6,   // Subsurface scattering in liquid

    // Common combinations
    FullWater = Caustics | Foam | Reflection | Refraction | Flow | Waves | Subsurface,
    Puddle = Reflection | Refraction,
    Wetness = None,  // Just changes surface properties, no visual liquid
    Stream = Flow | Foam | Refraction,
};

inline LiquidFlags operator|(LiquidFlags a, LiquidFlags b) {
    return static_cast<LiquidFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline LiquidFlags operator&(LiquidFlags a, LiquidFlags b) {
    return static_cast<LiquidFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool hasLiquidFeature(LiquidFlags flags, LiquidFlags feature) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(feature)) != 0;
}

/**
 * SurfaceComponent - Base PBR surface properties
 *
 * Every material has a surface component. This defines the fundamental
 * appearance: color, roughness, metallic, and normal intensity.
 * Texture slots provide spatial variation.
 */
struct SurfaceComponent {
    glm::vec4 baseColor = glm::vec4(1.0f);  // RGB + alpha
    float roughness = 0.5f;                   // 0 = mirror, 1 = diffuse
    float metallic = 0.0f;                    // 0 = dielectric, 1 = metal
    float normalScale = 1.0f;                 // Normal map intensity
    float aoStrength = 1.0f;                  // Ambient occlusion strength

    // Factory methods for common surface types
    static SurfaceComponent defaultSurface() {
        return SurfaceComponent{};
    }

    static SurfaceComponent metal(const glm::vec3& color, float rough = 0.3f) {
        SurfaceComponent s;
        s.baseColor = glm::vec4(color, 1.0f);
        s.roughness = rough;
        s.metallic = 1.0f;
        return s;
    }

    static SurfaceComponent dielectric(const glm::vec3& color, float rough = 0.5f) {
        SurfaceComponent s;
        s.baseColor = glm::vec4(color, 1.0f);
        s.roughness = rough;
        s.metallic = 0.0f;
        return s;
    }
};

/**
 * LiquidComponent - Water/liquid effects
 *
 * Can be applied to any surface to add water-like behavior:
 * - Full water bodies (oceans, lakes, rivers)
 * - Puddles on terrain or roads
 * - Wet surfaces (rain, splashes)
 * - Flowing water on any geometry
 *
 * The 'depth' field controls intensity: 0 = dry, small values = wet, large = submerged
 */
struct LiquidComponent {
    glm::vec4 color = glm::vec4(0.0f, 0.3f, 0.5f, 0.8f);  // RGB + transparency
    glm::vec4 absorption = glm::vec4(0.4f, 0.08f, 0.04f, 0.1f);  // RGB coefficients + turbidity

    float depth = 1.0f;              // Liquid depth (0 = dry surface, >0 = in liquid)
    float absorptionScale = 1.0f;    // How quickly light is absorbed
    float scatteringScale = 0.3f;    // Turbidity/scattering multiplier
    float roughness = 0.02f;         // Surface roughness (calm = low, choppy = high)

    glm::vec2 flowDirection = glm::vec2(0.0f);  // Flow direction (normalized)
    float flowSpeed = 0.0f;          // Flow animation speed
    float flowStrength = 0.0f;       // UV distortion strength

    float foamIntensity = 0.0f;      // Foam at edges/turbulence
    float sssIntensity = 0.3f;       // Subsurface scattering intensity
    float fresnelPower = 5.0f;       // Fresnel reflection power
    float refractionStrength = 1.0f; // Refraction distortion

    LiquidFlags flags = LiquidFlags::FullWater;

    // Factory methods for common liquid types
    static LiquidComponent ocean();
    static LiquidComponent coastalOcean();
    static LiquidComponent river();
    static LiquidComponent muddyRiver();
    static LiquidComponent clearStream();
    static LiquidComponent lake();
    static LiquidComponent swamp();
    static LiquidComponent tropical();
    static LiquidComponent puddle();
    static LiquidComponent wetSurface(float wetness = 0.5f);
};

/**
 * WeatheringComponent - Environmental accumulation effects
 *
 * Applies weather-based surface modifications:
 * - Snow coverage based on world position/normal
 * - Wetness from rain or proximity to water
 * - Dirt/grime accumulation
 * - Moss/vegetation growth
 */
struct WeatheringComponent {
    float snowCoverage = 0.0f;       // 0-1 snow accumulation
    float snowBlendSharpness = 2.0f; // How sharp the snow edge is
    glm::vec3 snowColor = glm::vec3(0.95f, 0.95f, 0.98f);
    float snowRoughness = 0.8f;

    float wetness = 0.0f;            // 0-1 surface wetness (darkens, lowers roughness)
    float wetnessRoughnessScale = 0.3f;  // Roughness multiplier when wet

    float dirtAccumulation = 0.0f;   // 0-1 dirt coverage
    glm::vec3 dirtColor = glm::vec3(0.3f, 0.25f, 0.2f);

    float moss = 0.0f;               // 0-1 moss/vegetation growth
    glm::vec3 mossColor = glm::vec3(0.2f, 0.35f, 0.15f);

    // Factory methods
    static WeatheringComponent none() {
        return WeatheringComponent{};
    }

    static WeatheringComponent snowy(float coverage = 0.8f) {
        WeatheringComponent w;
        w.snowCoverage = coverage;
        return w;
    }

    static WeatheringComponent wet(float amount = 0.7f) {
        WeatheringComponent w;
        w.wetness = amount;
        return w;
    }

    static WeatheringComponent weathered(float dirt = 0.3f, float mossAmt = 0.2f) {
        WeatheringComponent w;
        w.dirtAccumulation = dirt;
        w.moss = mossAmt;
        return w;
    }
};

/**
 * SubsurfaceComponent - Subsurface scattering
 *
 * For translucent materials like skin, wax, leaves, marble.
 * Light penetrates the surface and scatters internally.
 */
struct SubsurfaceComponent {
    glm::vec3 scatterColor = glm::vec3(1.0f, 0.2f, 0.1f);  // Scattered light color
    float scatterDistance = 0.1f;    // How far light travels inside
    float intensity = 0.5f;          // Overall SSS strength
    float distortion = 0.5f;         // View-dependent distortion

    // Factory methods
    static SubsurfaceComponent skin() {
        SubsurfaceComponent s;
        s.scatterColor = glm::vec3(1.0f, 0.35f, 0.2f);
        s.scatterDistance = 0.15f;
        s.intensity = 0.6f;
        return s;
    }

    static SubsurfaceComponent leaf() {
        SubsurfaceComponent s;
        s.scatterColor = glm::vec3(0.5f, 0.8f, 0.3f);
        s.scatterDistance = 0.05f;
        s.intensity = 0.4f;
        return s;
    }

    static SubsurfaceComponent wax() {
        SubsurfaceComponent s;
        s.scatterColor = glm::vec3(1.0f, 0.9f, 0.7f);
        s.scatterDistance = 0.2f;
        s.intensity = 0.7f;
        return s;
    }
};

/**
 * DisplacementComponent - Height/displacement mapping
 *
 * Modifies surface geometry or applies parallax effects.
 * Can use tessellation for true displacement or parallax for approximation.
 */
struct DisplacementComponent {
    float heightScale = 0.1f;        // Maximum displacement distance
    float midLevel = 0.5f;           // Height map value that means no displacement
    int tessellationLevel = 4;       // Tessellation factor (if using tessellation)
    bool useParallax = true;         // Use parallax mapping instead of tessellation
    int parallaxSteps = 8;           // Parallax occlusion mapping steps

    // Wave-specific displacement (for water)
    float waveAmplitude = 0.0f;
    float waveFrequency = 1.0f;
    float waveSpeed = 1.0f;
};

/**
 * EmissiveComponent - Self-illumination
 *
 * For glowing materials, screens, lava, etc.
 */
struct EmissiveComponent {
    glm::vec3 emissiveColor = glm::vec3(1.0f);
    float intensity = 1.0f;          // HDR intensity multiplier
    float pulseSpeed = 0.0f;         // Animated pulsing (0 = static)
    float pulseMin = 0.5f;           // Minimum intensity during pulse
};

/**
 * ComposedMaterial - A material built from components
 *
 * Combines multiple components with feature flags indicating which are active.
 * Shaders use specialization constants based on enabledFeatures.
 */
struct ComposedMaterial {
    SurfaceComponent surface;
    LiquidComponent liquid;
    WeatheringComponent weathering;
    SubsurfaceComponent subsurface;
    DisplacementComponent displacement;
    EmissiveComponent emissive;

    FeatureFlags enabledFeatures = FeatureFlags::None;

    // Builder-style methods for fluent construction
    ComposedMaterial& withSurface(const SurfaceComponent& s) {
        surface = s;
        return *this;
    }

    ComposedMaterial& withLiquid(const LiquidComponent& l) {
        liquid = l;
        enabledFeatures = enabledFeatures | FeatureFlags::Liquid;
        return *this;
    }

    ComposedMaterial& withWeathering(const WeatheringComponent& w) {
        weathering = w;
        enabledFeatures = enabledFeatures | FeatureFlags::Weathering;
        return *this;
    }

    ComposedMaterial& withSubsurface(const SubsurfaceComponent& s) {
        subsurface = s;
        enabledFeatures = enabledFeatures | FeatureFlags::Subsurface;
        return *this;
    }

    ComposedMaterial& withDisplacement(const DisplacementComponent& d) {
        displacement = d;
        enabledFeatures = enabledFeatures | FeatureFlags::Displacement;
        return *this;
    }

    ComposedMaterial& withEmissive(const EmissiveComponent& e) {
        emissive = e;
        enabledFeatures = enabledFeatures | FeatureFlags::Emissive;
        return *this;
    }
};

/**
 * WaterMaterialAdapter - Conversion utilities for backward compatibility
 *
 * These functions allow converting between the new LiquidComponent and
 * the legacy WaterSystem::WaterMaterial struct. This enables gradual
 * migration while maintaining compatibility.
 */
struct WaterMaterialAdapter {
    // Convert from legacy WaterMaterial fields to LiquidComponent
    // This function takes the individual fields rather than the struct
    // to avoid circular header dependencies
    static LiquidComponent fromWaterMaterial(
        const glm::vec4& waterColor,
        const glm::vec4& scatteringCoeffs,
        float absorptionScale,
        float scatteringScale,
        float specularRoughness,
        float sssIntensity)
    {
        LiquidComponent l;
        l.color = waterColor;
        l.absorption = scatteringCoeffs;
        l.absorptionScale = absorptionScale;
        l.scatteringScale = scatteringScale;
        l.roughness = specularRoughness;
        l.sssIntensity = sssIntensity;
        l.flags = LiquidFlags::FullWater;
        return l;
    }

    // Extract WaterMaterial-compatible fields from LiquidComponent
    // Returns values in the order: waterColor, scatteringCoeffs, absorptionScale,
    // scatteringScale, specularRoughness, sssIntensity
    static void toWaterMaterialFields(
        const LiquidComponent& liquid,
        glm::vec4& outWaterColor,
        glm::vec4& outScatteringCoeffs,
        float& outAbsorptionScale,
        float& outScatteringScale,
        float& outSpecularRoughness,
        float& outSssIntensity)
    {
        outWaterColor = liquid.color;
        outScatteringCoeffs = liquid.absorption;
        outAbsorptionScale = liquid.absorptionScale;
        outScatteringScale = liquid.scatteringScale;
        outSpecularRoughness = liquid.roughness;
        outSssIntensity = liquid.sssIntensity;
    }
};

/**
 * LiquidType - Named liquid presets (mirrors WaterSystem::WaterType)
 *
 * Provides a standalone enum for liquid types that can be used
 * independently of WaterSystem.
 */
enum class LiquidType {
    Ocean,
    CoastalOcean,
    River,
    MuddyRiver,
    ClearStream,
    Lake,
    Swamp,
    Tropical,
    Puddle,
    WetSurface
};

// Get a LiquidComponent preset by type
inline LiquidComponent getLiquidPreset(LiquidType type) {
    switch (type) {
        case LiquidType::Ocean: return LiquidComponent::ocean();
        case LiquidType::CoastalOcean: return LiquidComponent::coastalOcean();
        case LiquidType::River: return LiquidComponent::river();
        case LiquidType::MuddyRiver: return LiquidComponent::muddyRiver();
        case LiquidType::ClearStream: return LiquidComponent::clearStream();
        case LiquidType::Lake: return LiquidComponent::lake();
        case LiquidType::Swamp: return LiquidComponent::swamp();
        case LiquidType::Tropical: return LiquidComponent::tropical();
        case LiquidType::Puddle: return LiquidComponent::puddle();
        case LiquidType::WetSurface: return LiquidComponent::wetSurface();
    }
    return LiquidComponent::ocean();  // Default
}

} // namespace material
