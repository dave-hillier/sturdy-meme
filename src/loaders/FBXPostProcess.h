#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

// Forward declarations
struct GLTFSkinnedLoadResult;
struct GLTFLoadResult;
struct AnimationClip;
struct Skeleton;

// Coordinate system axes
enum class UpAxis {
    Y_UP,       // Standard for OpenGL/Vulkan (default)
    Z_UP,       // Common in Blender, some CAD software
    NEG_Y_UP,   // Inverted Y
    NEG_Z_UP    // Inverted Z (rare)
};

enum class ForwardAxis {
    NEG_Z,      // Standard OpenGL/Vulkan (default)
    Z,          // Maya, some exporters
    NEG_Y,      // Some Blender exports
    Y,          // Rare
    X,          // Rare
    NEG_X       // Rare
};

// Import settings for FBX post-processing
struct FBXImportSettings {
    // Scale factor to convert from source units to meters
    // Examples:
    //   1.0      = already in meters
    //   0.01     = centimeters to meters (Mixamo, 3ds Max default)
    //   0.0254   = inches to meters
    //   0.3048   = feet to meters
    float scaleFactor = 1.0f;

    // Source coordinate system
    UpAxis sourceUpAxis = UpAxis::Y_UP;
    ForwardAxis sourceForwardAxis = ForwardAxis::NEG_Z;

    // Additional rotation correction (applied after coordinate system conversion)
    // Euler angles in degrees (applied as XYZ intrinsic rotation)
    glm::vec3 rotationCorrection = glm::vec3(0.0f);

    // Whether to flip UV V coordinate (handled during FBX load, kept for reference)
    bool flipUVs = true;

    // Whether to recalculate tangents (handled during FBX load, kept for reference)
    bool recalculateTangents = true;

    // Name for debugging
    std::string presetName = "Custom";
};

// Common presets for different source applications
namespace FBXPresets {
    // Mixamo exports: Y-up, cm units
    inline FBXImportSettings Mixamo() {
        FBXImportSettings settings;
        settings.scaleFactor = 0.01f;  // cm to meters
        settings.sourceUpAxis = UpAxis::Y_UP;
        settings.sourceForwardAxis = ForwardAxis::NEG_Z;
        settings.rotationCorrection = glm::vec3(0.0f);
        settings.presetName = "Mixamo";
        return settings;
    }

    // Blender FBX export with default settings (Z-up, meters)
    inline FBXImportSettings BlenderMeters() {
        FBXImportSettings settings;
        settings.scaleFactor = 1.0f;  // Already in meters
        settings.sourceUpAxis = UpAxis::Z_UP;
        settings.sourceForwardAxis = ForwardAxis::NEG_Y;
        settings.rotationCorrection = glm::vec3(0.0f);
        settings.presetName = "Blender (Meters)";
        return settings;
    }

    // Blender FBX export with cm scale
    inline FBXImportSettings BlenderCentimeters() {
        FBXImportSettings settings;
        settings.scaleFactor = 0.01f;  // cm to meters
        settings.sourceUpAxis = UpAxis::Z_UP;
        settings.sourceForwardAxis = ForwardAxis::NEG_Y;
        settings.rotationCorrection = glm::vec3(0.0f);
        settings.presetName = "Blender (Centimeters)";
        return settings;
    }

    // 3ds Max default (Z-up, system units usually inches or generic)
    inline FBXImportSettings Max3DS() {
        FBXImportSettings settings;
        settings.scaleFactor = 0.0254f;  // inches to meters (adjust as needed)
        settings.sourceUpAxis = UpAxis::Z_UP;
        settings.sourceForwardAxis = ForwardAxis::NEG_Y;
        settings.rotationCorrection = glm::vec3(0.0f);
        settings.presetName = "3ds Max";
        return settings;
    }

    // Maya default (Y-up, cm)
    inline FBXImportSettings Maya() {
        FBXImportSettings settings;
        settings.scaleFactor = 0.01f;  // cm to meters
        settings.sourceUpAxis = UpAxis::Y_UP;
        settings.sourceForwardAxis = ForwardAxis::Z;  // Maya typically exports with +Z forward
        settings.rotationCorrection = glm::vec3(0.0f);
        settings.presetName = "Maya";
        return settings;
    }

    // No transformation (data already in target coordinate system and units)
    inline FBXImportSettings Identity() {
        FBXImportSettings settings;
        settings.scaleFactor = 1.0f;
        settings.sourceUpAxis = UpAxis::Y_UP;
        settings.sourceForwardAxis = ForwardAxis::NEG_Z;
        settings.rotationCorrection = glm::vec3(0.0f);
        settings.presetName = "Identity";
        return settings;
    }
}

// Post-process FBX data to convert to engine coordinate system
namespace FBXPostProcess {
    // Process a skinned mesh result (vertices, skeleton, animations)
    void process(GLTFSkinnedLoadResult& result, const FBXImportSettings& settings);

    // Process a static mesh result (vertices only)
    void process(GLTFLoadResult& result, const FBXImportSettings& settings);

    // Process animations only (when loading additional animation files)
    void processAnimations(std::vector<AnimationClip>& animations,
                           const Skeleton& skeleton,
                           const FBXImportSettings& settings);

    // Build transformation matrix from import settings
    // This includes scale, coordinate system conversion, and rotation correction
    glm::mat4 buildTransformMatrix(const FBXImportSettings& settings);

    // Build rotation-only matrix (for transforming normals and directions)
    glm::mat3 buildRotationMatrix(const FBXImportSettings& settings);
}
