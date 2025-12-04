#pragma once

#include "Mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <random>

// Tree generation parameters similar to ez-tree
struct TreeParameters {
    // Seed for reproducibility
    uint32_t seed = 12345;

    // Trunk parameters
    float trunkHeight = 5.0f;           // Height of main trunk
    float trunkRadius = 0.3f;           // Radius at trunk base
    float trunkTaper = 0.6f;            // Taper ratio (0-1, 1 = no taper)
    int trunkSegments = 8;              // Radial segments for trunk
    int trunkRings = 6;                 // Vertical segments for trunk

    // Branch parameters
    int branchLevels = 3;               // Number of branch recursion levels
    int childrenPerBranch = 4;          // Number of child branches per parent
    float branchingAngle = 35.0f;       // Angle from parent branch (degrees)
    float branchingSpread = 120.0f;     // Spread angle around parent (degrees)
    float branchLengthRatio = 0.7f;     // Child length as ratio of parent
    float branchRadiusRatio = 0.5f;     // Child radius as ratio of parent
    float branchTaper = 0.5f;           // Taper along each branch
    float branchStartHeight = 0.4f;     // Where branches start on trunk (0-1)

    // Curvature and natural variation
    float gnarliness = 0.2f;            // Random twist/curve amount
    float twistAngle = 15.0f;           // Twist per segment (degrees)

    // Growth direction influence
    glm::vec3 growthDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float growthInfluence = 0.3f;       // How much growth direction affects branches

    // Leaf parameters
    bool generateLeaves = true;
    float leafSize = 0.3f;
    int leavesPerBranch = 8;
    float leafAngle = 45.0f;            // Angle from branch (degrees)
    int leafStartLevel = 2;           // Branch level where leaves start

    // Quality settings
    int branchSegments = 6;             // Radial segments for branches
    int branchRings = 4;                // Segments along branch length
    float minBranchRadius = 0.02f;      // Stop branching below this radius
};

// Represents a branch segment for building the tree
struct BranchSegment {
    glm::vec3 startPos;
    glm::vec3 endPos;
    glm::quat orientation;
    float startRadius;
    float endRadius;
    int level;              // Branch depth level (0 = trunk)
    int parentIndex;        // Index of parent segment (-1 for trunk base)
};

// Leaf billboard data
struct LeafInstance {
    glm::vec3 position;
    glm::vec3 normal;
    float size;
    float rotation;         // Rotation around normal
};

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
    void buildLeafMesh(Mesh& outMesh);

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

    std::vector<BranchSegment> segments;
    std::vector<Vertex> branchVertices;
    std::vector<uint32_t> branchIndices;
    std::vector<LeafInstance> leafInstances;

    std::mt19937 rng;
};
