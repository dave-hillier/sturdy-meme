#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace training {

enum class TaskType {
    Heading,    // Match a target heading direction
    Location,   // Move toward a target position
    Strike      // Hand reaches a target
};

struct TaskGoal {
    TaskType type = TaskType::Heading;
    glm::vec3 targetPosition{0.0f};
    float targetHeading = 0.0f;        // radians
    int strikeBodyIndex = -1;          // Which key body to use for strike
};

class RewardComputer {
public:
    // Compute task reward for current state.
    // Returns a scalar reward in [0, 1] depending on the task type.
    float computeTaskReward(const TaskGoal& goal,
                            const glm::vec3& rootPosition,
                            const glm::quat& rootRotation,
                            const glm::vec3& rootVelocity,
                            const std::vector<glm::vec3>& keyBodyPositions) const;

private:
    // Heading task: reward for matching a target facing direction.
    // exp(-2.0 * |angle_diff|) where angle_diff is wrapped to [-pi, pi].
    float computeHeadingReward(float targetHeading,
                               const glm::quat& rootRotation,
                               const glm::vec3& rootVelocity) const;

    // Location task: reward for moving toward a target position.
    // exp(-0.5 * distance) * direction_reward
    // where direction_reward = max(0, dot(normalized_velocity, direction_to_target)).
    float computeLocationReward(const glm::vec3& targetPos,
                                const glm::vec3& rootPos,
                                const glm::vec3& rootVelocity) const;

    // Strike task: reward for a key body reaching a target.
    // exp(-10.0 * distance_to_target).
    float computeStrikeReward(const glm::vec3& targetPos,
                              const std::vector<glm::vec3>& keyBodyPositions,
                              int bodyIndex) const;
};

} // namespace training
