#include "TreeGenerator.h"
#include "SpaceColonisationGenerator.h"
#include "CurvedGeometryGenerator.h"
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

    if (params.algorithm == TreeAlgorithm::SpaceColonisation) {
        generateSpaceColonisation(params);
    } else {
        // Original recursive algorithm
        glm::vec3 trunkStart(0.0f, 0.0f, 0.0f);
        glm::quat trunkOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        // Use per-level params or legacy params for trunk
        float trunkLength = params.usePerLevelParams ? params.branchParams[0].length : params.trunkHeight;
        float trunkRadius = params.usePerLevelParams ? params.branchParams[0].radius : params.trunkRadius;

        generateBranch(params,
                       trunkStart,
                       trunkOrientation,
                       trunkLength,
                       trunkRadius,
                       0,  // level 0 = trunk
                       -1); // no parent
    }

    // Generate geometry for all branch segments
    for (const auto& segment : segments) {
        generateBranchGeometry(segment, params);

        // Generate leaves on terminal branches
        if (params.generateLeaves && segment.level >= params.leafStartLevel) {
            generateLeaves(segment, params);
        }
    }

    SDL_Log("Tree generated: %zu segments, %zu vertices, %zu indices, %zu leaves",
            segments.size(), branchVertices.size(), branchIndices.size(), leafInstances.size());
}

