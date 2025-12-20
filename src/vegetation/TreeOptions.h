#pragma once

#include <glm/glm.hpp>
#include <array>
#include <string>

// Tree branching type
enum class TreeType {
    Deciduous = 0,  // Terminal branching (branches from end)
    Evergreen = 1   // Radial branching (branches along trunk)
};

// Leaf billboard mode
enum class BillboardMode {
    Single = 0,  // Single quad
    Double = 1   // Two perpendicular quads
};

// Bark appearance options
struct BarkOptions {
    std::string type = "oak";  // Texture type name (oak, pine, birch, willow)
    glm::vec3 tint = glm::vec3(1.0f);
    bool flatShading = false;
    bool textured = true;
    glm::vec2 textureScale = glm::vec2(1.0f);
};

// Branch structure options
struct BranchOptions {
    // Number of branch recursion levels (0 = trunk only, max 3)
    int levels = 3;

    // Angle of child branches relative to parent (degrees) [per level 1-3]
    std::array<float, 4> angle = {0.0f, 70.0f, 60.0f, 60.0f};

    // Number of children per branch [per level 0-2]
    std::array<int, 4> children = {7, 7, 5, 0};

    // External force direction for growth
    glm::vec3 forceDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float forceStrength = 0.01f;

    // Amount of curling/twisting [per level 0-3]
    std::array<float, 4> gnarliness = {0.15f, 0.2f, 0.3f, 0.02f};

    // Length of each branch level
    std::array<float, 4> length = {20.0f, 20.0f, 10.0f, 1.0f};

    // Radius of each branch level
    std::array<float, 4> radius = {1.5f, 0.7f, 0.7f, 0.7f};

    // Number of sections (length subdivisions) per level
    std::array<int, 4> sections = {12, 10, 8, 6};

    // Number of radial segments per level
    std::array<int, 4> segments = {8, 6, 4, 3};

    // Where child branches start on parent (0-1) [per level 1-3]
    std::array<float, 4> start = {0.0f, 0.4f, 0.3f, 0.3f};

    // Taper factor per level (radius reduction along branch)
    std::array<float, 4> taper = {0.7f, 0.7f, 0.7f, 0.7f};

    // Twist amount per level (radians per section)
    std::array<float, 4> twist = {0.0f, 0.0f, 0.0f, 0.0f};
};

// Leaf appearance options
struct LeafOptions {
    std::string type = "oak";  // Texture type name (oak, ash, aspen, pine)
    BillboardMode billboard = BillboardMode::Double;

    // Angle of leaves relative to parent branch (degrees)
    float angle = 10.0f;

    // Number of leaves per final branch
    int count = 1;

    // Where leaves start on branch length (0-1)
    float start = 0.0f;

    // Base size of leaves
    float size = 2.5f;

    // Variance in leaf size (0-1)
    float sizeVariance = 0.7f;

    // Leaf color tint
    glm::vec3 tint = glm::vec3(1.0f);

    // Alpha test threshold for transparency
    float alphaTest = 0.5f;
};

// Complete tree configuration
struct TreeOptions {
    uint32_t seed = 0;
    TreeType type = TreeType::Deciduous;
    BarkOptions bark;
    BranchOptions branch;
    LeafOptions leaves;

    // Built-in presets
    static TreeOptions defaultOak();
    static TreeOptions defaultPine();
    static TreeOptions defaultBirch();
    static TreeOptions defaultWillow();
    static TreeOptions defaultAspen();
    static TreeOptions defaultBush();

    // Load from JSON preset file
    static TreeOptions loadFromJson(const std::string& jsonPath);
    static TreeOptions loadFromJsonString(const std::string& jsonString);
};

// GPU-compatible tree parameters UBO (std140 layout)
struct alignas(16) TreeParamsGPU {
    // Basic params
    uint32_t seed;
    uint32_t type;           // 0=deciduous, 1=evergreen
    uint32_t branchLevels;
    uint32_t pad0;

    // Per-level branch parameters (vec4 for std140 alignment)
    glm::vec4 branchAngle;       // degrees per level
    glm::vec4 branchChildren;    // children count per level
    glm::vec4 branchGnarliness;  // gnarliness per level
    glm::vec4 branchLength;      // length per level
    glm::vec4 branchRadius;      // radius per level
    glm::vec4 branchSections;    // sections per level
    glm::vec4 branchSegments;    // segments per level
    glm::vec4 branchStart;       // start position per level
    glm::vec4 branchTaper;       // taper per level
    glm::vec4 branchTwist;       // twist per level

    // Force
    glm::vec4 forceDirectionAndStrength;  // xyz=direction, w=strength

    // Leaves
    uint32_t leafBillboard;   // 0=single, 1=double
    float leafAngle;
    uint32_t leafCount;
    float leafStart;

    float leafSize;
    float leafSizeVariance;
    float leafAlphaTest;
    uint32_t pad1;

    // Bark
    uint32_t barkType;
    uint32_t barkTextured;
    glm::vec2 barkTextureScale;

    glm::vec4 barkTint;
    glm::vec4 leafTint;

    static TreeParamsGPU fromOptions(const TreeOptions& opts);
};

// Tree instance data for positioning in world
struct TreeInstance {
    glm::vec3 position = glm::vec3(0.0f);
    float rotation = 0.0f;  // Y-axis rotation (radians)
    float scale = 1.0f;
    uint32_t optionsIndex = 0;  // Index into TreeOptions array
};

// GPU-compatible instance data (std140 layout)
struct alignas(16) TreeInstanceGPU {
    glm::vec4 positionAndRotation;  // xyz=position, w=rotation
    glm::vec4 scaleAndIndices;      // x=scale, y=optionsIndex, zw=unused

    static TreeInstanceGPU fromInstance(const TreeInstance& inst);
};
