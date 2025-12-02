#pragma once

#include "GLTFLoader.h"
#include <string>
#include <optional>

namespace FBXLoader {
    // Load skinned mesh with bone weights and animations from FBX file
    //
    // This loader is designed for Mixamo FBX files:
    // - Handles 'mixamorig:' bone name prefix
    // - Supports Y-up, right-handed coordinate system
    // - Converts Euler angle rotations to quaternions
    //
    // Returns nullopt if loading fails
    std::optional<GLTFSkinnedLoadResult> loadSkinned(const std::string& path);

    // Load static mesh without skeleton or animations
    std::optional<GLTFLoadResult> load(const std::string& path);

    // Load only animations from an FBX file (for additional animation files)
    // Uses the provided skeleton for bone name mapping
    std::vector<AnimationClip> loadAnimations(const std::string& path, const Skeleton& skeleton);
}
