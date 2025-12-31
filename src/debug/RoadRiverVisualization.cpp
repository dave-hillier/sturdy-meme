#include "RoadRiverVisualization.h"
#include "DebugLineSystem.h"
#include "../terrain/TerrainHeightMap.h"
#include "../terrain/RoadNetworkLoader.h"
#include "../water/WaterPlacementData.h"
#include <glm/gtc/constants.hpp>
#include <SDL3/SDL_log.h>
#include <cmath>

void RoadRiverVisualization::addToDebugLines(DebugLineSystem& debugLines) {
    // Rebuild cache if dirty
    if (dirty_) {
        rebuildCache();
        dirty_ = false;
    }

    // Ensure persistent lines are set if we have cached data
    // This handles re-enabling after being disabled (persistent lines were cleared)
    if (!cachedLineVertices_.empty() && debugLines.getPersistentLineCount() == 0) {
        debugLines.setPersistentLines(
            reinterpret_cast<const DebugLineVertex*>(cachedLineVertices_.data()),
            cachedLineVertices_.size()
        );
    }
    // No per-frame work needed - persistent lines are rendered automatically
}

void RoadRiverVisualization::rebuildCache() {
    cachedLineVertices_.clear();

    if (config_.showRivers && waterData_ != nullptr) {
        buildRiverCones();
    }

    if (config_.showRoads && roadNetwork_ != nullptr) {
        buildRoadCones();
    }

    // 16 lines * 2 verts = 32 verts per cone
    size_t estimatedCones = cachedLineVertices_.size() / 32;
    SDL_Log("RoadRiverVisualization: cached %zu line vertices (~%zu cones)",
            cachedLineVertices_.size(), estimatedCones);
}

void RoadRiverVisualization::addConeToCache(const glm::vec3& base, const glm::vec3& tip,
                                             float radius, const glm::vec4& color) {
    const int segments = 8;

    // Calculate axis direction
    glm::vec3 axis = tip - base;
    float length = glm::length(axis);
    if (length < 0.001f) return;
    axis /= length;

    // Find perpendicular vectors for the base circle
    glm::vec3 up = (std::abs(axis.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 right = glm::normalize(glm::cross(axis, up));
    glm::vec3 forward = glm::cross(right, axis);

    // Generate circle points at base
    glm::vec3 circlePoints[8];
    for (int i = 0; i < segments; i++) {
        float angle = (float(i) / segments) * glm::two_pi<float>();
        glm::vec3 offset = (right * std::cos(angle) + forward * std::sin(angle)) * radius;
        circlePoints[i] = base + offset;
    }

    // Add lines from base circle to tip
    for (int i = 0; i < segments; i++) {
        cachedLineVertices_.push_back({circlePoints[i], color});
        cachedLineVertices_.push_back({tip, color});
    }

    // Add lines around base circle
    for (int i = 0; i < segments; i++) {
        cachedLineVertices_.push_back({circlePoints[i], color});
        cachedLineVertices_.push_back({circlePoints[(i + 1) % segments], color});
    }
}

void RoadRiverVisualization::buildRiverCones() {
    const float spacing = config_.riverConeSpacing;
    const float coneLength = config_.coneLength;
    const float heightOffset = config_.heightAboveGround;

    for (const auto& river : waterData_->rivers) {
        if (river.controlPoints.size() < 2) continue;

        // Walk along the river spline and place cones at regular intervals
        float accumulated = 0.0f;
        float nextConeAt = 0.0f;

        for (size_t i = 1; i < river.controlPoints.size(); i++) {
            const glm::vec3& prev = river.controlPoints[i - 1];
            const glm::vec3& curr = river.controlPoints[i];

            glm::vec3 segmentDir = curr - prev;
            float segmentLen = glm::length(segmentDir);
            if (segmentLen < 0.001f) continue;

            glm::vec3 direction = segmentDir / segmentLen;

            // Place cones along this segment
            while (nextConeAt <= accumulated + segmentLen) {
                float t = (nextConeAt - accumulated) / segmentLen;
                glm::vec3 pos = glm::mix(prev, curr, t);

                // River control points already have Y as height, but add offset
                glm::vec3 basePos = pos + glm::vec3(0.0f, heightOffset, 0.0f);
                glm::vec3 tipPos = basePos + direction * coneLength;

                addConeToCache(basePos, tipPos, config_.coneRadius, config_.riverColor);

                nextConeAt += spacing;
            }

            accumulated += segmentLen;
        }
    }
}

void RoadRiverVisualization::buildRoadCones() {
    const float spacing = config_.roadConeSpacing;
    const float coneLength = config_.coneLength;
    const float heightOffset = config_.heightAboveGround;

    // Road coordinates are in 0-terrainSize space, need to convert to centered world space
    const float halfTerrain = roadNetwork_->terrainSize * 0.5f;

    for (const auto& road : roadNetwork_->roads) {
        if (road.controlPoints.size() < 2) continue;

        // Walk along the road spline and place bidirectional cones
        float accumulated = 0.0f;
        float nextConeAt = 0.0f;

        for (size_t i = 1; i < road.controlPoints.size(); i++) {
            const glm::vec2& prevXZ = road.controlPoints[i - 1].position;
            const glm::vec2& currXZ = road.controlPoints[i].position;

            glm::vec2 segmentDir2D = currXZ - prevXZ;
            float segmentLen = glm::length(segmentDir2D);
            if (segmentLen < 0.001f) continue;

            glm::vec2 dir2D = segmentDir2D / segmentLen;

            // Place cones along this segment
            while (nextConeAt <= accumulated + segmentLen) {
                float t = (nextConeAt - accumulated) / segmentLen;
                glm::vec2 posXZ = glm::mix(prevXZ, currXZ, t);

                // Convert from 0-terrainSize to centered world coordinates
                float worldX = posXZ.x - halfTerrain;
                float worldZ = posXZ.y - halfTerrain;

                // Get terrain height at this position
                float terrainY = getTerrainHeight(worldX, worldZ);
                glm::vec3 basePos(worldX, terrainY + heightOffset, worldZ);

                // Forward direction cone
                glm::vec3 forwardDir(dir2D.x, 0.0f, dir2D.y);
                glm::vec3 tipPosForward = basePos + forwardDir * coneLength;
                addConeToCache(basePos, tipPosForward, config_.coneRadius, config_.roadColor);

                // Backward direction cone
                glm::vec3 tipPosBackward = basePos - forwardDir * coneLength;
                addConeToCache(basePos, tipPosBackward, config_.coneRadius, config_.roadColor);

                nextConeAt += spacing;
            }

            accumulated += segmentLen;
        }
    }
}

float RoadRiverVisualization::getTerrainHeight(float x, float z) const {
    if (heightMap_ == nullptr) {
        return 0.0f;
    }

    float height = heightMap_->getHeightAt(x, z);

    // Handle holes in terrain
    if (height == TerrainHeightMap::NO_GROUND) {
        return 0.0f;
    }

    return height;
}
