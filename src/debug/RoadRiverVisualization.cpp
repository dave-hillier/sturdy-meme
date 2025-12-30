#include "RoadRiverVisualization.h"
#include "DebugLineSystem.h"
#include "../terrain/TerrainHeightMap.h"
#include "../terrain/RoadNetworkLoader.h"
#include "../water/WaterPlacementData.h"
#include <glm/gtc/constants.hpp>

void RoadRiverVisualization::addToDebugLines(DebugLineSystem& debugLines) {
    if (config_.showRivers && waterData_ != nullptr) {
        addRiverVisualization(debugLines);
    }

    if (config_.showRoads && roadNetwork_ != nullptr) {
        addRoadVisualization(debugLines);
    }
}

void RoadRiverVisualization::addRiverVisualization(DebugLineSystem& debugLines) {
    const float spacing = config_.riverConeSpacing;
    const float coneRadius = config_.coneRadius;
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

                debugLines.addCone(basePos, tipPos, coneRadius, config_.riverColor);

                nextConeAt += spacing;
            }

            accumulated += segmentLen;
        }
    }
}

void RoadRiverVisualization::addRoadVisualization(DebugLineSystem& debugLines) {
    const float spacing = config_.roadConeSpacing;
    const float coneRadius = config_.coneRadius;
    const float coneLength = config_.coneLength;
    const float heightOffset = config_.heightAboveGround;

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

                // Get terrain height at this position
                float terrainY = getTerrainHeight(posXZ.x, posXZ.y);
                glm::vec3 basePos(posXZ.x, terrainY + heightOffset, posXZ.y);

                // Forward direction cone
                glm::vec3 forwardDir(dir2D.x, 0.0f, dir2D.y);
                glm::vec3 tipPosForward = basePos + forwardDir * coneLength;
                debugLines.addCone(basePos, tipPosForward, coneRadius, config_.roadColor);

                // Backward direction cone (offset slightly to side to not overlap)
                glm::vec3 backwardDir = -forwardDir;
                glm::vec3 tipPosBackward = basePos + backwardDir * coneLength;
                debugLines.addCone(basePos, tipPosBackward, coneRadius, config_.roadColor);

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
