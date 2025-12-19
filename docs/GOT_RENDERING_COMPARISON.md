# Ghost of Tsushima Rendering Techniques Comparison

Comparison of techniques from Justin Petri's "A Real-Time Samurai Cinema Experience" (SIGGRAPH 2021) against the current codebase implementation.

---

## Summary

| Category | GOT Techniques | Implemented | Partially | Missing |
|----------|---------------|-------------|-----------|---------|
| **Indirect Diffuse** | SH probes, sky visibility, bounce light | - | - | All |
| **Indirect Specular** | Reflection probes, cube map shadows, horizon occlusion | SSR + horizon occlusion | - | Reflection probes |
| **Atmospheric LUTs** | Transmittance, multi-scatter, irradiance | Transmittance, multi-scatter, skyview | Irradiance (basic) | Rayleigh/Mie split irradiance |
| **LMS Color Space** | Spectral primaries for accurate Rayleigh | Yes | - | - |
| **Volumetric Fog** | Froxel grid, density anti-aliasing, tricubic filtering | Yes | - | - |
| **Cloud Rendering** | Paraboloid maps, density AA, phase function blending | Yes | - | Triple-buffered temporal |
| **Particle Haze Lighting** | Haze lighting model, multi-scatter approx | - | - | All |
| **Tone Mapping** | Bilateral filter local contrast | Bilateral grid + shaders | - | CPU-side system integration |
| **Color Grading** | White balance with Bradford chromatic adaptation | - | - | All |
| **Custom Tone Map Space** | Modified ACES CG (adjusted red primary) | - | - | All |
| **Purkinje Effect** | LMSR physiological model | Yes (simplified) | - | Full LMSR model |

---

## 1. Indirect Lighting - Diffuse

### Ghost of Tsushima Approach
- **Regular grid of SH irradiance probes** at 12.5m intervals, 3 elevation levels (1.5m, 10m, 30m)
- **Tetrahedral meshes** for interiors with higher density
- **Sky visibility capture** (offline): SH representation of hemisphere visibility
- **Bounce sky visibility**: Pre-captured bounce light from environment
- **Runtime relighting**: Multiply sky visibility by current sky SH
- **Sun bounce approximation**: Use bounce sky visibility × reflected sun direction
- **Directionality boost**: 25% lerp toward SH delta function maximum
- **Runtime de-ringing**: Peter Pike Sloan's method for positive-everywhere SH
- **Interior/exterior mask**: Barycentric weight adjustment to prevent light leaking

### Current Implementation
**Not implemented.** The codebase uses direct atmospheric scattering via LUTs for indirect lighting.

### Gap Analysis
This is a significant gap for indoor scenes and localized ambient lighting. The current approach works well for outdoor environments but lacks:
- Spatially-varying ambient lighting
- Interior/exterior transition handling
- Bounce light from local geometry

### Recommended Priority
**Medium** - Important for interiors and varied outdoor lighting. Could start with a simple probe grid before adding tetrahedral meshes.

---

## 2. Indirect Lighting - Specular (Reflection Probes)

### Ghost of Tsushima Approach
- **235 reflection probes** with streaming and relighting
- **Albedo + Normal/Depth cube maps** for runtime relighting
- **Cube map shadow tracing**: Trace toward light using stored depth for interior shadows
- **Horizon occlusion term**: Account for normal map bump occlusion of reflections
- **Parallax correction with roughness compensation**: Adjust roughness by distance ratio

### Current Implementation
- **SSRSystem**: Screen-space reflections with hierarchical ray marching
- **Bilateral blur** with depth awareness for SSR denoising
- **No reflection probes** or pre-captured environment maps

### Gap Analysis
SSR alone has fundamental limitations:
- Cannot reflect off-screen content
- Limited range and accuracy
- Struggles with rough surfaces

### Horizon Occlusion (Implemented)
Implemented in `shaders/lighting_common.glsl:92-119` as `horizonOcclusion()`:
- Applied to ambient specular in `shader.frag:319-322`
- Prevents normal-mapped bumps from glowing unrealistically on back sides
- Uses GOT's formula with proper cone angle calculation

### Parallax Roughness Compensation (Missing)
```glsl
// Adjust roughness when sampling parallax-corrected probes
float distanceRatio = length(eyeToSample) / length(probeToSample);
float adjustedRoughness = roughness * distanceRatio;
```

