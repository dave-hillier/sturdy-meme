#include "RewardComputer.h"

#include <cmath>
#include <algorithm>
#include <SDL3/SDL_log.h>

namespace training {

// Wrap an angle to [-pi, pi]
static float wrapAngle(float angle) {
    constexpr float PI = 3.14159265358979323846f;
    constexpr float TWO_PI = 2.0f * PI;
    angle = std::fmod(angle + PI, TWO_PI);
    if (angle < 0.0f) angle += TWO_PI;
    return angle - PI;
}

// Extract heading angle (yaw around Y axis) from a quaternion
static float getHeadingFromQuat(const glm::quat& q) {
    // Forward direction in local space is -Z, rotated by the quaternion
    glm::vec3 forward = q * glm::vec3(0.0f, 0.0f, -1.0f);
    return std::atan2(forward.x, -forward.z);
}

float RewardComputer::computeTaskReward(
    const TaskGoal& goal,
    const glm::vec3& rootPosition,
    const glm::quat& rootRotation,
    const glm::vec3& rootVelocity,
    const std::vector<glm::vec3>& keyBodyPositions) const
{
    switch (goal.type) {
        case TaskType::Heading:
            return computeHeadingReward(goal.targetHeading, rootRotation, rootVelocity);
        case TaskType::Location:
            return computeLocationReward(goal.targetPosition, rootPosition, rootVelocity);
        case TaskType::Strike:
            return computeStrikeReward(goal.targetPosition, keyBodyPositions, goal.strikeBodyIndex);
        default:
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "RewardComputer: unknown task type %d", static_cast<int>(goal.type));
            return 0.0f;
    }
}

float RewardComputer::computeHeadingReward(
    float targetHeading,
    const glm::quat& rootRotation,
    const glm::vec3& /*rootVelocity*/) const
{
    float currentHeading = getHeadingFromQuat(rootRotation);
    float angleDiff = wrapAngle(targetHeading - currentHeading);
    return std::exp(-2.0f * std::abs(angleDiff));
}

float RewardComputer::computeLocationReward(
    const glm::vec3& targetPos,
    const glm::vec3& rootPos,
    const glm::vec3& rootVelocity) const
{
    glm::vec3 toTarget = targetPos - rootPos;
    float distance = glm::length(toTarget);

    float distanceReward = std::exp(-0.5f * distance);

    // Direction reward: how well the velocity aligns with direction to target
    float directionReward = 0.0f;
    float speed = glm::length(rootVelocity);
    if (distance > 0.01f && speed > 0.01f) {
        glm::vec3 dirToTarget = toTarget / distance;
        glm::vec3 velDir = rootVelocity / speed;
        directionReward = std::max(0.0f, glm::dot(velDir, dirToTarget));
    }

    return distanceReward * directionReward;
}

float RewardComputer::computeStrikeReward(
    const glm::vec3& targetPos,
    const std::vector<glm::vec3>& keyBodyPositions,
    int bodyIndex) const
{
    if (bodyIndex < 0 || static_cast<size_t>(bodyIndex) >= keyBodyPositions.size()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "RewardComputer::computeStrikeReward: invalid body index %d (have %zu bodies)",
                    bodyIndex, keyBodyPositions.size());
        return 0.0f;
    }

    float distance = glm::length(targetPos - keyBodyPositions[bodyIndex]);
    return std::exp(-10.0f * distance);
}

} // namespace training
