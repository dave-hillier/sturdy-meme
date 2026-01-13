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

### Phase 1: Extract LiquidComponent
- Create `LiquidComponent` struct separate from WaterUBO
- Keep WaterSystem working by composing from LiquidComponent
- Add liquid-related flags enum

### Phase 2: WeatheringComponent for Terrain
- Extract snow/wetness into WeatheringComponent
- Apply to terrain shader
- Unify with existing snow mask system

### Phase 3: MaterialLayer System
- Create MaterialLayer struct and blending infrastructure
- Implement blend modes (height, mask, slope, noise)
- Apply to terrain for multi-material support

### Phase 4: Liquid Effects on Terrain
- Allow LiquidComponent on terrain materials
- Enable puddles, rivers, wet areas
- Flow map support for terrain

### Phase 5: Generalize to All Renderables
- Extend MaterialRegistry to support component composition
- Update scene object shaders to use unified material evaluation
- Full composability across all surface types

## Benefits

| Current | Proposed |
|---------|----------|
| Water only on water plane | Water effects on any geometry |
| Separate shader per system | Unified material shader with variants |
| 40+ param water UBO | Small composable components |
| Hardcoded material blending | Flexible MaterialLayer system |
| Snow mask = terrain only | Weathering on any surface |
