#pragma once

#include "TreeParameters.h"
#include "TreeGeometry.h"
#include "Mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <random>

class TreeGenerator {
public:
    TreeGenerator() = default;
    ~TreeGenerator() = default;

    // Generate tree geometry from parameters
    void generate(const TreeParameters& params);

    // Get generated geometry
    const std::vector<Vertex>& getBranchVertices() const { return branchVertices; }
    const std::vector<uint32_t>& getBranchIndices() const { return branchIndices; }
    const std::vector<LeafInstance>& getLeafInstances() const { return leafInstances; }

    // Get branch segments (for visualization/debugging)
    const std::vector<BranchSegment>& getBranchSegments() const { return segments; }

    // Build mesh from generated geometry
    void buildMesh(Mesh& outMesh);

    // Get leaf vertex data (quad billboards)
    void buildLeafMesh(Mesh& outMesh, const TreeParameters& params);

private:
    // Recursive branch generation
    void generateBranch(const TreeParameters& params,
                       const glm::vec3& startPos,
                       const glm::quat& orientation,
                       float length,
                       float radius,
                       int level,
                       int parentIdx);

    // Generate geometry for a single branch segment
    void generateBranchGeometry(const BranchSegment& segment,
                                const TreeParameters& params);

    // Generate leaves along a branch
    void generateLeaves(const BranchSegment& segment,
                       const TreeParameters& params);

    // Apply natural variation
    glm::quat applyGnarliness(const glm::quat& orientation,
                              const TreeParameters& params);

    // Random number generation
    float randomFloat(float min, float max);
    glm::vec3 randomOnSphere();

    // Space colonisation algorithm
    void generateSpaceColonisation(const TreeParameters& params);

    std::vector<BranchSegment> segments;
    std::vector<Vertex> branchVertices;
    std::vector<uint32_t> branchIndices;
    std::vector<LeafInstance> leafInstances;

    std::mt19937 rng;
};
