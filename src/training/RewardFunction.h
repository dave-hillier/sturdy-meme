#pragma once

#include "MotionClip.h"
#include "../physics/ArticulatedBody.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

// 5-term reward function from the UniCon paper.
// r = w_p * r_p + w_r * r_r + w_v * r_v + w_lv * r_lv + w_av * r_av
//
// Each term uses exp(-k * ||target - actual||^2) kernel.
// Constrained: episode terminates if any term < alpha.
struct RewardConfig {
    // Reward weights
    float wPosition = 0.4f;
    float wRotation = 0.3f;
    float wVelocity = 0.1f;
    float wLinearVel = 0.1f;
    float wAngularVel = 0.1f;

    // Kernel sharpness
    float kPosition = 5.0f;
    float kRotation = 2.0f;
    float kVelocity = 0.5f;
    float kLinearVel = 1.0f;
    float kAngularVel = 0.5f;

    // Early termination threshold
    float alpha = 0.1f;

    // Height threshold for falling
    float minHeight = 0.3f;
};

struct RewardResult {
    float total = 0.0f;
    float position = 0.0f;
    float rotation = 0.0f;
    float velocity = 0.0f;
    float linearVel = 0.0f;
    float angularVel = 0.0f;
    bool earlyTermination = false;
};

// Compute reward given current body state and a reference motion frame.
RewardResult computeReward(
    const std::vector<ArticulatedBody::PartState>& currentStates,
    const MotionFrame& referenceFrame,
    const RewardConfig& config = {});
