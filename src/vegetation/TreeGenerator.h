#pragma once

#include "TreeOptions.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <queue>
#include <cstdint>
#include <optional>

// Forward declaration
struct TreeSkeleton;

// Seeded random number generator (matching ez-tree's RNG)
class TreeRNG {
public:
    explicit TreeRNG(uint32_t seed);
    float random(float max = 1.0f, float min = 0.0f);

private:
    uint32_t m_w;
    uint32_t m_z;
    static constexpr uint32_t MASK = 0xFFFFFFFF;
};

// Section data within a branch (for geometry generation)
struct SectionData {
    glm::vec3 origin;
    glm::quat orientation;
    float radius;
};

// Branch data for GPU geometry generation
struct BranchData {
    glm::vec3 origin;
    glm::quat orientation;
    float length;
    float radius;
    int level;
    int sectionCount;
    int segmentCount;
    int32_t parentBranchIndex{-1};  // Index of parent branch (-1 for trunk)
    std::vector<SectionData> sections;
};

// Leaf placement data
struct LeafData {
    glm::vec3 position;
    glm::quat orientation;
    float size;
};

// Complete tree mesh data for GPU upload
struct TreeMeshData {
    std::vector<BranchData> branches;
    std::vector<LeafData> leaves;

    // Vertex counts for buffer allocation
    uint32_t totalBranchVertices() const;
    uint32_t totalBranchIndices() const;
    uint32_t totalLeafVertices() const;
    uint32_t totalLeafIndices() const;

    // Generate skeleton from branch hierarchy
    // Must include TreeSkeleton.h before calling
    TreeSkeleton generateSkeleton() const;
};

// GPU-compatible branch data for compute shader (std430 layout)
struct alignas(16) BranchDataGPU {
    glm::vec4 origin;           // xyz = origin, w = radius
    glm::vec4 orientation;      // quaternion xyzw
    glm::vec4 params;           // x = length, y = level, z = sectionCount, w = segmentCount
    glm::vec4 sectionStart;     // x = first section index in SectionBuffer, y = section count, zw = unused
};

// GPU-compatible section data for compute shader (std430 layout)
struct alignas(16) SectionDataGPU {
    glm::vec4 origin;           // xyz = origin, w = radius
    glm::vec4 orientation;      // quaternion xyzw
};

// GPU-compatible leaf data for compute shader (std430 layout)
struct alignas(16) LeafDataGPU {
    glm::vec4 positionAndSize;  // xyz = position, w = size
    glm::vec4 orientation;      // quaternion xyzw
};

// Tree generator - ports ez-tree algorithm to C++
class TreeGenerator {
public:
    TreeGenerator() = default;

    // Generate tree mesh data from options
    TreeMeshData generate(const TreeOptions& options);

    // Convert to GPU-compatible format
    static void toGPUFormat(const TreeMeshData& meshData,
                           std::vector<BranchDataGPU>& branchesOut,
                           std::vector<SectionDataGPU>& sectionsOut,
                           std::vector<LeafDataGPU>& leavesOut);

private:
    struct Branch {
        glm::vec3 origin;
        glm::quat orientation;
        float length;
        float radius;
        int level;
        int sectionCount;
        int segmentCount;
        int32_t parentBranchIndex{-1};  // Index of parent branch in meshData.branches
    };

    void processBranch(const Branch& branch, const TreeOptions& options,
                       TreeRNG& rng, TreeMeshData& meshData);

    void generateChildBranches(int count, int level,
                               const std::vector<SectionData>& sections,
                               const TreeOptions& options, TreeRNG& rng,
                               std::queue<Branch>& branchQueue,
                               int32_t parentBranchIndex);

    void generateLeaves(const std::vector<SectionData>& sections,
                        const TreeOptions& options, TreeRNG& rng,
                        TreeMeshData& meshData);

    void generateLeaf(const glm::vec3& origin, const glm::quat& orientation,
                      const TreeOptions& options, TreeRNG& rng,
                      TreeMeshData& meshData);
};
