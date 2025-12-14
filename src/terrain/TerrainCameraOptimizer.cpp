#include "TerrainCameraOptimizer.h"

bool TerrainCameraOptimizer::shouldSkipCompute() {
    if (!enabled || forceNextCompute) {
        return false;
    }

    if (staticFrameCount > config.convergenceFrames) {
        if (framesSinceLastCompute < config.maxSkipFrames) {
            return true;
        }
    }

    return false;
}

void TerrainCameraOptimizer::update(const glm::vec3& cameraPos, const glm::mat4& view) {
    if (cameraHasMoved(cameraPos, view)) {
        staticFrameCount = 0;
    } else {
        staticFrameCount++;
    }
}

bool TerrainCameraOptimizer::cameraHasMoved(const glm::vec3& cameraPos, const glm::mat4& view) {
    // Extract forward direction from view matrix (negated row 2)
    glm::vec3 forward = -glm::vec3(view[0][2], view[1][2], view[2][2]);

    // First frame - always consider moved
    if (!previousCamera.valid) {
        previousCamera.position = cameraPos;
        previousCamera.forward = forward;
        previousCamera.valid = true;
        return true;
    }

    // Check position delta
    float positionDelta = glm::length(cameraPos - previousCamera.position);
    if (positionDelta > config.positionThreshold) {
        previousCamera.position = cameraPos;
        previousCamera.forward = forward;
        return true;
    }

    // Check rotation delta (using dot product of forward vectors)
    float forwardDot = glm::dot(forward, previousCamera.forward);
    if (forwardDot < (1.0f - config.rotationThreshold)) {
        previousCamera.position = cameraPos;
        previousCamera.forward = forward;
        return true;
    }

    // No significant change
    return false;
}
