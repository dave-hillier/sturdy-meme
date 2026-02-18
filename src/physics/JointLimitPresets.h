#pragma once

#include <string>
#include <unordered_map>
#include <cmath>

namespace physics {

// Anatomical joint limit preset for a single joint.
// Angles are in radians.
struct JointLimitPreset {
    float swingYHalfAngle = 0.5f;   // Lateral swing (radians)
    float swingZHalfAngle = 0.5f;   // Forward/back swing (radians)
    float twistMin = -0.5f;          // Axial rotation min
    float twistMax = 0.5f;           // Axial rotation max
};

// Default humanoid joint limits keyed by common bone name substrings.
// These are matched case-insensitively against skeleton bone names.
inline const std::unordered_map<std::string, JointLimitPreset>& getDefaultJointLimits() {
    static const float DEG = 3.14159265f / 180.0f;
    static const std::unordered_map<std::string, JointLimitPreset> presets = {
        // Spine chain
        {"Hips",   {20.0f * DEG, 30.0f * DEG, -20.0f * DEG, 20.0f * DEG}},
        {"Spine",  {20.0f * DEG, 30.0f * DEG, -20.0f * DEG, 20.0f * DEG}},
        {"Spine1", {15.0f * DEG, 20.0f * DEG, -15.0f * DEG, 15.0f * DEG}},
        {"Spine2", {15.0f * DEG, 20.0f * DEG, -15.0f * DEG, 15.0f * DEG}},
        {"Chest",  {15.0f * DEG, 20.0f * DEG, -15.0f * DEG, 15.0f * DEG}},

        // Neck / Head
        {"Neck",   {30.0f * DEG, 40.0f * DEG, -30.0f * DEG, 30.0f * DEG}},
        {"Head",   {20.0f * DEG, 30.0f * DEG, -15.0f * DEG, 15.0f * DEG}},

        // Arms
        {"Shoulder",  {45.0f * DEG, 45.0f * DEG, -30.0f * DEG, 30.0f * DEG}},
        {"UpperArm",  {90.0f * DEG, 80.0f * DEG, -90.0f * DEG, 90.0f * DEG}},
        {"Arm",       {90.0f * DEG, 80.0f * DEG, -90.0f * DEG, 90.0f * DEG}},
        {"ForeArm",   {5.0f * DEG, 130.0f * DEG, -5.0f * DEG, 5.0f * DEG}},
        {"LowerArm",  {5.0f * DEG, 130.0f * DEG, -5.0f * DEG, 5.0f * DEG}},
        {"Hand",      {30.0f * DEG, 60.0f * DEG, -40.0f * DEG, 40.0f * DEG}},

        // Legs
        {"UpLeg",     {80.0f * DEG, 100.0f * DEG, -30.0f * DEG, 30.0f * DEG}},
        {"Thigh",     {80.0f * DEG, 100.0f * DEG, -30.0f * DEG, 30.0f * DEG}},
        {"Leg",       {5.0f * DEG, 130.0f * DEG, -5.0f * DEG, 5.0f * DEG}},
        {"Shin",      {5.0f * DEG, 130.0f * DEG, -5.0f * DEG, 5.0f * DEG}},
        {"Foot",      {20.0f * DEG, 40.0f * DEG, -15.0f * DEG, 15.0f * DEG}},
        {"Toe",       {5.0f * DEG, 30.0f * DEG, -5.0f * DEG, 5.0f * DEG}},
    };
    return presets;
}

// Find the best matching preset for a given bone name.
// Strips "Left"/"Right" prefixes, then matches against substrings.
inline JointLimitPreset findJointLimitPreset(const std::string& boneName) {
    // Try exact match first
    const auto& presets = getDefaultJointLimits();
    auto it = presets.find(boneName);
    if (it != presets.end()) return it->second;

    // Strip side prefixes for matching
    std::string stripped = boneName;
    if (stripped.find("Left") == 0) stripped = stripped.substr(4);
    else if (stripped.find("Right") == 0) stripped = stripped.substr(5);
    else if (stripped.find("left") == 0) stripped = stripped.substr(4);
    else if (stripped.find("right") == 0) stripped = stripped.substr(5);

    it = presets.find(stripped);
    if (it != presets.end()) return it->second;

    // Substring match: find the longest preset name that appears in the bone name
    const JointLimitPreset* best = nullptr;
    size_t bestLen = 0;
    for (const auto& [key, preset] : presets) {
        if (stripped.find(key) != std::string::npos && key.size() > bestLen) {
            best = &preset;
            bestLen = key.size();
        }
    }

    if (best) return *best;

    // Default: moderate limits
    return {30.0f * 3.14159265f / 180.0f,
            30.0f * 3.14159265f / 180.0f,
            -20.0f * 3.14159265f / 180.0f,
            20.0f * 3.14159265f / 180.0f};
}

} // namespace physics
