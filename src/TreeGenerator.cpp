#include "TreeGenerator.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void TreeGenerator::generate(const TreeParameters& params) {
    // Clear previous data
    segments.clear();
    branchVertices.clear();
    branchIndices.clear();
    leafInstances.clear();

    // Seed random generator
    rng.seed(params.seed);

    // Start with trunk
    glm::vec3 trunkStart(0.0f, 0.0f, 0.0f);
    glm::quat trunkOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    // Generate trunk and all branches recursively
    generateBranch(params,
                   trunkStart,
                   trunkOrientation,
                   params.trunkHeight,
                   params.trunkRadius,
                   0,  // level 0 = trunk
                   -1); // no parent

    // Generate geometry for all branch segments
    for (const auto& segment : segments) {
        generateBranchGeometry(segment, params);

        // Generate leaves on terminal branches
        if (params.generateLeaves && segment.level >= params.leafStartLevel) {
            generateLeaves(segment, params);
        }
    }

    SDL_Log("Tree generated: %zu segments, %zu vertices, %zu leaves",
            segments.size(), branchVertices.size(), leafInstances.size());
}

void TreeGenerator::generateBranch(const TreeParameters& params,
                                   const glm::vec3& startPos,
                                   const glm::quat& orientation,
                                   float length,
                                   float radius,
                                   int level,
                                   int parentIdx) {
    // Check termination conditions
    if (level > params.branchLevels) return;
    if (radius < params.minBranchRadius) return;

    // Create this branch segment
    BranchSegment segment;
    segment.startPos = startPos;
    segment.orientation = orientation;
    segment.startRadius = radius;
    segment.level = level;
    segment.parentIndex = parentIdx;

    // Calculate end position based on orientation
    glm::vec3 direction = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    // Apply growth direction influence
    if (params.growthInfluence > 0.0f) {
        direction = glm::normalize(
            glm::mix(direction, params.growthDirection, params.growthInfluence)
        );
    }

    segment.endPos = startPos + direction * length;

    // Calculate end radius (taper)
    float taperRatio = level == 0 ? params.trunkTaper : params.branchTaper;
    segment.endRadius = radius * taperRatio;

    // Store segment
    int segmentIdx = static_cast<int>(segments.size());
    segments.push_back(segment);

    // Don't generate children if at max level
    if (level >= params.branchLevels) return;

    // Determine where on this branch to spawn children
    float childStartT = level == 0 ? params.branchStartHeight : 0.3f;

    // Calculate number of children to spawn
    int numChildren = params.childrenPerBranch;
    if (level == 0) {
        // Trunk gets more branches
        numChildren = static_cast<int>(numChildren * 1.5f);
    }

    // Spawn child branches
    for (int i = 0; i < numChildren; ++i) {
        // Position along parent branch
        float t = childStartT + (1.0f - childStartT) * (static_cast<float>(i) / static_cast<float>(numChildren));

        // Interpolate position along branch
        glm::vec3 childStart = glm::mix(segment.startPos, segment.endPos, t);

        // Calculate child radius at spawn point
        float radiusAtT = glm::mix(segment.startRadius, segment.endRadius, t);
        float childRadius = radiusAtT * params.branchRadiusRatio;

        // Calculate child length
        float childLength = length * params.branchLengthRatio;

        // Calculate child orientation
        // Spread children around the branch
        float spreadAngle = (static_cast<float>(i) / static_cast<float>(numChildren)) * glm::radians(params.branchingSpread);
        spreadAngle += randomFloat(-0.2f, 0.2f) * glm::radians(params.branchingSpread);

        // Branching angle from parent
        float branchAngle = glm::radians(params.branchingAngle);
        branchAngle += randomFloat(-10.0f, 10.0f) * glm::radians(1.0f);

        // Create rotation: first rotate around Y for spread, then tilt outward
        glm::quat spreadRot = glm::angleAxis(spreadAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat tiltRot = glm::angleAxis(branchAngle, glm::vec3(1.0f, 0.0f, 0.0f));

        glm::quat childOrientation = orientation * spreadRot * tiltRot;

        // Apply twist along the branch
        float twist = glm::radians(params.twistAngle) * t;
        glm::quat twistRot = glm::angleAxis(twist, glm::vec3(0.0f, 1.0f, 0.0f));
        childOrientation = childOrientation * twistRot;

        // Apply gnarliness (random variation)
        childOrientation = applyGnarliness(childOrientation, params);

        // Recursively generate child branch
        generateBranch(params, childStart, childOrientation, childLength, childRadius, level + 1, segmentIdx);
    }
}

void TreeGenerator::generateBranchGeometry(const BranchSegment& segment,
                                           const TreeParameters& params) {
    int radialSegments = segment.level == 0 ? params.trunkSegments : params.branchSegments;
    int rings = segment.level == 0 ? params.trunkRings : params.branchRings;

    // Direction of branch
    glm::vec3 direction = glm::normalize(segment.endPos - segment.startPos);
    float length = glm::length(segment.endPos - segment.startPos);

    // Build coordinate frame
    glm::vec3 up = glm::abs(direction.y) > 0.99f
        ? glm::vec3(1.0f, 0.0f, 0.0f)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(up, direction));
    up = glm::cross(direction, right);

    uint32_t baseVertexIndex = static_cast<uint32_t>(branchVertices.size());

    // Generate vertices for each ring
    for (int ring = 0; ring <= rings; ++ring) {
        float t = static_cast<float>(ring) / static_cast<float>(rings);
        glm::vec3 center = glm::mix(segment.startPos, segment.endPos, t);
        float radius = glm::mix(segment.startRadius, segment.endRadius, t);

        for (int i = 0; i <= radialSegments; ++i) {
            float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(radialSegments);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            // Position on ring
            glm::vec3 offset = (right * cosA + up * sinA) * radius;
            glm::vec3 pos = center + offset;

            // Normal points outward
            glm::vec3 normal = glm::normalize(offset);

            // UV coordinates
            glm::vec2 uv(static_cast<float>(i) / static_cast<float>(radialSegments), t);

            // Tangent along circumference
            glm::vec3 tangentDir = glm::normalize(-right * sinA + up * cosA);
            glm::vec4 tangent(tangentDir, 1.0f);

            // Color based on branch level (for visualization)
            float levelColor = 1.0f - static_cast<float>(segment.level) / static_cast<float>(params.branchLevels + 1);
            glm::vec4 color(0.4f * levelColor + 0.2f, 0.25f * levelColor + 0.1f, 0.1f, 1.0f);

            branchVertices.push_back({pos, normal, uv, tangent, color});
        }
    }

    // Generate indices
    for (int ring = 0; ring < rings; ++ring) {
        for (int i = 0; i < radialSegments; ++i) {
            uint32_t current = baseVertexIndex + ring * (radialSegments + 1) + i;
            uint32_t next = current + 1;
            uint32_t below = current + (radialSegments + 1);
            uint32_t belowNext = below + 1;

            // Two triangles per quad
            branchIndices.push_back(current);
            branchIndices.push_back(next);
            branchIndices.push_back(below);

            branchIndices.push_back(next);
            branchIndices.push_back(belowNext);
            branchIndices.push_back(below);
        }
    }
}

