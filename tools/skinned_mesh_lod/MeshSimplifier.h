#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

// Skinned vertex data for LOD generation (matches src/animation/SkinnedMesh.h)
struct SkinnedVertexData {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 tangent;      // xyz = direction, w = handedness
    glm::uvec4 boneIndices; // 4 bone influences
    glm::vec4 boneWeights;
    glm::vec4 color;        // material base color
};

// Joint data for skeleton
struct JointData {
    std::string name;
    int32_t parentIndex;    // -1 for root
    glm::mat4 inverseBindMatrix;
    glm::mat4 localTransform;
};

// LOD mesh data
struct LODMeshData {
    std::vector<SkinnedVertexData> vertices;
    std::vector<uint32_t> indices;
    uint32_t lodLevel;
    float targetRatio;      // target triangle ratio vs original
    float actualRatio;      // actual achieved ratio
};

// Complete skinned mesh with multiple LOD levels
struct SkinnedMeshLODs {
    std::vector<LODMeshData> lods;  // LOD 0 = highest detail
    std::vector<JointData> skeleton;
    std::string name;
};

// Configuration for LOD generation
struct LODConfig {
    std::vector<float> lodRatios = {1.0f, 0.5f, 0.25f, 0.125f}; // Triangle ratios for each LOD
    float targetError = 0.01f;          // Target error threshold for simplification
    bool lockBoundary = true;           // Preserve mesh boundaries
    bool preserveAttributes = true;     // Preserve bone weights during simplification
    float errorAggressiveness = 0.5f;   // How aggressively to simplify (0-1)
};

// Progress callback: (progress 0-1, status message)
using ProgressCallback = std::function<void(float, const std::string&)>;

// Mesh simplifier for skinned meshes
class MeshSimplifier {
public:
    MeshSimplifier() = default;

    // Load a skinned mesh from GLTF/GLB file
    bool loadGLTF(const std::string& path);

    // Load a skinned mesh from FBX file
    bool loadFBX(const std::string& path);

    // Generate LOD levels based on config
    bool generateLODs(const LODConfig& config, ProgressCallback progress = nullptr);

    // Get the generated LOD data
    const SkinnedMeshLODs& getLODs() const { return lodData_; }

    // Save LODs to output directory as GLTF files
    bool saveGLTF(const std::string& outputDir) const;

    // Save LODs to a single JSON manifest + binary data
    bool saveBinary(const std::string& outputPath) const;

    // Get statistics
    struct Statistics {
        size_t originalVertices = 0;
        size_t originalTriangles = 0;
        std::vector<size_t> lodVertices;
        std::vector<size_t> lodTriangles;
        size_t skeletonJoints = 0;
    };
    const Statistics& getStatistics() const { return stats_; }

private:
    // Simplify mesh to target triangle count
    LODMeshData simplifyMesh(const LODMeshData& source, float targetRatio,
                             const LODConfig& config);

    // Remap bone weights after vertex merging
    void remapBoneWeights(LODMeshData& mesh);

    // Normalize bone weights to sum to 1.0
    void normalizeBoneWeights(LODMeshData& mesh);

    SkinnedMeshLODs lodData_;
    Statistics stats_;
};
