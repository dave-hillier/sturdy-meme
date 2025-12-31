# Unified Froxel-Based Volumetric Rendering System

Implementation plan based on "Real-Time Underwater Spectral Rendering" (Monzon et al., EUROGRAPHICS 2024).

## Goal

Unify atmospheric fog and underwater volumetrics into a single froxel-based system that works seamlessly above and below water with smooth transitions at the water surface.

## Current State

| Component | Above Water | Underwater |
|-----------|-------------|------------|
| Volumetrics | FroxelSystem (128×64×64) | Screen-space Beer-Lambert |
| Phase Function | Henyey-Greenstein g=0.7 | N/A |
| Scattering | Height-based density | RGB absorption only |
| Multiple Scatter | Ambient term only | None |

**Problem**: Two separate code paths, froxels disabled underwater, no smooth transition.

## Target Architecture

```
WaterSystem (optical properties)
         │
         ▼
FroxelSystem.recordFroxelUpdate()
         │
         ▼
┌────────────────────────────────┐
│   froxel_update.comp           │
│   • Detect medium per froxel   │
│   • Air: height-based fog      │
│   • Water: absorption+scatter  │
│   • Blend at surface           │
└────────────────────────────────┘
         │
         ▼
┌────────────────────────────────┐
│   froxel_integrate.comp        │
│   • Front-to-back integration  │
│   • Kd multiple scattering     │
│   • Seafloor upwelling         │
└────────────────────────────────┘
         │
         ▼
┌────────────────────────────────┐
│   postprocess.frag             │
│   • Unified froxel sampling    │
│   • Snell's window (if underwater) │
└────────────────────────────────┘
```

---

## Implementation Phases

### Phase 1: Water Parameters in FroxelUniforms

**Files**: `FroxelSystem.h`, `FroxelSystem.cpp`

Add water optical properties to the froxel uniform buffer:

```glsl
// Add to FroxelUniforms UBO
vec4 waterParams;       // x=waterLevel, y=phaseG_water(0.8), z=enabled, w=transitionThickness
vec4 waterAbsorption;   // rgb=absorption coeffs (from scatteringCoeffs.rgb), a=Kd_factor
vec4 waterScattering;   // rgb=scattering coeffs (derived from turbidity), a=multiScatterStrength
vec4 seafloorParams;    // x=reflectance, y=maxUpwellingDist, z=unused, w=unused
```

Add setters to FroxelSystem:
- `setWaterLevel(float level)`
- `setWaterOpticalProperties(vec3 absorption, float turbidity, float Kd)`
- `setSeafloorReflectance(float r)`

Wire Renderer to pass WaterSystem parameters to FroxelSystem each frame.

**Test**: Parameters visible in shader debugger, no visual change yet.

---

### Phase 2: Medium Detection in froxel_update.comp

**Files**: `froxel_update.comp`

Each froxel determines its medium based on world position:

```glsl
float waterLevel = ubo.waterParams.x;
float transitionThickness = ubo.waterParams.w;  // e.g., 0.3m

// Smooth blend at surface
float surfaceBlend = smoothstep(
    waterLevel - transitionThickness,
    waterLevel + transitionThickness,
    worldPos.y
);
// surfaceBlend: 0 = fully underwater, 1 = fully above water

bool isUnderwater = surfaceBlend < 0.5;
```

Create helper function:
```glsl
struct MediumProperties {
    vec3 scattering;
    vec3 absorption;
    float phaseG;
    float density;
};

MediumProperties getMediumProperties(vec3 worldPos, float surfaceBlend) {
    MediumProperties air, water, result;

    // Air: existing height-based fog
    air.density = getHazeDensity(worldPos);
    air.scattering = vec3(air.density);
    air.absorption = vec3(ubo.fogParams.w * air.density);
    air.phaseG = 0.7;

    // Water: from uniforms
    water.scattering = ubo.waterScattering.rgb;
    water.absorption = ubo.waterAbsorption.rgb;
    water.phaseG = ubo.waterParams.y;  // 0.8
    water.density = length(water.scattering);

    // Blend at surface
    result.scattering = mix(water.scattering, air.scattering, surfaceBlend);
    result.absorption = mix(water.absorption, air.absorption, surfaceBlend);
    result.phaseG = mix(water.phaseG, air.phaseG, surfaceBlend);
    result.density = mix(water.density, air.density, surfaceBlend);

    return result;
}
```