void TreeGenerator::generateLeaves(const BranchSegment& segment,
                                   const TreeParameters& params) {
    glm::vec3 branchDir = glm::normalize(segment.endPos - segment.startPos);

    // Place leaves along the branch
    for (int i = 0; i < params.leavesPerBranch; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(params.leavesPerBranch);

        // Position along branch (favor end of branch)
        t = 0.3f + t * 0.7f;
        glm::vec3 pos = glm::mix(segment.startPos, segment.endPos, t);

        // Random offset from branch axis
        glm::vec3 offset = randomOnSphere();
        offset -= branchDir * glm::dot(offset, branchDir);  // Project to perpendicular plane
        if (glm::length(offset) > 0.001f) {
            offset = glm::normalize(offset);
        }
        float radius = glm::mix(segment.startRadius, segment.endRadius, t);
        pos += offset * (radius + params.leafSize * 0.5f);

        // Leaf normal points outward and upward
        glm::vec3 normal = glm::normalize(offset + glm::vec3(0.0f, 0.3f, 0.0f));

        LeafInstance leaf;
        leaf.position = pos;
        leaf.normal = normal;
        leaf.size = params.leafSize * (0.7f + randomFloat(0.0f, 0.6f));
        leaf.rotation = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));

        leafInstances.push_back(leaf);
    }
}

glm::quat TreeGenerator::applyGnarliness(const glm::quat& orientation,
                                          const TreeParameters& params) {
    if (params.gnarliness <= 0.0f) return orientation;

    // Random rotation angles
    float maxAngle = glm::radians(params.gnarliness * 30.0f);
    float rx = randomFloat(-maxAngle, maxAngle);
    float ry = randomFloat(-maxAngle, maxAngle);
    float rz = randomFloat(-maxAngle, maxAngle);

    glm::quat variation = glm::quat(glm::vec3(rx, ry, rz));
    return glm::normalize(orientation * variation);
}

float TreeGenerator::randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng);
}

glm::vec3 TreeGenerator::randomOnSphere() {
    float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
    float phi = std::acos(randomFloat(-1.0f, 1.0f));

    float x = std::sin(phi) * std::cos(theta);
    float y = std::sin(phi) * std::sin(theta);
    float z = std::cos(phi);

    return glm::vec3(x, y, z);
}

void TreeGenerator::buildMesh(Mesh& outMesh) {
    if (branchVertices.empty()) return;
    outMesh.setCustomGeometry(branchVertices, branchIndices);
}

void TreeGenerator::buildLeafMesh(Mesh& outMesh) {
    if (leafInstances.empty()) return;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Create a quad for each leaf
    for (size_t i = 0; i < leafInstances.size(); ++i) {
        const auto& leaf = leafInstances[i];

        // Build tangent space from normal
        glm::vec3 right;
        if (std::abs(leaf.normal.y) > 0.99f) {
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        } else {
            right = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), leaf.normal));
        }
        glm::vec3 up = glm::cross(leaf.normal, right);

        // Apply rotation around normal
        float c = std::cos(leaf.rotation);
        float s = std::sin(leaf.rotation);
        glm::vec3 rotRight = right * c + up * s;
        glm::vec3 rotUp = -right * s + up * c;

        float halfSize = leaf.size * 0.5f;

        uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

        // Four corners of quad
        glm::vec3 corners[4] = {
            leaf.position + (-rotRight - rotUp) * halfSize,
            leaf.position + ( rotRight - rotUp) * halfSize,
            leaf.position + ( rotRight + rotUp) * halfSize,
            leaf.position + (-rotRight + rotUp) * halfSize
        };

        glm::vec2 uvs[4] = {
            {0.0f, 1.0f},
            {1.0f, 1.0f},
            {1.0f, 0.0f},
            {0.0f, 0.0f}
        };

        // Green leaf color
        glm::vec4 color(0.2f, 0.5f, 0.15f, 1.0f);

        for (int j = 0; j < 4; ++j) {
            vertices.push_back({
                corners[j],
                leaf.normal,
                uvs[j],
                glm::vec4(rotRight, 1.0f),
                color
            });
        }

        // Two triangles per quad
        indices.push_back(baseIdx);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx);
        indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 3);
    }

    outMesh.setCustomGeometry(vertices, indices);
}
