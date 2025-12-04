# Future Work

Outstanding features and improvements not yet implemented.

---

## Camera Improvements

### Smoothing
- Add interpolated camera state (smoothedYaw, smoothedPitch, smoothedDistance)
- Exponential smoothing formula: `smoothed += (target - smoothed) * (1 - exp(-speed * deltaTime))`
- Configurable speed parameters: positionSmoothSpeed (8.0), rotationSmoothSpeed (12.0), distanceSmoothSpeed (6.0)

### Occlusion Handling
- Raycast from player to camera to detect occluding objects
- Fade occluding objects to transparent (opacity 0.3) instead of camera clipping
- Requires: PhysicsSystem raycast query, per-object opacity uniform, alpha blending

### Orientation Lock
- Toggle to lock player facing direction while allowing movement in any direction (strafe)
- Input: Left trigger hold or right stick click on gamepad

### Dynamic FOV
- Widen FOV during sprinting for sense of speed (45° idle → 55° sprint)

---

## Animation - Combat & Advanced Locomotion

Available animations in `assets/characters/fbx/` not yet integrated:

### Combat Attacks
- `ss_attack1.fbx`, `ss_attack2.fbx`, `ss_attack3.fbx` - Sword swings with combo timing windows
- `ss_slash1.fbx`, `ss_slash2.fbx` - Horizontal slashes
- `ss_kick.fbx` - Kick attack

### Combat Defense
- `ss_block.fbx` - Raise shield
- `ss_block_idle.fbx` - Hold block stance

### Combat Reactions
- `ss_impact1.fbx`, `ss_impact2.fbx` - Hit reactions
- `ss_death1.fbx`, `ss_death2.fbx` - Death animations

### Advanced Locomotion
- `ss_strafe_left.fbx`, `ss_strafe_right.fbx` - Strafing when locked-on
- `ss_turn_left.fbx`, `ss_turn_right.fbx`, `ss_turn_180.fbx` - Turn in place

### Special States
- `ss_crouch.fbx`, `ss_crouch_idle.fbx` - Crouch toggle
- `ss_casting.fbx` - Magic cast
- `ss_power_up.fbx` - Charge/power up
- `ss_sheath.fbx` - Sheathe weapon

---

## Procedural Trees

GPU-driven tree generation with adjustable parameters. Not currently implemented.

Key techniques:
- Lindenmayer systems (L-systems) or Space Colonization algorithm
- Bark texture with normal mapping
- Branch sway animation using WindSystem

---

## Painterly Tree Rendering

Stylized trees inspired by The Witness and Starboard devlog.

### Spherical Normals for Leaf Clumps
- Calculate normals as if clump were a sphere instead of per-polygon
- Blend factor for spherical vs polygon normals (tunable per tree type)
- Eliminates lighting sparkle on foliage

### Edge Fade
- Fade out leaf polygons when nearly perpendicular to camera
- `smoothstep(0.0, 0.3, abs(dot(faceNormal, viewDir)))`
- Maintains soft silhouettes

### Shadow Proxy System
- Leaf cards do NOT cast shadows
- Invisible proxy spheres at clump centers cast smooth shadows
- Eliminates chaotic self-shadowing

### Interior Shading
- Distance-from-center darkening for depth
- Blue color shift in shadows (ambient sky)
- Subsurface scattering approximation for backlit leaves

### Wind Animation
- Layered: trunk sway + branch sway + leaf rustle
- Hash-based phase variation per tree
- Integrates with existing WindSystem

---

## Atmosphere - Missing Features

From Phase 4 implementation status:

### Paraboloid Cloud Maps
- Pre-render clouds to hemisphere texture
- Triple-buffer for temporal stability
- Amortize cloud rendering cost over frames

### Cloud Temporal Reprojection
- History blending for cloud stability
- Time-sliced updates to reduce per-frame cost

### Improved Cloud Noise
- Replace FBM value noise with proper Perlin-Worley 3D textures
- Curl noise for wispy detail distortion

### Irradiance LUTs
- Separate Rayleigh/Mie irradiance textures for cloud/haze lighting

---

## Post-Processing - Missing Features

From Phase 5 implementation status:

### Local Tone Mapping
- Bilateral grid 3D texture (64×32×64)
- Trilinear splatting population
- Separable Gaussian blur
- Preserves detail while reducing contrast in high dynamic range scenes

### Color Grading
- White balance with Bradford chromatic adaptation
- Lift-gamma-gain shadow/midtone/highlight controls
- 3D color LUT support for external grading tools

### Additional Tone Mappers
- GT (Gran Turismo) curve
- AgX (modern hue-preserving)

### Vignette
- Screen-edge darkening for cinematic feel

### Full Purkinje Effect
- Upgrade from simplified desaturation/blue shift
- LMSR color space conversion
- Physiologically accurate rod/cone blending

---

## Wet Surfaces

When it rains, surfaces should show wetness effects:

### Surface Material Changes
- Reduce roughness (wet surfaces more reflective)
- Darken albedo (water absorption)
- Dampen normal maps (water fills micro-details)

### Puddles
- Form in concave areas when wetness exceeds threshold
- Near-zero roughness for mirror-like reflections
- Animated ripples from rain impact

### Drying Simulation
- Gradual wetness decrease when rain stops
- Material-dependent drying rates (porous dries slower)
- Wind increases evaporation

---

## Platform Considerations

See `MOLTENVK_CONSTRAINTS.md` for macOS/iOS specific constraints:
- Use per-cascade framebuffers instead of geometry shader layering for shadows
- Consider fixed-point atomics for bilateral grid population
- Query subgroup operation support at runtime