**Test**: Underwater froxels show water-colored scattering, smooth transition at surface.

---

### Phase 3: Water Scattering Model

**Files**: `froxel_update.comp`

Implement depth-dependent attenuation for underwater single scattering:

```glsl
vec3 computeWaterSingleScatter(vec3 worldPos, vec3 viewDir, vec3 sunDir,
                                vec3 absorption, vec3 scattering, float shadow) {
    float waterLevel = ubo.waterParams.x;
    float depthBelowSurface = max(waterLevel - worldPos.y, 0.0);

    // Sun light attenuated by water column above this point
    // Account for sun angle (refracted)
    float sunAngleFactor = 1.0 / max(abs(sunDir.y), 0.1);
    vec3 sunTransmittance = exp(-absorption * depthBelowSurface * sunAngleFactor);

    // Phase function (forward scattering in water)
    float cosTheta = dot(viewDir, sunDir);
    float phase = henyeyGreenstein(cosTheta, 0.8);

    // Single scattering
    return scattering * phase * ubo.sunColor.rgb * ubo.toSunDirection.w
           * sunTransmittance * shadow;
}
```

Modify main() to use medium-appropriate scattering:
```glsl
MediumProperties medium = getMediumProperties(worldPos, surfaceBlend);

vec3 inScatter;
if (surfaceBlend < 0.99) {
    // Has water contribution
    vec3 waterScatter = computeWaterSingleScatter(...);
    vec3 airScatter = sunInScatter + skyInScatter;  // existing
    inScatter = mix(waterScatter, airScatter, surfaceBlend);
} else {
    inScatter = sunInScatter + skyInScatter;  // existing path
}
```

**Test**: Underwater shows depth-dependent blue attenuation, sun shafts visible through water.

---

### Phase 4: Kd Multiple Scattering Approximation

**Files**: `froxel_integrate.comp`

Implement O(1) multiple scattering from paper's Equation 9:

```glsl
// Compute diffuse downwelling attenuation coefficient
// Kd ≈ absorption + backscatter (simplified from full radiative transfer)
vec3 computeKd(vec3 absorption, vec3 scattering) {
    float backscatterRatio = 0.018;  // Typical for oceanic water
    return absorption + scattering * backscatterRatio;
}

// In integration loop, after single scatter accumulation:
if (worldPos.y < ubo.waterParams.x && ubo.waterParams.z > 0.5) {
    vec3 Kd = computeKd(ubo.waterAbsorption.rgb, ubo.waterScattering.rgb);
    float depthFromSurface = ubo.waterParams.x - worldPos.y;

    // Downwelling irradiance at this depth (Equation 6 from paper)
    vec3 Ed = ubo.sunColor.rgb * exp(-Kd * depthFromSurface);

    // Multiple scattering contribution (isotropic)
    float multiScatterStrength = ubo.waterScattering.a;
    vec3 Lms = ubo.waterScattering.rgb * Ed * multiScatterStrength / (4.0 * PI);

    // Add to accumulated scatter
    accumulatedScatter += accumulatedTransmittance * Lms * sliceThickness;
}
```

**Test**: Deep water has subtle ambient glow, not complete blackness.

---

### Phase 5: Seafloor Upwelling (Optional Enhancement)

**Files**: `froxel_update.comp`

Add upwelling light reflected from seafloor:

```glsl
// Requires terrain height texture bound to froxel descriptor set
// Add: layout(binding = BINDING_FROXEL_TERRAIN) uniform sampler2D terrainHeight;

vec3 computeSeafloorUpwelling(vec3 worldPos, vec3 absorption) {
    float waterLevel = ubo.waterParams.x;
    if (worldPos.y >= waterLevel) return vec3(0.0);

    // Sample terrain height at this XZ position
    vec2 terrainUV = worldPos.xz / ubo.terrainSize + 0.5;
    float terrainHeight = texture(terrainHeight, terrainUV).r * ubo.terrainHeightScale;

    float heightAboveSeafloor = worldPos.y - terrainHeight;
    float maxUpwellingDist = ubo.seafloorParams.y;  // e.g., 10m

    if (heightAboveSeafloor > maxUpwellingDist || heightAboveSeafloor < 0.0)
        return vec3(0.0);

    // Light that reached seafloor
    float depthToSeafloor = waterLevel - terrainHeight;
    vec3 downwelling = exp(-absorption * depthToSeafloor) * ubo.sunColor.rgb;

    // Reflected and attenuated back up
    float reflectance = ubo.seafloorParams.x;  // e.g., 0.2 for sand
    vec3 upwelling = reflectance * downwelling * exp(-absorption * heightAboveSeafloor);

    // Fade with distance from floor
    float fade = 1.0 - (heightAboveSeafloor / maxUpwellingDist);

    return upwelling * fade * ubo.waterScattering.rgb / (4.0 * PI);
}
```

