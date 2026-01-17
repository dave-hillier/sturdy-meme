#pragma once

#include "Mesh.h"
#include <string>
#include <optional>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Material properties extracted from FBX/glTF files
struct MaterialInfo {
    std::string name;

    // Colors
    glm::vec3 diffuseColor = glm::vec3(1.0f);
    glm::vec3 specularColor = glm::vec3(0.0f);
    glm::vec3 emissiveColor = glm::vec3(0.0f);

    // PBR properties
    float roughness = 0.5f;      // Derived from shininess
    float metallic = 0.0f;
    float opacity = 1.0f;
    float emissiveFactor = 0.0f;

    // Texture paths (relative to FBX file or absolute)
    std::string diffuseTexturePath;
    std::string normalTexturePath;
    std::string specularTexturePath;
    std::string emissiveTexturePath;

    // Index range in the mesh (which vertices/indices use this material)
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
};

// Forward declarations for skeleton and animation (Phase 2+)
// alignas(16) required for SIMD operations on glm::mat4 members
struct alignas(16) Joint {
    std::string name;
    int32_t parentIndex;  // -1 for root
    glm::mat4 inverseBindMatrix;
    glm::mat4 localTransform;
    glm::quat preRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // FBX pre-rotation (identity by default)
};

struct Skeleton {
    std::vector<Joint> joints;

    void computeGlobalTransforms(std::vector<glm::mat4>& outGlobalTransforms) const;
    int32_t findJointIndex(const std::string& name) const;

    // Helper to get parent's global transform for a joint (returns identity for root joints)
    inline glm::mat4 getParentGlobalTransform(int32_t jointIndex,
                                              const std::vector<glm::mat4>& globalTransforms) const {
        if (jointIndex < 0 || static_cast<size_t>(jointIndex) >= joints.size()) {
            return glm::mat4(1.0f);
        }
        int32_t parentIdx = joints[jointIndex].parentIndex;
        if (parentIdx < 0 || static_cast<size_t>(parentIdx) >= globalTransforms.size()) {
            return glm::mat4(1.0f);
        }
        return globalTransforms[parentIdx];
    }
};

// Result of loading a glTF file (static mesh)
struct GLTFLoadResult {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    Skeleton skeleton;

    // Materials extracted from the file
    std::vector<MaterialInfo> materials;
};

// Forward declare SkinnedVertex to avoid circular include
struct SkinnedVertex;

// Forward declare animation types
struct AnimationClip;

// Result of loading a skinned glTF file (with bone weights)
struct GLTFSkinnedLoadResult {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    Skeleton skeleton;
    std::vector<AnimationClip> animations;

    // Materials extracted from the file
    std::vector<MaterialInfo> materials;
};

namespace GLTFLoader {
    // Load mesh data from a glTF/GLB file
    // Returns nullopt if loading fails
    std::optional<GLTFLoadResult> load(const std::string& path);

    // Load only the mesh (no skeleton or animations) - useful for static models
    std::optional<GLTFLoadResult> loadMeshOnly(const std::string& path);

    // Load skinned mesh with bone weights and animations
    std::optional<GLTFSkinnedLoadResult> loadSkinned(const std::string& path);
}