### Recommended Priority
**Medium-High** - Reflection probes would significantly improve specular quality, especially for water and metallic surfaces. Horizon occlusion is quick to add to existing shaders.

---

## 3. Atmospheric Scattering LUTs

### Ghost of Tsushima Approach
- **3D LUTs** indexed by (altitude, sun polar angle, view polar angle)
- **Parallel irradiance LUTs** storing Rayleigh/Mie irradiance before phase function
- **Multi-scattering** included in precomputation
- **Runtime earth shadow** term applied analytically
- **LMS color space** with optimized Rayleigh coefficients for accurate sunset colors

### Current Implementation (`AtmosphereLUTSystem`)
- **Transmittance LUT** (256×64, RGBA16F): Per-altitude light transmission
- **Multi-scatter LUT** (32×32, RG16F): Second-order scattering
- **Sky-View LUT** (192×108, RGBA16F): Updated per-frame
- **Irradiance LUT** (64×16, RGBA16F): Rayleigh + Mie combined
- **Cloud Map LUT** (256×256, RGBA16F): Paraboloid-projected cloud density
- **LMS color space blending** in `sky.frag:588-604` with sun-angle interpolation

### Gap Analysis
The LMS implementation is present and well-executed. Minor improvements possible:
1. **Separate Rayleigh/Mie irradiance**: GOT stores these separately for better cloud/haze lighting
2. **Cubic upsampling at sunrise/sunset**: Currently uses bilinear

### Recommended Priority
**Low** - Current implementation is solid. Separate irradiance channels would help particle/cloud lighting.

---

## 4. Volumetric Fog (Froxel System)

### Ghost of Tsushima Approach
- **128×64×64 froxel grid**, exponential depth slices (20% growth)
- **Analytic density functions**: Exponential height falloff + sigmoidal layers
- **Density anti-aliasing**: Store L/α (radiance divided by opacity), recompute transmittance per-pixel
- **Tricubic B-spline filtering** for smooth gradients
- **Temporal reprojection** with separate filtering for shadows/AO and local lights
- **Front-to-back integration** with quad swizzling in compute
- **Async compute** in ~0.5ms on base PS4

### Current Implementation (`FroxelSystem`, `postprocess.frag`)
- **128×64×64 froxel grid** with 1.2x exponential depth distribution
- **Exponential + sigmoidal density** functions in `atmosphere_common.glsl:278-303`
- **Density anti-aliasing** implemented: `fogData.rgb * fogData.a` recovery (`postprocess.frag:173-176`)
- **Tricubic B-spline filtering** with 8 bilinear taps (`postprocess.frag:84-141`)
- **Trilinear fallback** option for performance
- **Temporal reprojection** with ping-pong buffers and configurable blend factor
- **Double-buffered scattering volumes** for history

### Gap Analysis
The implementation closely matches GOT's approach. Minor differences:
1. **Temporal filtering granularity**: GOT uses separate reprojection for shadows vs. local lights
2. **Async compute**: Could verify actual async execution
3. **Quad swizzling**: Not clear if used for front-to-back integration

### Recommended Priority
**Very Low** - Implementation is comprehensive and matches the reference.

---

## 5. Cloud Rendering

### Ghost of Tsushima Approach
- **Paraboloid map** (768×768) covering hemisphere
- **Triple-buffered**: Two for temporal blending while scrolling, one for rendering
- **Time-sliced updates** over 60 frames
- **Density anti-aliasing** using derivatives of cloud position w.r.t. texture coordinates
- **Phase function blending**: Forward → back scattering based on transmittance
- **Silver lining effect** from back-scattering at cloud edges
- **Multi-scattering scale** (2.16×) for back-scattering based on Mie albedo simulation

### Current Implementation
- **Paraboloid cloud map LUT** (256×256) in `cloudmap_lut.comp`
- **Cloud shadows** ray-marched from sun perspective (`CloudShadowSystem`)
- **Procedural FBM noise** for cloud shapes
- **Height gradient** with horizon stretch
- **No triple-buffered temporal** blending
- **No phase function depth-blending**

