#pragma once

#include "Mesh.h"
#include <string>
#include <optional>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declarations for skeleton and animation (Phase 2+)
struct Joint {
    std::string name;
    int32_t parentIndex;  // -1 for root
    glm::mat4 inverseBindMatrix;
    glm::mat4 localTransform;
};

struct Skeleton {
    std::vector<Joint> joints;

    void computeGlobalTransforms(std::vector<glm::mat4>& outGlobalTransforms) const;
    int32_t findJointIndex(const std::string& name) const;
};

// Result of loading a glTF file
struct GLTFLoadResult {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    Skeleton skeleton;

    // Material info (for future use)
    std::string baseColorTexturePath;
    std::string normalTexturePath;
};

namespace GLTFLoader {
    // Load mesh data from a glTF/GLB file
    // Returns nullopt if loading fails
    std::optional<GLTFLoadResult> load(const std::string& path);

    // Load only the mesh (no skeleton or animations) - useful for static models
    std::optional<GLTFLoadResult> loadMeshOnly(const std::string& path);
}
