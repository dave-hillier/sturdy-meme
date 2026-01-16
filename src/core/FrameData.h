#pragma once

#include <glm/glm.hpp>
#include <cstdint>

/**
 * FrameData - Per-frame shared state passed to subsystems
 *
 * Consolidates the scattered per-frame parameters that are computed once
 * at the start of render() and passed to multiple subsystems. This reduces
 * parameter passing overhead and makes dependencies explicit.
 *
 * Usage:
 *   FrameData frame;
 *   frame.frameIndex = currentFrame;
 *   frame.deltaTime = deltaTime;
 *   // ... populate other fields
 *   subsystem.update(frame);
 *
 * Note: alignas(16) ensures proper alignment for SIMD operations on glm::mat4
 * members (view, projection, viewProj). Without this, O3 optimizations using
 * aligned SSE/AVX loads can crash on misaligned data.
 */
struct alignas(16) FrameData {
    // Frame identification
    uint32_t frameIndex = 0;

    // Timing
    float deltaTime = 0.0f;
    float time = 0.0f;          // Total elapsed time
    float timeOfDay = 0.0f;     // Normalized day/night cycle [0, 1]

    // Camera
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::mat4 viewProj = glm::mat4(1.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    // Lighting
    glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 sunColor = glm::vec3(1.0f);
    float sunIntensity = 1.0f;
    glm::vec3 moonDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float moonIntensity = 0.0f;

    // Player (for interaction systems like grass displacement)
    glm::vec3 playerPosition = glm::vec3(0.0f);
    glm::vec3 playerVelocity = glm::vec3(0.0f);
    float playerCapsuleRadius = 0.5f;

    // Terrain parameters (for systems that need terrain info)
    float terrainSize = 1024.0f;
    float heightScale = 0.0f;

    // Wind parameters (from WindSystem/EnvironmentSettings)
    glm::vec2 windDirection = glm::vec2(1.0f, 0.0f);
    float windStrength = 1.0f;
    float windSpeed = 5.0f;
    float gustFrequency = 0.5f;
    float gustAmplitude = 0.3f;

    // Weather state
    uint32_t weatherType = 0;       // 0 = clear, 1 = rain, 2 = snow
    float weatherIntensity = 0.0f;

    // Snow parameters
    float snowAmount = 0.0f;
    glm::vec3 snowColor = glm::vec3(0.95f, 0.97f, 1.0f);

    // Frustum planes (extracted from viewProj, normalized)
    // Order: left, right, bottom, top, near, far
    glm::vec4 frustumPlanes[6] = {};
};