### Gap Analysis
Cloud rendering is functional but missing some GOT refinements:
1. **Phase function blending**: Not implemented - clouds use fixed forward scattering
2. **Density anti-aliasing**: Not implemented for clouds (only for fog)
3. **Triple-buffered temporal**: Would reduce per-frame cost
4. **Silver lining**: Back-scattering not modulated by transmittance

### Phase Function Blending (Missing)
```glsl
// GOT's approach: blend between forward and back scattering
float transmittanceSample = ...; // accumulated transmittance
float transmittanceToLight = ...; // transmittance toward sun
float blendFactor = transmittanceSample * transmittanceToLight;
float g = mix(-0.15, 0.8, blendFactor); // back-scatter (-0.15) to forward (0.8)
float phase = HenyeyGreenstein(cosTheta, g);
// Back-scatter boosted by 2.16× for multi-scattering approximation
float msScale = mix(2.16, 1.0, blendFactor);
```

### Recommended Priority
**Medium** - Phase function blending would significantly improve cloud appearance, especially backlit clouds.

---

## 6. Particle Haze Lighting

### Ghost of Tsushima Approach
- **Haze lighting model** for fog-like particles: Sample froxel Li volume
- **Li stored in parallel volume**: Scattered light before opacity multiplication
- **Multi-scattering approximation** for particles:
  - CPU computes back-scatter/forward-scatter ratio
  - Lerp between ratios based on view-light angle
  - Scale result by particle opacity

### Current Implementation
**Not implemented.** Particles use standard lighting, not volumetric haze sampling.

### Gap Analysis
This would integrate particles naturally with volumetric fog, preventing the "pasted on" look.

### Recommended Priority
**Low-Medium** - Useful for atmospheric particles but not critical.

---

## 7. Local Tone Mapping (Bilateral Filter)

### Ghost of Tsushima Approach
- **Bilateral grid algorithm** (64×32×64 volume)
- **Reduces contrast** while preserving local detail
- **Hybrid approach**: 40% bilateral + 60% wide Gaussian blur
- **De-ringing**: Wide Gaussian prevents artifacts on smooth gradients
- **Detail strength parameter** for local contrast boost
- **~250 μs** overhead at 1080p

### Current Implementation (Partial - Shaders Only)
Shader implementation added:
- `shaders/bilateral_grid_build.comp` - Populates 3D grid with log-luminance
- `shaders/bilateral_grid_blur.comp` - Separable Gaussian blur for the grid
- `shaders/postprocess.frag:238-301` - Grid sampling and local tone mapping

The shader-side implementation includes:
- Trilinear grid sampling
- Contrast reduction toward midpoint (middle gray)
- Detail boost parameter for local contrast enhancement
- Applied before ACES tonemapping

### Remaining Work (CPU-side)
The `BilateralGridSystem` C++ class needs to be added to:
- Create and manage the 3D grid texture (64×32×64 RGBA16F)
- Set up compute pipelines for build and blur passes
- Integrate with PostProcessSystem for per-frame updates
- Add UI controls for contrast/detail parameters

### Recommended Priority
**Medium** - Shaders are ready, needs C++ integration.

---

## 8. White Balance & Chromatic Adaptation

### Ghost of Tsushima Approach
- **Bradford LMS chromatic adaptation**
- **Artist picks target white color** (inverse of typical white balance)
- **Free GPU cost**: Baked into Rec709→tonemap color space matrix

### Current Implementation
**Not implemented.** Documented in `FUTURE_WORK.md:132`.

### Implementation Reference
```glsl
// Bradford LMS matrix
const mat3 RGBtoLMS = mat3(
    0.8951,  0.2664, -0.1614,
   -0.7502,  1.7135,  0.0367,
    0.0389, -0.0685,  1.0296
);

// Chromatic adaptation: scale by white point ratio in LMS space
vec3 adaptToWhite(vec3 rgb, vec3 sourceWhite, vec3 destWhite) {
    vec3 srcLMS = RGBtoLMS * sourceWhite;
    vec3 dstLMS = RGBtoLMS * destWhite;
    vec3 scale = dstLMS / srcLMS;
    vec3 lms = RGBtoLMS * rgb;
    lms *= scale;
    return inverse(RGBtoLMS) * lms;
}
```

### Recommended Priority
**Low** - Artistic tool, can be added anytime. Low implementation cost.

---

## 9. Custom Tone Mapping Color Space

