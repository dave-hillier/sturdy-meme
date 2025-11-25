# Lighting, Atmospheric Effects & Shadows Implementation Plan

A step-by-step guide to implementing cinematic lighting inspired by Ghost of Tsushima's rendering techniques, adapted for this Vulkan game engine.

## Document Structure

This implementation plan is split across multiple documents for easier navigation:

| Document | Description |
|----------|-------------|
| [Overview](LIGHTING_OVERVIEW.md) | Introduction, phase dependencies, implementation order (this document) |
| [Phase 1: PBR Lighting](LIGHTING_PHASE1_PBR.md) | Foundation - Light data structures, PBR model, normal mapping |
| [Phase 2: Shadow Mapping](LIGHTING_PHASE2_SHADOWS.md) | Basic shadows, CSM, quality improvements |
| [Phase 3: Indirect Lighting](LIGHTING_PHASE3_INDIRECT.md) | SH probes, reflection probes, horizon occlusion |
| [Phase 4: Atmospheric Scattering](LIGHTING_PHASE4_ATMOSPHERE.md) | Sky model, volumetric clouds, haze/fog, god rays |
| [Phase 5: Post-Processing](LIGHTING_PHASE5_POSTPROCESS.md) | HDR, exposure, tone mapping, color grading |
| [Phase 6: Integration](LIGHTING_PHASE6_INTEGRATION.md) | Time of day, weather, optimization, Vulkan appendix |

---

## Current State

- Basic forward rendering pipeline with depth buffer
- Single hardcoded directional light in fragment shader
- Simple diffuse lighting (Lambertian with 0.3 ambient floor)
- No shadows, specular, or atmospheric effects
- No post-processing pipeline

---

## Phase Dependencies & Alternatives

Not all phases are equally essential. Here's how they relate:

**Required foundation:**
- **Phase 1** (PBR lighting) and **Phase 2** (shadows) are foundational for any modern look

**Optional enhancements:**
- **Phase 3** (probes) and **Phase 4** (atmosphere) add polish but are more independent of each other

### Recommended Implementation Order

1. **Phase 1** - PBR with simple hemisphere ambient
2. **Phase 2** - Shadows (even basic shadow mapping transforms a scene)
3. **Phase 4** - Atmospheric scattering (big visual impact, independent of probes)
4. **Phase 3** - Add probes later if needed for cinematic quality

### Phase 3 Alternatives (Lighter-Weight Options)

The SH probe system provides high-quality indirect lighting but requires offline baking and assumes static geometry. If this complexity isn't needed, consider these alternatives:

| Instead of... | Use... | Quality |
|---------------|--------|---------|
| SH irradiance probes | Constant ambient + AO | Acceptable for stylized |
| SH irradiance probes | Hemisphere lighting (sky vs ground color) | Better, still cheap |
| Reflection probes | Single skybox cubemap | Flat but functional |
| Reflection probes | Screen-space reflections | Good for planar surfaces |

The atmospheric sky model from Phase 4 can serve as a cheap ambient source - sampling the sky LUT provides some directional fill light without full probe baking.

Many shipped games use "PBR + shadows + atmosphere + simple ambient" and achieve good results. Probes push from "good game lighting" toward "cinematic rendering" but aren't strictly required.

---

## Implementation Order (Recommended)

| Priority | Phase | Feature | Complexity | Impact |
|----------|-------|---------|------------|--------|
| 1 | 1.1 | Light uniforms | Low | Foundation |
| 2 | 1.2 | PBR lighting | Medium | Visual quality |
| 3 | 2.1 | Basic shadow map | Medium | Grounding |
| 4 | 5.1 | HDR pipeline | Medium | Enables post-FX |
| 5 | 5.5 | Tone mapping | Low | Visual quality |
| 6 | 2.2 | Cascaded shadows | High | Shadow quality |
| 7 | 4.1 | Sky model | High | Atmosphere |
| 8 | 4.3 | Volumetric haze | High | Atmosphere |
| 9 | 3.1 | SH probes | High | Indirect light |
| 10 | 4.2 | Volumetric clouds | Very High | Dramatic skies |
| 11 | 5.3 | Local tone mapping | Medium | HDR handling |
| 12 | 3.2 | Reflection probes | High | Specular IBL |
| 13 | 5.6 | Purkinje effect | Medium | Night scenes |

---

## References

### Papers & Presentations
- Bruneton & Neyret, "Precomputed Atmospheric Scattering" (2008)
- Hillaire, "A Scalable and Production Ready Sky and Atmosphere" (2020)
- Schneider, "The Real-time Volumetric Cloudscapes of Horizon Zero Dawn" (2015)
- Wronski, "Volumetric Fog and Lighting" (2014)
- Petri, "Samurai Cinema: Creating a Real-Time Cinematic Experience" (GDC 2021)

### Code Resources
- learnopengl.com - PBR, shadows, HDR tutorials
- github.com/sebh/UnrealEngineSkyAtmosphere - Atmosphere implementation
- Filament renderer source code - PBR reference
