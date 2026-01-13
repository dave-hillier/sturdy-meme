#include "MaterialComponents.h"

namespace material {

// Liquid presets based on real-world optical properties
// Absorption coefficients: how quickly each wavelength is absorbed (higher = faster)
// Real water absorbs red fastest, then green, then blue
// Turbidity: amount of suspended particles causing scattering

LiquidComponent LiquidComponent::ocean() {
    LiquidComponent l;
    l.color = glm::vec4(0.01f, 0.03f, 0.08f, 0.95f);
    l.absorption = glm::vec4(0.45f, 0.09f, 0.02f, 0.05f);  // RGB + turbidity
    l.absorptionScale = 0.12f;
    l.scatteringScale = 0.8f;
    l.roughness = 0.04f;
    l.sssIntensity = 1.2f;
    l.flags = LiquidFlags::FullWater;
    return l;
}

LiquidComponent LiquidComponent::coastalOcean() {
    LiquidComponent l;
    l.color = glm::vec4(0.02f, 0.06f, 0.10f, 0.92f);
    l.absorption = glm::vec4(0.35f, 0.12f, 0.05f, 0.15f);
    l.absorptionScale = 0.18f;
    l.scatteringScale = 1.2f;
    l.roughness = 0.05f;
    l.sssIntensity = 1.4f;
    l.flags = LiquidFlags::FullWater;
    return l;
}

LiquidComponent LiquidComponent::river() {
    LiquidComponent l;
    l.color = glm::vec4(0.04f, 0.08f, 0.06f, 0.90f);
    l.absorption = glm::vec4(0.25f, 0.18f, 0.12f, 0.25f);
    l.absorptionScale = 0.25f;
    l.scatteringScale = 1.5f;
    l.roughness = 0.06f;
    l.sssIntensity = 1.0f;
    l.flowSpeed = 0.5f;
    l.flowStrength = 1.0f;
    l.flags = LiquidFlags::Stream | LiquidFlags::Waves;
    return l;
}

LiquidComponent LiquidComponent::muddyRiver() {
    LiquidComponent l;
    l.color = glm::vec4(0.12f, 0.10f, 0.06f, 0.85f);
    l.absorption = glm::vec4(0.15f, 0.20f, 0.25f, 0.6f);
    l.absorptionScale = 0.4f;
    l.scatteringScale = 2.5f;
    l.roughness = 0.08f;
    l.sssIntensity = 0.5f;
    l.flowSpeed = 0.3f;
    l.flowStrength = 0.8f;
    l.flags = LiquidFlags::Flow | LiquidFlags::Foam;
    return l;
}

LiquidComponent LiquidComponent::clearStream() {
    LiquidComponent l;
    l.color = glm::vec4(0.01f, 0.04f, 0.08f, 0.98f);
    l.absorption = glm::vec4(0.50f, 0.08f, 0.01f, 0.02f);
    l.absorptionScale = 0.08f;
    l.scatteringScale = 0.5f;
    l.roughness = 0.03f;
    l.sssIntensity = 2.0f;
    l.flowSpeed = 0.8f;
    l.flowStrength = 1.2f;
    l.flags = LiquidFlags::FullWater;
    return l;
}

LiquidComponent LiquidComponent::lake() {
    LiquidComponent l;
    l.color = glm::vec4(0.02f, 0.05f, 0.08f, 0.93f);
    l.absorption = glm::vec4(0.35f, 0.15f, 0.08f, 0.12f);
    l.absorptionScale = 0.20f;
    l.scatteringScale = 1.0f;
    l.roughness = 0.04f;
    l.sssIntensity = 1.5f;
    l.flowSpeed = 0.0f;  // Lakes don't flow
    l.flowStrength = 0.0f;
    l.flags = LiquidFlags::Reflection | LiquidFlags::Refraction | LiquidFlags::Caustics | LiquidFlags::Subsurface;
    return l;
}

LiquidComponent LiquidComponent::swamp() {
    LiquidComponent l;
    l.color = glm::vec4(0.08f, 0.10f, 0.04f, 0.80f);
    l.absorption = glm::vec4(0.10f, 0.15f, 0.20f, 0.8f);
    l.absorptionScale = 0.5f;
    l.scatteringScale = 3.0f;
    l.roughness = 0.10f;
    l.sssIntensity = 0.3f;
    l.flags = LiquidFlags::Reflection;  // Murky, minimal refraction visible
    return l;
}

LiquidComponent LiquidComponent::tropical() {
    LiquidComponent l;
    l.color = glm::vec4(0.0f, 0.08f, 0.12f, 0.97f);
    l.absorption = glm::vec4(0.55f, 0.06f, 0.03f, 0.03f);
    l.absorptionScale = 0.06f;
    l.scatteringScale = 0.4f;
    l.roughness = 0.02f;
    l.sssIntensity = 2.5f;
    l.flags = LiquidFlags::FullWater;
    return l;
}

LiquidComponent LiquidComponent::puddle() {
    LiquidComponent l;
    l.color = glm::vec4(0.02f, 0.03f, 0.04f, 0.7f);
    l.absorption = glm::vec4(0.3f, 0.2f, 0.15f, 0.1f);
    l.depth = 0.05f;  // Very shallow
    l.absorptionScale = 0.1f;
    l.scatteringScale = 0.5f;
    l.roughness = 0.02f;  // Calm, reflective
    l.sssIntensity = 0.0f;
    l.flowSpeed = 0.0f;
    l.flowStrength = 0.0f;
    l.foamIntensity = 0.0f;
    l.flags = LiquidFlags::Puddle;
    return l;
}

LiquidComponent LiquidComponent::wetSurface(float wetness) {
    LiquidComponent l;
    // Wet surfaces don't have visible liquid, just modified surface properties
    l.color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    l.absorption = glm::vec4(0.0f);
    l.depth = wetness * 0.01f;  // Very thin water film
    l.absorptionScale = 0.0f;
    l.scatteringScale = 0.0f;
    l.roughness = 0.1f * (1.0f - wetness * 0.7f);  // Wet = smoother
    l.sssIntensity = 0.0f;
    l.flowSpeed = 0.0f;
    l.flowStrength = 0.0f;
    l.foamIntensity = 0.0f;
    l.refractionStrength = 0.0f;
    l.flags = LiquidFlags::Wetness;
    return l;
}

} // namespace material
