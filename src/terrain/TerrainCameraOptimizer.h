#pragma once

#include <glm/glm.hpp>
#include <cstdint>

class TerrainCameraOptimizer {
public:
    struct Config {
        float positionThreshold = 0.1f;
        float rotationThreshold = 0.001f;
        uint32_t maxSkipFrames = 30;
        uint32_t convergenceFrames = 4;
    };

    TerrainCameraOptimizer() = default;
    explicit TerrainCameraOptimizer(const Config& cfg) : config(cfg) {}

    void setEnabled(bool enabled) { this->enabled = enabled; forceNextUpdate(); }
    bool isEnabled() const { return enabled; }

    void forceNextUpdate() { forceNextCompute = true; }

    // Returns true if compute should be skipped this frame
    bool shouldSkipCompute();

    // Call this every frame to update camera state
    void update(const glm::vec3& cameraPos, const glm::mat4& view);

    // Debug info
    bool wasLastFrameSkipped() const { return lastFrameWasSkipped; }
    uint32_t getStaticFrameCount() const { return staticFrameCount; }
    uint32_t getFramesSinceLastCompute() const { return framesSinceLastCompute; }

    void recordComputeExecuted() {
        forceNextCompute = false;
        framesSinceLastCompute = 0;
        lastFrameWasSkipped = false;
    }

    void recordComputeSkipped() {
        framesSinceLastCompute++;
        lastFrameWasSkipped = true;
    }

private:
    bool cameraHasMoved(const glm::vec3& cameraPos, const glm::mat4& view);

    Config config;
    bool enabled = true;

    struct CameraState {
        glm::vec3 position{0.0f};
        glm::vec3 forward{0.0f, 0.0f, -1.0f};
        bool valid = false;
    };
    CameraState previousCamera;

    uint32_t staticFrameCount = 0;
    uint32_t framesSinceLastCompute = 0;
    bool forceNextCompute = true;
    bool lastFrameWasSkipped = false;
};
