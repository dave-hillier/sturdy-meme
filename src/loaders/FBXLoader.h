#pragma once

#include "GLTFLoader.h"
#include "FBXPostProcess.h"
#include <string>
#include <optional>

namespace FBXLoader {
    // Load skinned mesh with bone weights and animations from FBX file
    //
    // This loader is designed for Mixamo FBX files by default:
    // - Handles 'mixamorig:' bone name prefix
    // - Supports Y-up, right-handed coordinate system
    // - Converts Euler angle rotations to quaternions
    // - Applies post-import processing (scale, coordinate system conversion)
    //
    // The settings parameter controls post-import processing:
    // - Use FBXPresets::Mixamo() for Mixamo files (default)
    // - Use FBXPresets::BlenderMeters() for Blender exports in meters
    // - Use FBXPresets::Identity() to skip post-processing
    //
    // Returns nullopt if loading fails
    std::optional<GLTFSkinnedLoadResult> loadSkinned(
        const std::string& path,
        const FBXImportSettings& settings = FBXPresets::Mixamo());

    // Load static mesh without skeleton or animations
    std::optional<GLTFLoadResult> load(
        const std::string& path,
        const FBXImportSettings& settings = FBXPresets::Mixamo());

    // Load only animations from an FBX file (for additional animation files)
    // Uses the provided skeleton for bone name mapping
    // Post-processing is applied using the given settings
    std::vector<AnimationClip> loadAnimations(
        const std::string& path,
        const Skeleton& skeleton,
        const FBXImportSettings& settings = FBXPresets::Mixamo());
}