void TreeGenerator::generateSpaceColonisation(const TreeParameters& params) {
    SpaceColonisationGenerator scGen(rng);
    std::vector<TreeNode> nodes;

    scGen.generate(params, nodes);

    // Generate curved geometry directly
    CurvedGeometryGenerator curveGen;
    curveGen.generateCurvedBranchGeometry(nodes, params, branchVertices, branchIndices);

    SDL_Log("Space colonisation: Total %zu vertices, %zu indices generated",
            branchVertices.size(), branchIndices.size());
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

    // Get per-level params (clamp level to valid range)
    int levelIdx = std::min(level, 3);
    const auto& levelParams = params.branchParams[levelIdx];

    // Create this branch segment
    BranchSegment segment;
    segment.startPos = startPos;
    segment.orientation = orientation;
    segment.startRadius = radius;
    segment.level = level;
    segment.parentIndex = parentIdx;

    // Calculate end position based on orientation
    glm::vec3 direction = orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    // Apply growth direction influence (ez-tree style force)
    if (params.growthInfluence > 0.0f) {
        glm::vec3 mixed = glm::mix(direction, params.growthDirection, params.growthInfluence);
        float mixedLen = glm::length(mixed);
        if (mixedLen > 0.0001f) {
            direction = mixed / mixedLen;
        }
        // If mixed is zero, keep original direction
    }

    segment.endPos = startPos + direction * length;

    // Calculate end radius (taper) - use per-level or legacy
    float taperRatio = params.usePerLevelParams ? levelParams.taper :
                       (level == 0 ? params.trunkTaper : params.branchTaper);
    segment.endRadius = radius * taperRatio;

    // Store segment
    int segmentIdx = static_cast<int>(segments.size());
    segments.push_back(segment);

    // Don't generate children if at max level
    if (level >= params.branchLevels) return;

    // Get next level params for children
    int nextLevelIdx = std::min(level + 1, 3);
    const auto& nextLevelParams = params.branchParams[nextLevelIdx];

    // Determine where on this branch to spawn children (per-level or legacy)
    float childStartT = params.usePerLevelParams ? nextLevelParams.start :
                        (level == 0 ? params.branchStartHeight : 0.3f);

    // Calculate number of children to spawn (per-level or legacy)
    int numChildren = params.usePerLevelParams ? levelParams.children : params.childrenPerBranch;

    // Spawn child branches
    for (int i = 0; i < numChildren; ++i) {
        // Position along parent branch
        float t = childStartT + (1.0f - childStartT) * (static_cast<float>(i) / static_cast<float>(std::max(1, numChildren)));

        // Interpolate position along branch
        glm::vec3 childStart = glm::mix(segment.startPos, segment.endPos, t);

        // Calculate child radius - use per-level params
        float radiusAtT = glm::mix(segment.startRadius, segment.endRadius, t);
        float childRadius = params.usePerLevelParams ?
                           nextLevelParams.radius :
                           radiusAtT * params.branchRadiusRatio;

        // Calculate child length - use per-level params
        float childLength = params.usePerLevelParams ?
                           nextLevelParams.length :
                           length * params.branchLengthRatio;

        // Calculate child orientation
        // Spread children evenly around the branch
        float spreadAngle = (2.0f * static_cast<float>(M_PI) * static_cast<float>(i)) / static_cast<float>(std::max(1, numChildren));
        spreadAngle += randomFloat(-0.3f, 0.3f);

        // Branching angle from parent (per-level or legacy)
        float branchAngleRad = params.usePerLevelParams ?
                              glm::radians(nextLevelParams.angle) :
                              glm::radians(params.branchingAngle);
        branchAngleRad += randomFloat(-0.1f, 0.1f) * branchAngleRad;

        // Create rotation: first rotate around Y for spread, then tilt outward
        glm::quat spreadRot = glm::angleAxis(spreadAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat tiltRot = glm::angleAxis(branchAngleRad, glm::vec3(1.0f, 0.0f, 0.0f));

        glm::quat childOrientation = orientation * spreadRot * tiltRot;

        // Apply twist along the branch (per-level or legacy)
        float twistAmount = params.usePerLevelParams ? levelParams.twist : params.twistAngle;
        float twist = glm::radians(twistAmount * 30.0f) * t;
        glm::quat twistRot = glm::angleAxis(twist, glm::vec3(0.0f, 1.0f, 0.0f));
        childOrientation = childOrientation * twistRot;

        // Apply gnarliness (random variation) - per-level or legacy
        float gnarlAmount = params.usePerLevelParams ? levelParams.gnarliness : params.gnarliness;
        if (gnarlAmount > 0.0f) {
            float maxAngle = glm::radians(gnarlAmount * 30.0f);
            float rx = randomFloat(-maxAngle, maxAngle);
            float ry = randomFloat(-maxAngle, maxAngle);
            float rz = randomFloat(-maxAngle, maxAngle);
            glm::quat variation = glm::quat(glm::vec3(rx, ry, rz));
            childOrientation = glm::normalize(childOrientation * variation);
        }

        // Recursively generate child branch
        generateBranch(params, childStart, childOrientation, childLength, childRadius, level + 1, segmentIdx);
    }
}

void TreeGenerator::generateBranchGeometry(const BranchSegment& segment,
                                           const TreeParameters& params) {
    // Calculate segment length first - skip degenerate segments to prevent NaN
    // This can happen if startPos == endPos (zero-length branch)
    float length = glm::length(segment.endPos - segment.startPos);
    if (length < 0.0001f) return;  // Skip zero-length segments

    // Skip segments with zero radius (would produce NaN normals)
    if (segment.startRadius < 0.0001f && segment.endRadius < 0.0001f) return;

    // Get per-level segments or use defaults
    int levelIdx = std::min(segment.level, 3);
    int radialSegments = params.usePerLevelParams ?
                        params.branchParams[levelIdx].segments :
                        (segment.level == 0 ? params.trunkSegments : params.branchSegments);
    int rings = params.usePerLevelParams ?
               params.branchParams[levelIdx].sections :
               (segment.level == 0 ? params.trunkRings : params.branchRings);

    // Direction of branch (safe since we checked length > 0)
    glm::vec3 direction = (segment.endPos - segment.startPos) / length;

    // Build coordinate frame (perpendicular to branch direction)
    // Choose an 'up' vector that's not parallel to direction
    glm::vec3 up;
    if (glm::abs(direction.y) > 0.99f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    } else if (glm::abs(direction.x) > 0.99f) {
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    } else if (glm::abs(direction.z) > 0.99f) {
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    }

    glm::vec3 right = glm::cross(up, direction);
    float rightLen = glm::length(right);
    if (rightLen < 0.0001f) {
        // Final fallback - shouldn't happen with the logic above
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Degenerate coordinate frame for direction (%.3f,%.3f,%.3f)",
            direction.x, direction.y, direction.z);
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        right = right / rightLen;
    }
    up = glm::cross(direction, right);

    uint32_t baseVertexIndex = static_cast<uint32_t>(branchVertices.size());

    // Texture scale (apply to UVs for bark texture tiling)
    glm::vec2 texScale = params.barkTextureScale;

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
            glm::vec3 radialDir = right * cosA + up * sinA;
            glm::vec3 offset = radialDir * radius;
            glm::vec3 pos = center + offset;

            // Normal points outward (use radial direction which is always unit length)
            glm::vec3 normal = radialDir;

            // UV coordinates with texture scaling
            // U wraps around circumference, V runs along branch length
            float u = static_cast<float>(i) / static_cast<float>(radialSegments);
            // Scale UVs for texture tiling
            glm::vec2 uv(u * texScale.x, t * length * texScale.y * 0.1f);

            // Tangent along circumference (perpendicular to radial in the ring plane)
            glm::vec3 tangentDir = -right * sinA + up * cosA;
            glm::vec4 tangent(tangentDir, 1.0f);

            // Color: apply bark tint (shader will multiply with texture)
            glm::vec4 color(params.barkTint, 1.0f);

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
    // Calculate branch direction safely
    glm::vec3 branchVec = segment.endPos - segment.startPos;
    float branchLen = glm::length(branchVec);
    if (branchLen < 0.0001f) return;  // Skip leaves on degenerate branches
    glm::vec3 branchDir = branchVec / branchLen;

    // Place leaves along the branch
    for (int i = 0; i < params.leavesPerBranch; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(std::max(1, params.leavesPerBranch));

        // Position along branch - use leafStart parameter
        t = params.leafStart + t * (1.0f - params.leafStart);
        glm::vec3 pos = glm::mix(segment.startPos, segment.endPos, t);

        // Random offset from branch axis
        glm::vec3 offset = randomOnSphere();
        offset -= branchDir * glm::dot(offset, branchDir);  // Project to perpendicular plane
        float offsetLen = glm::length(offset);
        if (offsetLen > 0.001f) {
            offset = offset / offsetLen;
        } else {
            // Fallback: use a perpendicular direction
            offset = glm::abs(branchDir.y) > 0.99f
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : glm::normalize(glm::cross(branchDir, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
        float radius = glm::mix(segment.startRadius, segment.endRadius, t);
        pos += offset * (radius + params.leafSize * 0.5f);

        // Leaf normal - angle from branch (degrees)
        float leafAngleRad = glm::radians(params.leafAngle);
        glm::vec3 normalVec = offset * std::cos(leafAngleRad) +
                              branchDir * std::sin(leafAngleRad) +
                              glm::vec3(0.0f, 0.2f, 0.0f);
        float normalLen = glm::length(normalVec);
        glm::vec3 normal = normalLen > 0.0001f ? normalVec / normalLen : glm::vec3(0.0f, 1.0f, 0.0f);

        // Apply size variance
        float sizeVariance = 1.0f - params.leafSizeVariance + randomFloat(0.0f, 2.0f * params.leafSizeVariance);

        LeafInstance leaf;
        leaf.position = pos;
        leaf.normal = normal;
        leaf.size = params.leafSize * sizeVariance;
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

void TreeGenerator::buildLeafMesh(Mesh& outMesh, const TreeParameters& params) {
    if (leafInstances.empty()) return;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    bool doubleBillboard = (params.leafBillboard == BillboardMode::Double);

    // Create quads for each leaf
    for (size_t i = 0; i < leafInstances.size(); ++i) {
        const auto& leaf = leafInstances[i];

        // Skip leaves with NaN position or normal
        if (std::isnan(leaf.position.x) || std::isnan(leaf.position.y) || std::isnan(leaf.position.z) ||
            std::isnan(leaf.normal.x) || std::isnan(leaf.normal.y) || std::isnan(leaf.normal.z)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Skipping leaf %zu with NaN data", i);
            continue;
        }

        // Build tangent space from normal
        glm::vec3 right;
        glm::vec3 crossVec = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), leaf.normal);
        float crossLen = glm::length(crossVec);
        if (crossLen < 0.001f) {
            // Normal is nearly vertical, use a different reference
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        } else {
            right = crossVec / crossLen;
        }
        glm::vec3 up = glm::cross(leaf.normal, right);

        // Apply rotation around normal
        float c = std::cos(leaf.rotation);
        float s = std::sin(leaf.rotation);
        glm::vec3 rotRight = right * c + up * s;
        glm::vec3 rotUp = -right * s + up * c;

        float halfSize = leaf.size * 0.5f;

        // Leaf tint color
        glm::vec4 color(params.leafTint, 1.0f);

        glm::vec2 uvs[4] = {
            {0.0f, 1.0f},
            {1.0f, 1.0f},
            {1.0f, 0.0f},
            {0.0f, 0.0f}
        };

        // First quad (always created)
        {
            uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

            glm::vec3 corners[4] = {
                leaf.position + (-rotRight - rotUp) * halfSize,
                leaf.position + ( rotRight - rotUp) * halfSize,
                leaf.position + ( rotRight + rotUp) * halfSize,
                leaf.position + (-rotRight + rotUp) * halfSize
            };

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

        // Second quad (perpendicular) for double billboard mode
        if (doubleBillboard) {
            uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

            // Rotate 90 degrees around up axis for perpendicular billboard
            glm::vec3 rotRight2 = leaf.normal;
            glm::vec3 normal2 = -rotRight;

            glm::vec3 corners[4] = {
                leaf.position + (-rotRight2 - rotUp) * halfSize,
                leaf.position + ( rotRight2 - rotUp) * halfSize,
                leaf.position + ( rotRight2 + rotUp) * halfSize,
                leaf.position + (-rotRight2 + rotUp) * halfSize
            };

            for (int j = 0; j < 4; ++j) {
                vertices.push_back({
                    corners[j],
                    normal2,
                    uvs[j],
                    glm::vec4(rotRight2, 1.0f),
                    color
                });
            }

            indices.push_back(baseIdx);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 3);
        }
    }

    // Debug: Check for NaN in leaf mesh vertices
    int nanLeafCount = 0;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto& v = vertices[i];
        if (std::isnan(v.position.x) || std::isnan(v.position.y) || std::isnan(v.position.z) ||
            std::isnan(v.normal.x) || std::isnan(v.normal.y) || std::isnan(v.normal.z)) {
            if (nanLeafCount < 5) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "NaN in leaf mesh vertex %zu: pos(%.2f,%.2f,%.2f) normal(%.2f,%.2f,%.2f)",
                    i, v.position.x, v.position.y, v.position.z,
                    v.normal.x, v.normal.y, v.normal.z);
            }
            nanLeafCount++;
        }
    }
    if (nanLeafCount > 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Total NaN leaf mesh vertices: %d", nanLeafCount);
    }

    outMesh.setCustomGeometry(vertices, indices);
}
