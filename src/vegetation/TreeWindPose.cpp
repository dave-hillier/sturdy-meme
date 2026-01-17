#include "TreeWindPose.h"
#include <cmath>
#include <glm/gtc/constants.hpp>

// Simple 3D noise function (approximates shader simplex3 behavior)
// Uses a hash-based approach for deterministic results
float TreeWindPose::noise3(const glm::vec3& p) {
    // Simple hash-based noise
    glm::vec3 i = glm::floor(p);
    glm::vec3 f = glm::fract(p);

    // Hash function
    auto hash = [](glm::vec3 v) -> float {
        float n = glm::dot(v, glm::vec3(1.0f, 57.0f, 113.0f));
        return glm::fract(std::sin(n) * 43758.5453f);
    };

    // Smooth interpolation
    glm::vec3 u = f * f * (3.0f - 2.0f * f);

    // Trilinear interpolation of 8 corner values
    float a = hash(i);
    float b = hash(i + glm::vec3(1, 0, 0));
    float c = hash(i + glm::vec3(0, 1, 0));
    float d = hash(i + glm::vec3(1, 1, 0));
    float e = hash(i + glm::vec3(0, 0, 1));
    float fa = hash(i + glm::vec3(1, 0, 1));
    float g = hash(i + glm::vec3(0, 1, 1));
    float h = hash(i + glm::vec3(1, 1, 1));

    float x1 = glm::mix(a, b, u.x);
    float x2 = glm::mix(c, d, u.x);
    float x3 = glm::mix(e, fa, u.x);
    float x4 = glm::mix(g, h, u.x);

    float y1 = glm::mix(x1, x2, u.y);
    float y2 = glm::mix(x3, x4, u.y);

    return glm::mix(y1, y2, u.z) * 2.0f - 1.0f;  // Return [-1, 1]
}

TreeWindParams TreeWindParams::fromWindSystem(
    const glm::vec2& dir, float strength, float gustFreq, float time) {
    TreeWindParams params;
    params.direction = glm::normalize(dir);
    params.strength = strength;
    params.gustFrequency = gustFreq;
    params.time = time;
    return params;
}

TreeOscillation TreeWindPose::calculateOscillation(
    const glm::vec3& treeWorldPosition,
    const TreeWindParams& wind) {

    TreeOscillation result;

    // Per-tree phase offset from noise (so trees don't sway in sync)
    // Matches: result.treePhase = simplex3(treeBaseWorld * 0.1) * 6.28318;
    result.treePhase = noise3(treeWorldPosition * 0.1f) * glm::two_pi<float>();

    // Wind direction in 3D (XZ plane)
    result.windDir3D = glm::vec3(wind.direction.x, 0.0f, wind.direction.y);

    // Perpendicular to wind direction (for secondary sway)
    result.windPerp3D = glm::vec3(-wind.direction.y, 0.0f, wind.direction.x);

    // Multi-frequency oscillation for natural motion
    // Matches shader exactly
    float mainBendTime = wind.time * wind.gustFrequency;
    result.mainBend =
        0.5f * std::sin(mainBendTime + result.treePhase) +
        0.3f * std::sin(mainBendTime * 2.1f + result.treePhase * 1.3f) +
        0.2f * std::sin(mainBendTime * 3.7f + result.treePhase * 0.7f);

    // Secondary perpendicular sway (figure-8 motion)
    result.perpBend =
        0.3f * std::sin(mainBendTime * 1.3f + result.treePhase + 1.57f) +
        0.2f * std::sin(mainBendTime * 2.7f + result.treePhase * 0.9f);

    return result;
}

float TreeWindPose::calculateBranchFlexibility(int branchLevel) {
    // Matches: return 0.02 + branchLevel * 0.025;  // 0.02 to 0.095
    return 0.02f + static_cast<float>(branchLevel) * 0.025f;
}

float TreeWindPose::calculateDirectionScale(
    const glm::vec3& branchDirection,
    const glm::vec3& windDir3D) {

    float windAlignment = glm::dot(branchDirection, windDir3D);
    // windAlignment: 1 = facing wind, -1 = back to wind
    // Scale: back-facing (1.5x), perpendicular (1.0x), wind-facing (0.5x)
    return glm::mix(1.5f, 0.5f, (windAlignment + 1.0f) * 0.5f);
}

NodeMask TreeWindPose::getDefaultFlexibilityMask(const TreeSkeleton& skeleton) {
    return skeleton.flexibilityMask();
}

HierarchyPose TreeWindPose::calculateWindPose(
    const TreeSkeleton& skeleton,
    const TreeOscillation& oscillation,
    const TreeWindParams& wind) {

    // Use default flexibility mask
    NodeMask flexMask = getDefaultFlexibilityMask(skeleton);
    return calculateWindPoseMasked(skeleton, oscillation, wind, flexMask);
}

HierarchyPose TreeWindPose::calculateWindPoseMasked(
    const TreeSkeleton& skeleton,
    const TreeOscillation& oscillation,
    const TreeWindParams& wind,
    const NodeMask& flexibilityMask) {

    HierarchyPose pose;
    pose.resize(skeleton.size());

    for (size_t i = 0; i < skeleton.size(); ++i) {
        const TreeBranch& branch = skeleton[i];

        // Get flexibility for this branch (from mask or default)
        float flexibility = flexibilityMask.getWeight(i);

        // Base flexibility from branch level
        float baseFlex = calculateBranchFlexibility(branch.level);

        // Combined flexibility (mask modulates base flexibility)
        float totalFlex = baseFlex * flexibility;

        // Extract branch direction from rest pose
        // The branch grows along local Y axis, so transform that
        glm::vec3 localUp(0.0f, 1.0f, 0.0f);
        glm::mat3 rotMat = glm::mat3(branch.restPoseLocal);
        glm::vec3 branchDir = glm::normalize(rotMat * localUp);

        // Calculate direction-based wind response
        float dirScale = calculateDirectionScale(branchDir, oscillation.windDir3D);

        // Calculate bend amount
        // This is a simplified version - in the shader, it uses height above pivot
        // Here we use branch level as a proxy for height
        float heightFactor = static_cast<float>(branch.level + 1);
        float bendAmount = heightFactor * totalFlex * wind.strength * dirScale;

        // Calculate rotation from wind
        // Main bend is around the axis perpendicular to both wind and up
        glm::vec3 bendAxis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), oscillation.windDir3D);
        if (glm::length(bendAxis) < 0.001f) {
            bendAxis = glm::vec3(1.0f, 0.0f, 0.0f);  // Fallback if wind is vertical
        }
        bendAxis = glm::normalize(bendAxis);

        // Create rotation quaternions for main and perpendicular bend
        float mainAngle = oscillation.mainBend * bendAmount * 0.1f;  // Scale down for reasonable angles
        float perpAngle = oscillation.perpBend * bendAmount * 0.05f;

        glm::quat mainRot = glm::angleAxis(mainAngle, bendAxis);
        glm::quat perpRot = glm::angleAxis(perpAngle, oscillation.windDir3D);

        // Combine rotations
        glm::quat windRotation = perpRot * mainRot;

        // Create pose delta
        // Translation delta is zero (branches don't translate, just rotate)
        // Scale delta is identity (1,1,1)
        pose[i].translation = glm::vec3(0.0f);
        pose[i].rotation = windRotation;
        pose[i].scale = glm::vec3(1.0f);
    }

    return pose;
}
