#include "RecursiveBranchingStrategy.h"
#include <glm/gtc/matrix_transform.hpp>
#include <SDL3/SDL.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void RecursiveBranchingStrategy::generate(const TreeParameters& params,
                                          std::mt19937& rng,
                                          TreeStructure& outTree) {
    rngPtr = &rng;

    // Create trunk as root branch
    glm::vec3 trunkStart(0.0f, 0.0f, 0.0f);
    glm::quat trunkOrientation(1.0f, 0.0f, 0.0f, 0.0f);

    float trunkLength = params.usePerLevelParams ? params.branchParams[0].length : params.trunkHeight;
    float trunkRadius = params.usePerLevelParams ? params.branchParams[0].radius : params.trunkRadius;

    // Get level 0 params for trunk
    const auto& trunkLevelParams = params.branchParams[0];

    Branch::Properties trunkProps;
    trunkProps.length = trunkLength;
    trunkProps.startRadius = trunkRadius;
    trunkProps.endRadius = trunkRadius * (params.usePerLevelParams ? trunkLevelParams.taper : params.trunkTaper);
    trunkProps.level = 0;
    trunkProps.radialSegments = params.usePerLevelParams ? trunkLevelParams.segments : params.trunkSegments;
    trunkProps.lengthSegments = params.usePerLevelParams ? trunkLevelParams.sections : params.trunkRings;

    Branch trunk(trunkStart, trunkOrientation, trunkProps);

    // Generate child branches recursively
    if (params.branchLevels > 0) {
        generateBranch(params, trunk, trunkStart, trunkOrientation, trunkLength, trunkRadius, 0);
    }

    outTree.setRoot(std::move(trunk));

    SDL_Log("RecursiveBranchingStrategy: Generated tree with %zu branches",
            outTree.getTotalBranchCount());
}

void RecursiveBranchingStrategy::generateBranch(const TreeParameters& params,
                                                 Branch& parentBranch,
                                                 const glm::vec3& startPos,
                                                 const glm::quat& orientation,
                                                 float length,
                                                 float radius,
                                                 int level) {
    // Check termination conditions
    if (level >= params.branchLevels) return;
    if (radius < params.minBranchRadius) return;

    // Get level params
    int levelIdx = std::min(level, 3);
    const auto& levelParams = params.branchParams[levelIdx];

    // Calculate where to spawn children
    int nextLevelIdx = std::min(level + 1, 3);
    const auto& nextLevelParams = params.branchParams[nextLevelIdx];

    float childStartT = params.usePerLevelParams ? nextLevelParams.start :
                        (level == 0 ? params.branchStartHeight : 0.3f);

    int numChildren = params.usePerLevelParams ? levelParams.children : params.childrenPerBranch;

    // Calculate end position of parent branch
    glm::vec3 direction = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    // Apply growth direction influence
    if (params.growthInfluence > 0.0f) {
        glm::vec3 mixed = glm::mix(direction, params.growthDirection, params.growthInfluence);
        float mixedLen = glm::length(mixed);
        if (mixedLen > 0.0001f) {
            direction = mixed / mixedLen;
        }
    }

    glm::vec3 endPos = startPos + direction * length;

    // Calculate taper
    float taperRatio = params.usePerLevelParams ? levelParams.taper :
                       (level == 0 ? params.trunkTaper : params.branchTaper);
    float endRadius = radius * taperRatio;

    // Spawn child branches
    for (int i = 0; i < numChildren; ++i) {
        float t = childStartT + (1.0f - childStartT) * (static_cast<float>(i) / static_cast<float>(std::max(1, numChildren)));

        // Position along parent branch
        glm::vec3 childStart = glm::mix(startPos, endPos, t);

        // Calculate child parameters
        float radiusAtT = glm::mix(radius, endRadius, t);
        float childRadius = params.usePerLevelParams ?
                           nextLevelParams.radius :
                           radiusAtT * params.branchRadiusRatio;

        float childLength = params.usePerLevelParams ?
                           nextLevelParams.length :
                           length * params.branchLengthRatio;

        // Calculate child orientation
        float spreadAngle = (2.0f * static_cast<float>(M_PI) * static_cast<float>(i)) / static_cast<float>(std::max(1, numChildren));
        spreadAngle += randomFloat(-0.3f, 0.3f);

        float branchAngleRad = params.usePerLevelParams ?
                              glm::radians(nextLevelParams.angle) :
                              glm::radians(params.branchingAngle);
        branchAngleRad += randomFloat(-0.1f, 0.1f) * branchAngleRad;

        glm::quat spreadRot = glm::angleAxis(spreadAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat tiltRot = glm::angleAxis(branchAngleRad, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat childOrientation = orientation * spreadRot * tiltRot;

        // Apply twist
        float twistAmount = params.usePerLevelParams ? levelParams.twist : params.twistAngle;
        float twist = glm::radians(twistAmount * 30.0f) * t;
        glm::quat twistRot = glm::angleAxis(twist, glm::vec3(0.0f, 1.0f, 0.0f));
        childOrientation = childOrientation * twistRot;

        // Apply gnarliness
        float gnarlAmount = params.usePerLevelParams ? levelParams.gnarliness : params.gnarliness;
        if (gnarlAmount > 0.0f) {
            float maxAngle = glm::radians(gnarlAmount * 30.0f);
            float rx = randomFloat(-maxAngle, maxAngle);
            float ry = randomFloat(-maxAngle, maxAngle);
            float rz = randomFloat(-maxAngle, maxAngle);
            glm::quat variation = glm::quat(glm::vec3(rx, ry, rz));
            childOrientation = glm::normalize(childOrientation * variation);
        }

        // Create child branch properties
        Branch::Properties childProps;
        childProps.length = childLength;
        childProps.startRadius = childRadius;
        childProps.endRadius = childRadius * (params.usePerLevelParams ? nextLevelParams.taper : params.branchTaper);
        childProps.level = level + 1;
        childProps.radialSegments = params.usePerLevelParams ? nextLevelParams.segments : params.branchSegments;
        childProps.lengthSegments = params.usePerLevelParams ? nextLevelParams.sections : params.branchRings;

        // Add child to parent and recurse
        Branch& childBranch = parentBranch.addChild(childStart, childOrientation, childProps);

        // Recursively generate grandchildren
        generateBranch(params, childBranch, childStart, childOrientation, childLength, childRadius, level + 1);
    }
}

float RecursiveBranchingStrategy::randomFloat(float min, float max) {
    if (!rngPtr) return (min + max) * 0.5f;
    std::uniform_real_distribution<float> dist(min, max);
    return dist(*rngPtr);
}
