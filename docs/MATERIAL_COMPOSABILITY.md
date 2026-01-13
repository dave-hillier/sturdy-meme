# Material Composability Design

## Overview

This document outlines the design for a unified, composable material system. The core principle is that materials should be composed from small, reusable **effect components** that can be applied to any surface.

## Current State

The codebase has several **siloed material systems**:
1. **MaterialRegistry** - Good foundation for scene objects (MaterialId → descriptor sets)
2. **WaterSystem** - Monolithic 40+ parameter UBO, single water plane, material blending only within water
3. **Terrain** - Separate shaders, single albedo with triplanar, no material layers
4. **Vegetation** - Doesn't use MaterialRegistry, procedural shading

**Core Problem**: Water, wetness, and surface effects are tightly coupled to specific geometry types. You can't have:
- A wet rock (water effects on non-water geometry)
- A puddle on terrain without a separate water plane
- Rain-slicked surfaces
- Rivers that flow over terrain naturally

## Material Components

Base building blocks that can be composed together:

### SurfaceComponent
Every material has this - the base PBR properties.
```cpp
struct SurfaceComponent {
    glm::vec4 baseColor;      // RGB + alpha
    float roughness;
    float metallic;
    float normalScale;
    // Texture slots: diffuse, normal, roughness, metallic, AO
};
```

### LiquidComponent
Water/liquid effects that can apply to ANY surface.
```cpp
struct LiquidComponent {
    glm::vec4 liquidColor;
    glm::vec4 absorption;     // Beer-Lambert coefficients
    float depth;              // How deep (0 = dry, >0 = wet/submerged)
    float flowSpeed;
    glm::vec2 flowDirection;
    float turbidity;
    uint32_t flags;           // CAUSTICS, FOAM, REFLECTION, SSS
};
```

### WeatheringComponent
Environmental accumulation effects.
```cpp
struct WeatheringComponent {
    float snowCoverage;       // 0-1 snow accumulation
    float wetness;            // 0-1 rain/water exposure
    float dirtAccumulation;
    float moss;
};
```

### SubsurfaceComponent
For skin, wax, leaves, water - anything with subsurface scattering.
```cpp
struct SubsurfaceComponent {
    glm::vec3 scatterColor;
    float scatterDistance;
    float intensity;
};
```

### DisplacementComponent
Height/displacement mapping and wave animation.
```cpp
struct DisplacementComponent {
    float heightScale;
    float tessellationLevel;
    uint32_t flags;           // PARALLAX, TESSELLATION, WAVE_GERSTNER
};
```

## Unified Material UBO

Instead of separate UBOs per system, compose from components:

```cpp
struct MaterialUBO {
    // Always present
    SurfaceComponent surface;

    // Optional components (controlled by feature flags)
    LiquidComponent liquid;
    WeatheringComponent weathering;
    SubsurfaceComponent subsurface;
    DisplacementComponent displacement;

    // Feature flags (use specialization constants in shader)
    uint32_t enabledFeatures;  // LIQUID | WEATHERING | SSS | DISPLACEMENT
};
```

## Water as Composable Effects

Water effects decomposed into reusable pieces:

| Effect | Can Apply To | Example Use |
|--------|--------------|-------------|
| **Flow/Animation** | Any UV-mapped surface | Rivers on terrain, waterfalls |
| **Caustics** | Surfaces below water level | Underwater terrain, rocks |
| **Foam/Bubbles** | Edges, shores, turbulence | Shore foam, rapids |
| **Reflection** | Flat/near-flat surfaces | Puddles, calm water |
| **Refraction** | Transparent surfaces | Deep water, glass |
| **Wetness** | Any surface | Rain, splashes, damp areas |

## Material Layers

Allow stacking materials with spatial blending:

```cpp
struct MaterialLayer {
    MaterialId baseMaterial;
    MaterialId overlayMaterial;

    enum BlendMode { HEIGHT, MASK, SLOPE, NOISE, WORLD_POS };
    BlendMode mode;

    Texture* blendMask;       // Optional explicit mask
    float blendThreshold;
    float blendSoftness;
    glm::vec4 blendParams;    // Mode-specific
};
```

**Use Cases**:
- Terrain: Rock + Grass + Snow (height-based)
- Roads: Asphalt + Puddles (mask-based)
- Buildings: Stone + Moss + Dirt (procedural)
- Shoreline: Sand + Wet Sand + Shallow Water (distance to water)

## Shader Architecture

Use specialization constants for efficient variants:

```glsl
layout(constant_id = 0) const bool ENABLE_LIQUID = false;
layout(constant_id = 1) const bool ENABLE_WEATHERING = false;
layout(constant_id = 2) const bool ENABLE_SSS = false;
layout(constant_id = 3) const bool ENABLE_DISPLACEMENT = false;

vec4 evaluateMaterial(MaterialUBO mat, vec2 uv, vec3 worldPos, vec3 normal) {
    vec4 result = evaluateSurface(mat.surface, uv, normal);

    if (ENABLE_WEATHERING) {
        result = applyWeathering(result, mat.weathering, worldPos, normal);
    }
    if (ENABLE_LIQUID) {
        result = applyLiquidEffects(result, mat.liquid, worldPos, uv);
    }
    if (ENABLE_SSS) {
        result = applySubsurface(result, mat.subsurface);
    }
    return result;
}
```

