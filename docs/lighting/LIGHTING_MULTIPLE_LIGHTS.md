# Multiple Dynamic Lights Plan

Purpose: extend the engine from the current sun + moon + single point glow into a data-driven multi-light system while keeping the sun’s cascaded shadows intact. This is an implementation guide without code.

## Scope and Preconditions
- Build on Phase 1 (PBR) and Phase 2 (CSM) foundations described in the existing lighting documents.
- Retain current directional-light path (sun/moon) and cascaded shadow resources.
- Keep performance in mind: start with a small fixed budget and add culling before scaling up.

## Current State (baseline)
- Directional lights: sun (with cascaded shadows) and moon (fill, no shadows).
- One point light for the glowing orb, unshadowed, radius-based attenuation.
- Forward renderer with per-frame UBO carrying a single point light payload.

## Target Capabilities
- Support an arbitrary list of additional lights (points first; spots next) with per-light color, intensity, radius, and optional direction/cone for spots.
- Data-driven creation so gameplay systems can add/remove lights per frame.
- Graceful fallback when the light budget is exceeded (cull or clamp count).
- Optional future step: shadows for selected non-directional lights.

## Data Model (CPU side)
- Introduce a Light struct that covers common fields: type (directional/point/spot), position, direction, color, intensity, range, and cone angles for spots.
- Store lights in a per-frame list owned by the renderer or a light manager, populated by gameplay or scene setup.
- Keep sun/moon separate so their directional data continues to drive CSM.
- Define a maximum light count for the frame and drop or fade excess lights deterministically to avoid flicker.

## GPU Interface
- Add a light array buffer (prefer an SSBO for flexibility) containing the compact light records plus a count.
- Update descriptor set layout to bind the light buffer alongside existing UBOs and textures; push or include the active light count.
- In the main material shaders, loop over the active lights and accumulate contributions using the existing PBR routines, leaving sun/moon paths untouched for shadows and atmospheric integration.
- Apply the current attenuation model for points; for spots, use direction plus cone falloff blended smoothly to radius-based attenuation.

## Shading Behavior
- Sun: unchanged, cascaded shadows.
- Moon: unchanged, fill light without shadows.
- Extra lights: unshadowed initially; contribute direct lighting only. Order-independent accumulation is fine for the small budgets expected here.
- Emissive materials stay additive and can visually “explain” local lights (e.g., the glowing orb).

## Incremental Implementation Steps
1) Refactor the existing single point-light uniforms into a generic light record and light count, still populating only one light. Verify rendering matches the current look.
2) Switch to an SSBO-backed light array with a modest max (e.g., 16) and update the shader loop to consume it. Validate with two or three lights placed near the player.
3) Add per-frame light culling/prioritization: drop lights outside a radius around the camera or exceeding the budget; keep results stable across frames.
4) Add spot light parameters and shading branch; test with a single spotlight to confirm cone shaping and attenuation feel good.
5) Optional: add shadowing for selected non-directional lights (cube maps for points, single shadow map for spots). Start with a cap of one shadowed local light to protect performance.

## Testing and Validation
- Build: cmake --preset debug && cmake --build build/debug
- Run: ./run-debug.sh
- Visual checks: multiple lights in view, no sudden popping when lights enter/leave range, sun shadows unaffected, night scenes retain moon fill.
- Performance sanity: measure frame time before/after enabling a batch of lights; tune the max light count or culling radius as needed.
