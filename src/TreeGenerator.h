#pragma once

#include "Mesh.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <random>
#include <array>

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

// Bark texture types (matches ez-tree)
enum class BarkType {
    Oak = 0,
    Birch = 1,
    Pine = 2,
    Willow = 3
};

// Leaf texture types (matches ez-tree)
enum class LeafType {
    Oak = 0,
    Ash = 1,
    Aspen = 2,
    Pine = 3
};

// Billboard rendering mode for leaves
enum class BillboardMode {
    Single,     // Single-sided quad
    Double      // Two perpendicular quads for 3D effect
};

// Tree type (affects terminal branch behavior)
enum class TreeType {
    Deciduous,  // Terminal branch extends from parent end
    Evergreen   // No terminal branch, cone-like shape
};

// Per-level branch parameters (ez-tree style)
struct BranchLevelParams {
    float angle = 60.0f;        // Angle from parent branch (degrees)
    int children = 5;           // Number of child branches
    float gnarliness = 0.2f;    // Random twist/curve amount
    float length = 10.0f;       // Length of branches at this level
    float radius = 0.7f;        // Radius at this level
    int sections = 8;           // Segments along branch length
    int segments = 6;           // Radial segments
    float start = 0.3f;         // Where children start on parent (0-1)
    float taper = 0.7f;         // Taper from base to tip (0-1)
    float twist = 0.0f;         // Axial twist amount
};

// Space colonisation specific parameters (scaled for meters)
struct SpaceColonisationParams {
    // Crown volume shape and size
    VolumeShape crownShape = VolumeShape::Sphere;
    float crownRadius = 3.0f;           // Base radius of crown volume (~3m)
    float crownHeight = 4.0f;           // Height for non-spherical shapes
    glm::vec3 crownOffset = glm::vec3(0.0f, 0.0f, 0.0f);  // Offset from trunk top
    glm::vec3 crownScale = glm::vec3(1.0f, 1.0f, 1.0f);   // For ellipsoid scaling
    float crownExclusionRadius = 0.0f;  // Inner hollow zone (no points here)

    // Attraction point parameters
    int attractionPointCount = 500;     // Number of attraction points
    bool uniformDistribution = true;    // Uniform vs clustered distribution

    // Algorithm parameters
    float attractionDistance = 2.0f;    // Max distance for point to influence node (di)
    float killDistance = 0.3f;          // Distance at which point is removed (dk)
    float segmentLength = 0.2f;         // Length of each growth step (D)
    int maxIterations = 200;            // Safety limit for iterations
    float branchAngleLimit = 45.0f;     // Max angle change per segment (degrees)

    // Tropism (directional growth influence)
    glm::vec3 tropismDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float tropismStrength = 0.1f;       // How much tropism affects growth

    // Initial trunk before branching
    float trunkSegments = 3;            // Number of trunk segments before crown
    float trunkHeight = 2.5f;           // Height of trunk before crown starts (~2.5m)

    // Root system
    bool generateRoots = false;
    VolumeShape rootShape = VolumeShape::Hemisphere;
    float rootRadius = 1.5f;
    float rootDepth = 1.0f;
    int rootAttractionPointCount = 200;
    float rootTropismStrength = 0.3f;   // Downward tropism for roots

    // Branch thickness calculation
    float baseThickness = 0.2f;         // Trunk base thickness (~20cm)
    float thicknessPower = 2.0f;        // Exponent for pipe model (da Vinci's rule)
    float minThickness = 0.01f;         // Minimum branch thickness (~1cm)

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

    // Tree type
    TreeType treeType = TreeType::Deciduous;

    // Bark parameters
    BarkType barkType = BarkType::Oak;
    glm::vec3 barkTint = glm::vec3(1.0f);       // Tint color for bark
    bool barkTextured = true;                    // Whether to apply bark texture
    glm::vec2 barkTextureScale = glm::vec2(1.0f); // UV scale for bark texture
    bool barkFlatShading = false;                // Use flat shading for bark

    // Per-level branch parameters (ez-tree style, levels 0-3)
    // Scaled for meters: typical tree ~5-8m tall, trunk radius ~0.15-0.25m
    std::array<BranchLevelParams, 4> branchParams = {{
        // Level 0 (trunk): ~5m tall, 0.2m radius
        { 0.0f, 5, 0.15f, 5.0f, 0.2f, 12, 8, 0.0f, 0.7f, 0.0f },
        // Level 1: main branches ~2.5m long, 0.08m radius
        { 45.0f, 4, 0.2f, 2.5f, 0.08f, 10, 6, 0.5f, 0.7f, 0.0f },
        // Level 2: secondary branches ~1.2m long, 0.03m radius
        { 50.0f, 3, 0.3f, 1.2f, 0.03f, 8, 4, 0.3f, 0.7f, 0.0f },
        // Level 3: twigs ~0.4m long, 0.01m radius
        { 55.0f, 0, 0.02f, 0.4f, 0.01f, 6, 3, 0.3f, 0.7f, 0.0f }
    }};

    // Number of branch recursion levels (0-3)
    int branchLevels = 3;

    // Growth direction influence (external force)
    glm::vec3 growthDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float growthInfluence = 0.01f;       // How much growth direction affects branches

    // Leaf parameters (scaled for meters)
    // ez-tree uses leaf size ~12.5% of trunk height for good visual balance
    LeafType leafType = LeafType::Oak;
    glm::vec3 leafTint = glm::vec3(1.0f);       // Tint color for leaves
    BillboardMode leafBillboard = BillboardMode::Double;
    float leafAlphaTest = 0.5f;                  // Alpha discard threshold
    bool generateLeaves = true;
    float leafSize = 0.5f;                       // ~50cm leaves (matches ez-tree ratio)
    float leafSizeVariance = 0.5f;               // Random size variance (0-1)
    int leavesPerBranch = 3;                     // Number of leaves per branch
    float leafAngle = 30.0f;                     // Angle from branch (degrees)
    float leafStart = 0.3f;                      // Where leaves start on branch (0-1)
    int leafStartLevel = 2;                      // Branch level where leaves start

    // Legacy parameters (for backwards compatibility with existing code)
    // These are derived from branchParams when using per-level system
    float trunkHeight = 5.0f;           // Height of main trunk (uses branchParams[0].length)
    float trunkRadius = 0.2f;           // Radius at trunk base (uses branchParams[0].radius)
    float trunkTaper = 0.6f;            // Taper ratio (uses branchParams[0].taper)
    int trunkSegments = 8;              // Radial segments for trunk
    int trunkRings = 6;                 // Vertical segments for trunk

    // Legacy branch parameters (used when not using per-level system)
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

    // Quality settings
    int branchSegments = 6;             // Radial segments for branches
    int branchRings = 4;                // Segments along branch length
    float minBranchRadius = 0.02f;      // Stop branching below this radius

    // Whether to use per-level parameters (ez-tree style) vs legacy parameters
    bool usePerLevelParams = true;
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
