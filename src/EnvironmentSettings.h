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
};