**Test**: Shallow water over sand has subtle upward glow.

---

### Phase 6: Unify PostProcess Consumption

**Files**: `postprocess.frag`

Remove separate underwater path, use unified froxel volume:

```glsl
// BEFORE (two separate paths):
if (ubo.froxelEnabled > 0.5 && ubo.underwaterEnabled < 0.5) {
    // froxel fog
}
if (ubo.underwaterEnabled > 0.5) {
    hdr = applyUnderwaterEffects(hdr, fragTexCoord, linearDepth);
}

// AFTER (unified):
if (ubo.froxelEnabled > 0.5) {
    vec4 fog = sampleFroxelFog(fragTexCoord, linearDepth);
    hdr = hdr * fog.a + fog.rgb;  // Same for air and water
}

// Snell's window is view-dependent, keep as separate effect
if (ubo.underwaterEnabled > 0.5 && ubo.underwaterDepth > 0.5) {
    hdr = applySnellsWindow(hdr, fragTexCoord);
}
```

Can remove or simplify:
- `applyUnderwaterFog()` - replaced by froxel volume
- `applyUnderwaterEffects()` - simplified to just Snell's window

**Test**: Visual quality matches or exceeds separate systems, single code path.

---

## Water Type Integration

Existing water types automatically flow into unified system:

| WaterType | Absorption (RGB) | Turbidity | Derived Kd |
|-----------|------------------|-----------|------------|
| Ocean | (0.45, 0.09, 0.02) | 0.05 | ~(0.45, 0.09, 0.02) |
| CoastalOcean | (0.35, 0.12, 0.05) | 0.15 | ~(0.35, 0.12, 0.05) |
| Tropical | (0.55, 0.06, 0.03) | 0.03 | ~(0.55, 0.06, 0.03) |
| Swamp | (0.10, 0.15, 0.20) | 0.80 | Higher scattering |

The scattering coefficient is derived from turbidity:
```cpp
vec3 scattering = vec3(turbidity * 0.5);  // Simplified
```

For more accuracy, use wavelength-dependent scattering per paper's Jerlov data.

---

## Performance Considerations

1. **Early-out for no water**: Skip water calculations if `waterParams.z < 0.5`
2. **Shared phase function**: `henyeyGreenstein()` already exists in `dynamic_lights_common.glsl`
3. **Terrain sampling**: Only needed for seafloor upwelling, can be disabled
4. **Same froxel resolution**: 128×64×64 sufficient for both media

Expected performance impact: <1ms additional GPU time.

---

## Files to Modify Summary

| File | Changes |
|------|---------|
| `src/lighting/FroxelSystem.h` | Add water parameter setters and UBO fields |
| `src/lighting/FroxelSystem.cpp` | Wire water params to shader, add terrain binding |
| `shaders/froxel_update.comp` | Medium detection, water scattering, surface blend |
| `shaders/froxel_integrate.comp` | Kd multiple scattering approximation |
| `shaders/postprocess.frag` | Unified consumption, simplify underwater path |
| `src/core/pipeline/RenderPipelineFactory.cpp` | Pass water params to froxel recording |

---

## Testing Checklist

- [ ] Build compiles without errors
- [ ] Above-water fog unchanged (regression)
- [ ] Underwater shows blue attenuation
- [ ] Water surface transition is smooth
- [ ] Deep water has ambient glow (multiple scatter)
- [ ] Sun shafts visible underwater
- [ ] Different water types show correct colors
- [ ] Performance maintains 60fps
- [ ] Snell's window still works when looking up

---

## Future Extensions

1. **Spectral rendering**: Extend to 8 wavelengths using dual RGBA volumes
2. **Caustics integration**: Project caustics into froxel volume
3. **Dynamic Kd**: Compute Kd from IOPs instead of approximation
4. **Sensor response curves**: Add camera simulation for scientific visualization
