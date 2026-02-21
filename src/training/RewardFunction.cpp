#include "RewardFunction.h"

#include <algorithm>
#include <cmath>

static float quatDistance(const glm::quat& a, const glm::quat& b) {
    float dot = std::abs(glm::dot(a, b));
    dot = std::min(dot, 1.0f);
    return 2.0f * std::acos(dot); // angle in radians
}

RewardResult computeReward(
    const std::vector<ArticulatedBody::PartState>& currentStates,
    const MotionFrame& referenceFrame,
    const RewardConfig& config)
{
    RewardResult result;
    size_t numJoints = std::min(currentStates.size(), referenceFrame.jointPositions.size());

    if (numJoints == 0) return result;

    // Check for falling
    if (currentStates[0].position.y < config.minHeight) {
        result.earlyTermination = true;
        return result;
    }

    // 1. Joint position reward
    float posSqDist = 0.0f;
    for (size_t i = 0; i < numJoints; ++i) {
        glm::vec3 diff = currentStates[i].position - referenceFrame.jointPositions[i];
        posSqDist += glm::dot(diff, diff);
    }
    posSqDist /= static_cast<float>(numJoints);
    result.position = std::exp(-config.kPosition * posSqDist);

    // 2. Joint rotation reward
    float rotSqDist = 0.0f;
    for (size_t i = 0; i < numJoints; ++i) {
        float angle = quatDistance(currentStates[i].rotation, referenceFrame.jointRotations[i]);
        rotSqDist += angle * angle;
    }
    rotSqDist /= static_cast<float>(numJoints);
    result.rotation = std::exp(-config.kRotation * rotSqDist);

    // 3. End-effector velocity reward (feet + hands)
    // Use angular velocity of extremities as a proxy
    float velSqDist = 0.0f;
    const size_t endEffectors[] = {9, 13, 16, 19}; // hands and feet
    for (size_t idx : endEffectors) {
        if (idx < numJoints) {
            glm::vec3 vel = currentStates[idx].linearVelocity;
            velSqDist += glm::dot(vel, vel);
        }
    }
    velSqDist /= 4.0f;
    result.velocity = std::exp(-config.kVelocity * velSqDist);

    // 4. Root linear velocity reward
    glm::vec3 rootVelDiff = currentStates[0].linearVelocity; // target is zero for standing
    result.linearVel = std::exp(-config.kLinearVel * glm::dot(rootVelDiff, rootVelDiff));

    // 5. Root angular velocity reward
    glm::vec3 rootAngVelDiff = currentStates[0].angularVelocity;
    result.angularVel = std::exp(-config.kAngularVel * glm::dot(rootAngVelDiff, rootAngVelDiff));

    // Combined reward
    result.total = config.wPosition * result.position
                 + config.wRotation * result.rotation
                 + config.wVelocity * result.velocity
                 + config.wLinearVel * result.linearVel
                 + config.wAngularVel * result.angularVel;

    // Early termination check
    if (result.position < config.alpha || result.rotation < config.alpha) {
        result.earlyTermination = true;
    }

    return result;
}
