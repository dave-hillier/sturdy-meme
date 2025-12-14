#pragma once

#include <glm/glm.hpp>

// Shared environment settings used across systems for consistent wind and interaction tuning
struct EnvironmentSettings {
    // Wind parameters
    glm::vec2 windDirection = glm::vec2(1.0f, 0.0f);
    float windStrength = 1.0f;
    float windSpeed = 5.0f;
    float gustFrequency = 0.5f;
    float gustAmplitude = 0.3f;
    float noiseScale = 0.1f;

    // Interaction limits
    float leafDisruptionRadius = 2.0f;
    float leafDisruptionStrength = 5.0f;
    float leafGustLiftThreshold = 1.5f;
    float grassDisplacementDecay = 0.97f;
    float grassMaxDisplacement = 1.0f;

    // Snow accumulation parameters
    float snowAmount = 0.0f;                             // Global snow intensity (0-1)
    glm::vec3 snowColor = glm::vec3(0.95f, 0.97f, 1.0f); // Slightly blue-white snow
    float snowRoughness = 0.7f;                          // Snow surface roughness
    float snowTexScale = 0.1f;                           // World-space snow texture scale
    float snowNoiseScale = 0.02f;                        // Noise variation scale
    float snowAccumulationRate = 0.01f;                  // Rate of snow buildup per second
    float snowMeltRate = 0.005f;                         // Rate of snow melting per second
    float snowMaskSize = 500.0f;                         // World-space size of snow mask coverage
};

