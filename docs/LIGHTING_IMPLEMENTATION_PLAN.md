# Lighting, Atmospheric Effects & Shadows Implementation Plan

A step-by-step guide to implementing cinematic lighting inspired by Ghost of Tsushima's rendering techniques, adapted for this Vulkan game engine.

> **Note:** This document has been split into smaller, focused sections for easier navigation. Please see the individual phase documents below.

---

## Documentation Structure

| Document | Description |
|----------|-------------|
| [Overview](lighting/LIGHTING_OVERVIEW.md) | Introduction, phase dependencies, implementation order, and references |
| [Phase 1: PBR Lighting](lighting/LIGHTING_PHASE1_PBR.md) | Light data structures, PBR lighting model (GGX, Smith, Fresnel), normal mapping |
| [Phase 2: Shadows](lighting/LIGHTING_PHASE2_SHADOWS.md) | Basic shadow mapping, cascaded shadow maps, shadow quality improvements |
| [Phase 3: Indirect Lighting](lighting/LIGHTING_PHASE3_INDIRECT.md) | SH probe system, reflection probes, light leak prevention |
| [Phase 4: Atmosphere](lighting/LIGHTING_PHASE4_ATMOSPHERE.md) | Sky model, volumetric clouds, volumetric haze/fog, god rays |
| [Phase 5: Post-Processing](lighting/LIGHTING_PHASE5_POSTPROCESS.md) | HDR, exposure control, tone mapping, color grading, bloom |
| [Phase 6: Integration](lighting/LIGHTING_PHASE6_INTEGRATION.md) | Time of day, weather system, optimization, Vulkan appendix |

---

## Quick Start

Start with the [Overview](lighting/LIGHTING_OVERVIEW.md) for phase dependencies and recommended implementation order, then proceed through each phase sequentially.
