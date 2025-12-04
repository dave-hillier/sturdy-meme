#pragma once

#include "Mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <random>

// Algorithm selection
enum class TreeAlgorithm {
    Recursive,          // Original recursive branching algorithm
    SpaceColonisation   // Space colonisation algorithm
};

// Volume shapes for space colonisation
enum class VolumeShape {
    Sphere,
    Hemisphere,
    Cone,
    Cylinder,
    Ellipsoid,
    Box
};

// Space colonisation specific parameters
struct SpaceColonisationParams {
    // Crown volume shape and size
    VolumeShape crownShape = VolumeShape::Sphere;
    float crownRadius = 4.0f;           // Base radius of crown volume
    float crownHeight = 5.0f;           // Height for non-spherical shapes
    glm::vec3 crownOffset = glm::vec3(0.0f, 0.0f, 0.0f);  // Offset from trunk top
    glm::vec3 crownScale = glm::vec3(1.0f, 1.0f, 1.0f);   // For ellipsoid scaling
    float crownExclusionRadius = 0.0f;  // Inner hollow zone (no points here)

    // Attraction point parameters
    int attractionPointCount = 500;     // Number of attraction points
    bool uniformDistribution = true;    // Uniform vs clustered distribution

    // Algorithm parameters
    float attractionDistance = 3.0f;    // Max distance for point to influence node (di)
    float killDistance = 0.5f;          // Distance at which point is removed (dk)
    float segmentLength = 0.3f;         // Length of each growth step (D)
    int maxIterations = 200;            // Safety limit for iterations
    float branchAngleLimit = 45.0f;     // Max angle change per segment (degrees)

    // Tropism (directional growth influence)
    glm::vec3 tropismDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float tropismStrength = 0.1f;       // How much tropism affects growth

    // Initial trunk before branching
    float trunkSegments = 3;            // Number of trunk segments before crown
    float trunkHeight = 3.0f;           // Height of trunk before crown starts

    // Root system
    bool generateRoots = false;
    VolumeShape rootShape = VolumeShape::Hemisphere;
    float rootRadius = 2.0f;
    float rootDepth = 1.5f;
    int rootAttractionPointCount = 200;
    float rootTropismStrength = 0.3f;   // Downward tropism for roots

    // Branch thickness calculation
    float baseThickness = 0.3f;         // Trunk base thickness
    float thicknessPower = 2.0f;        // Exponent for pipe model (da Vinci's rule)
    float minThickness = 0.02f;         // Minimum branch thickness

    // Geometry quality settings
    int radialSegments = 8;             // Segments around circumference
    int curveSubdivisions = 3;          // Subdivisions per branch for smooth curves
    float smoothingStrength = 0.5f;     // How much to smooth branch curves (0-1)
};

// Tree generation parameters similar to ez-tree
struct TreeParameters {
    // Algorithm selection
    TreeAlgorithm algorithm = TreeAlgorithm::Recursive;

    // Space colonisation specific parameters
    SpaceColonisationParams spaceColonisation;

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

// Node for space colonisation algorithm
struct TreeNode {
    glm::vec3 position;
    int parentIndex;        // -1 for root
    int childCount;         // Number of children (for thickness calculation)
    float thickness;        // Calculated branch thickness
    bool isTerminal;        // Is this a leaf node?
    int depth;              // Depth from root (for level calculation)
    std::vector<int> childIndices;  // Indices of child nodes
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

    // Space colonisation algorithm
    void generateSpaceColonisation(const TreeParameters& params);

    // Generate points within a volume shape
    void generateAttractionPoints(const SpaceColonisationParams& scParams,
                                  const glm::vec3& center,
                                  bool isRoot,
                                  std::vector<glm::vec3>& outPoints);

    // Check if point is inside volume
    bool isPointInVolume(const glm::vec3& point,
                        const glm::vec3& center,
                        VolumeShape shape,
                        float radius,
                        float height,
                        const glm::vec3& scale,
                        float exclusionRadius);

    // Generate random point in volume
    glm::vec3 randomPointInVolume(VolumeShape shape,
                                  float radius,
                                  float height,
                                  const glm::vec3& scale);

    // Run space colonisation iteration
    bool spaceColonisationStep(std::vector<TreeNode>& nodes,
                               std::vector<glm::vec3>& attractionPoints,
                               const SpaceColonisationParams& params,
                               const glm::vec3& tropismDir,
                               float tropismStrength);

    // Calculate branch thicknesses using pipe model
    void calculateBranchThickness(std::vector<TreeNode>& nodes,
                                  const SpaceColonisationParams& params);

    // Convert tree nodes to branch segments
    void nodesToSegments(const std::vector<TreeNode>& nodes,
                        const TreeParameters& params);

    // Generate curved geometry for space colonisation trees
    void generateCurvedBranchGeometry(const std::vector<TreeNode>& nodes,
                                      const TreeParameters& params);

    // Generate a curved tube along a path of points
    void generateCurvedTube(const std::vector<glm::vec3>& points,
                           const std::vector<float>& radii,
                           int radialSegments,
                           int level,
                           const TreeParameters& params);

    // Catmull-Rom spline interpolation
    glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                         const glm::vec3& p2, const glm::vec3& p3, float t);

    // Build child index lists for nodes
    void buildChildIndices(std::vector<TreeNode>& nodes);

    // Find branch chains (sequences of single-child nodes)
    void findBranchChains(const std::vector<TreeNode>& nodes,
                          std::vector<std::vector<int>>& chains);

    std::vector<BranchSegment> segments;
    std::vector<Vertex> branchVertices;
    std::vector<uint32_t> branchIndices;
    std::vector<LeafInstance> leafInstances;

    std::mt19937 rng;
};