## Implementation Phases

### Phase 1: Extract LiquidComponent ✓
- Created `LiquidComponent` struct in `MaterialComponents.h`
- 10 presets: ocean, coastalOcean, river, muddyRiver, clearStream, lake, swamp, tropical, puddle, wetSurface
- WaterSystem updated with `setPrimaryLiquid/setSecondaryLiquid` API
- Backward compatible with existing WaterMaterial via `WaterMaterialAdapter`

### Phase 2: WeatheringComponent Infrastructure ✓
- Created `weathering_common.glsl` with wetness, dirt, moss functions
- Created `ubo_weathering.glsl` and `WeatheringUBO.h` for GPU upload
- Added shader bindings (BINDING_WEATHERING_UBO, BINDING_TERRAIN_WEATHERING_UBO)
- Complements existing volumetric snow system

### Phase 3: MaterialLayer System ✓
- Created `MaterialLayer.h` with:
  - `BlendMode` enum: Height, Slope, Mask, Noise, Distance, Directional, Altitude
  - `BlendParams` struct with mode-specific parameters
  - `MaterialLayerStack` for managing multiple layers
  - `MaterialLayerUBO` for GPU upload (up to 4 layers)
- Created `material_layer_common.glsl` with:
  - Blend factor calculation for all modes
  - FBM noise for procedural blending
  - Terrain-specific helpers (grass, rock, snow, sand factors)
  - Shore/water edge blending functions

### Phase 4: Liquid Effects on Terrain ✓
- Created `terrain_liquid_common.glsl` with:
  - `calculatePuddlePresence()`: Height/slope-aware puddle detection
  - `applyPuddleEffect()`: Water absorption, reflection, Fresnel
  - `calculateRainRipples()`: Animated ripple normals
  - `applyWetSurface()`: Simple dampness without standing water
  - `applyStreamEffect()`: Flowing water with animation
  - `applyShoreWetness()`: Graduated wetness near water bodies
  - `applyTerrainLiquidEffects()`: Combined effect application
- Created `TerrainLiquidUBO.h`:
  - GPU-compatible struct for puddles, streams, wetness
  - Weather presets: dryConditions, lightRain, heavyRain, afterRain
  - Integration with LiquidComponent presets
- Added BINDING_TERRAIN_LIQUID_UBO (29)

### Phase 5: Generalize to All Renderables ✓ + Terrain Integration
- Integrated terrain liquid effects into terrain.frag:
  - Added `#include "terrain_liquid_common.glsl"`
  - Added TerrainLiquidUniforms UBO at binding 29
  - Applied puddle/wetness effects after snow layer
  - Added liquid reflections to final color output
- Updated TerrainSystem for liquid UBO management:
  - `setLiquidWetness(float)` - simple wetness control
  - `setLiquidConfig(TerrainLiquidUBO)` - full configuration
  - Animation time updated each frame for rain ripples
- Updated TerrainBuffers with liquid UBO allocation
- Updated descriptor set layout with binding 29

**Usage: Connect WeatherSystem to Terrain**
```cpp
// In your renderer update loop:
if (weatherSystem && terrainSystem) {
    float rainIntensity = weatherSystem->getIntensity();
    // Rain type is 0, snow is 1
    if (weatherSystem->getWeatherType() == 0) {
        terrainSystem->setLiquidWetness(rainIntensity);
    }
}
```

- Created `ComposedMaterialRegistry` class with RAII GPU resource management:
  - `registerMaterial()` for composed materials with components
  - `createGPUResources()` allocates per-frame UBOs
  - `updateUBO()` / `updateAllUBOs()` for dirty tracking and upload
  - `setGlobalWeather()` for weather override system
  - Smart pointers (`std::unique_ptr<GPUBuffer>`) for automatic cleanup
- Created `ComposedMaterialUBO` for GPU upload:
  - std140-aligned struct packing all components
  - `fromMaterial()` converts ComposedMaterial to GPU format
  - `PackedSurfaceUBO` for minimal surface-only materials
- Created `material_evaluate.glsl` unified shader:
  - `MaterialInputs` / `MaterialResult` structures
  - Component evaluators: surface, liquid, weathering, subsurface, emissive
  - `evaluateMaterial()` main entry point
  - Feature flag checks for conditional evaluation
- Added `BINDING_COMPOSED_MATERIAL_UBO` (20) for shader binding

## Benefits

| Current | Proposed |
|---------|----------|
| Water only on water plane | Water effects on any geometry |
| Separate shader per system | Unified material shader with variants |
| 40+ param water UBO | Small composable components |
| Hardcoded material blending | Flexible MaterialLayer system |
| Snow mask = terrain only | Weathering on any surface |