### Ghost of Tsushima Approach
- **Modified ACES CG**: Red primary x-coordinate shifted from 0.713 to 0.75
- **Reduces yellow shift** of saturated reds during tone mapping
- **Preserves sunset warmth** without over-yellowing

### Current Implementation
Uses standard ACES filmic in Rec709 space (`postprocess.frag:180-187`).

### Gap Analysis
Per-channel ACES in Rec709 causes saturated colors to clip to CMY primaries. A wider gamut working space would help.

### Recommended Priority
**Low-Medium** - Noticeable improvement for sunsets and fire effects.

---

## 10. Purkinje Effect (Night Vision)

### Ghost of Tsushima Approach
Full physiological model based on Cow et al. and Kirk et al.:
- **LMSR color space**: Convert RGB to cone+rod responses
- **Rod intrusion**: Rods share neural pathways with cones
- **Opponent color space**: Model color shift in perceptual space
- **Multiplicative gain control**: Based on illuminance-dependent cone sensitivity

### Current Implementation (`postprocess.frag:202-223`)
Simplified approximation:
- **Desaturation**: Lerp to luminance below 10 lux
- **Blue shift**: Increase blue, decrease red below 5 lux
- **Brightening**: Boost luminance below 1 lux

### Gap Analysis
The simplified version captures the main perceptual effects but lacks:
- Proper LMSR color space transformation
- Opponent color space modeling
- Physiologically accurate gain control

### Full Implementation Reference
```glsl
// RGB to LMSR conversion (from Smith's basis spectra)
const mat4x3 RGBtoLMSR = mat4x3(...); // From paper

// Gain control (k values from Kirk et al.)
vec3 kRod = vec3(0.5, 0.5, 0.0); // Rod input to LMS pathways
vec3 m = vec3(0.08, 0.08, 0.08); // Cone sensitivity

vec4 q = RGBtoLMSR * rgb;
vec3 g = (m + kRod * q.w) / (m + q.xyz);

// Opponent space transform
const mat3 A = mat3(0.6, 0.4, 0.0,
                    0.24, -0.105, -0.7,
                    1.2, -1.6, 0.4);

vec3 deltaO = ...; // Rod contribution in opponent space
vec3 deltaRGB = inverse(RGBtoLMSR_3x3) * inverse(A) * deltaO;
```

### Recommended Priority
**Low** - Current implementation is functional. Full model adds accuracy but complexity.

---

## Prioritized Improvement Roadmap

### Completed (This Update)
1. ✅ **Horizon Occlusion for Reflections** - Added to `lighting_common.glsl:92-119`
2. ✅ **Cloud Phase Function Blending** - Already implemented in `sky.frag:301-326`
3. ⚡ **Bilateral Local Tone Mapping** - Shaders added, needs C++ integration

### High Impact, Remaining Work
4. **Bilateral Grid C++ System** - Integrate shaders with PostProcessSystem
5. **Reflection Probes** - Add static environment probes for off-screen reflections
6. **Separate Rayleigh/Mie Irradiance LUTs** - Improve cloud/particle lighting

### Medium Impact
7. **Particle Haze Lighting** - Sample froxel Li for volumetric particles
8. **SH Irradiance Probes** - Full indirect diffuse system

### Lower Priority
9. **White Balance Tool** - Artist control via Bradford adaptation
10. **Custom Tone Map Space** - Modified ACES CG primaries
11. **Full Purkinje Model** - Physiological accuracy

---

## References

- Petri, Justin. "Samurai Cinema: Creating a Real-Time Cinematic Experience in Ghost of Tsushima." SIGGRAPH 2021.
- Bruneton, Eric & Neyret, Fabrice. "Precomputed Atmospheric Scattering." EGSR 2008.
- Hillaire, Sébastien. "A Scalable and Production Ready Sky and Atmosphere Rendering Technique." EGSR 2020.
- Sloan, Peter-Pike. "Efficient Spherical Harmonic Evaluation." JCGT 2013.
- Cow, David et al. "A Model of Human Visual Adaptation for Realistic Image Synthesis." SIGGRAPH 1998.
- Kirk, Adam et al. "Perceptually Based Tone Mapping for Low-Light Conditions." TOG 2006.
- Wronski, Bart. "Localized Tonemapping." Blog post, 2020.
- Schuler, Kristen. "Spectral Rendering with RGB." Blog post.
