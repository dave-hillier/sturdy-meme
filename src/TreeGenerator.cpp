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
        direction = glm::normalize(
            glm::mix(direction, params.growthDirection, params.growthInfluence)
        );
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
    // Get per-level segments or use defaults
    int levelIdx = std::min(segment.level, 3);
    int radialSegments = params.usePerLevelParams ?
                        params.branchParams[levelIdx].segments :
                        (segment.level == 0 ? params.trunkSegments : params.branchSegments);
    int rings = params.usePerLevelParams ?
               params.branchParams[levelIdx].sections :
               (segment.level == 0 ? params.trunkRings : params.branchRings);

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
            glm::vec3 offset = (right * cosA + up * sinA) * radius;
            glm::vec3 pos = center + offset;

            // Normal points outward
            glm::vec3 normal = glm::normalize(offset);

            // UV coordinates with texture scaling
            // U wraps around circumference, V runs along branch length
            float u = static_cast<float>(i) / static_cast<float>(radialSegments);
            // Alternate V between 0 and 1 for adjacent rings to improve seaming (ez-tree style)
            float v = (ring % 2 == 0) ? 0.0f : 1.0f;
            // Scale UVs for texture tiling
            glm::vec2 uv(u * texScale.x, t * length * texScale.y * 0.1f);

            // Tangent along circumference
            glm::vec3 tangentDir = glm::normalize(-right * sinA + up * cosA);
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
    glm::vec3 branchDir = glm::normalize(segment.endPos - segment.startPos);

    // Place leaves along the branch
    for (int i = 0; i < params.leavesPerBranch; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(std::max(1, params.leavesPerBranch));

        // Position along branch - use leafStart parameter
        t = params.leafStart + t * (1.0f - params.leafStart);
        glm::vec3 pos = glm::mix(segment.startPos, segment.endPos, t);

        // Random offset from branch axis
        glm::vec3 offset = randomOnSphere();
        offset -= branchDir * glm::dot(offset, branchDir);  // Project to perpendicular plane
        if (glm::length(offset) > 0.001f) {
            offset = glm::normalize(offset);
        }
        float radius = glm::mix(segment.startRadius, segment.endRadius, t);
        pos += offset * (radius + params.leafSize * 0.5f);

        // Leaf normal - angle from branch (degrees)
        float leafAngleRad = glm::radians(params.leafAngle);
        glm::vec3 normal = glm::normalize(
            offset * std::cos(leafAngleRad) +
            branchDir * std::sin(leafAngleRad) +
            glm::vec3(0.0f, 0.2f, 0.0f)
        );

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

    outMesh.setCustomGeometry(vertices, indices);
}

// =============================================================================
// Space Colonisation Algorithm Implementation
// =============================================================================

glm::vec3 TreeGenerator::randomPointInVolume(VolumeShape shape,
                                              float radius,
                                              float height,
                                              const glm::vec3& scale) {
    glm::vec3 point;

    switch (shape) {
        case VolumeShape::Sphere: {
            // Uniform distribution in sphere
            float r = radius * std::cbrt(randomFloat(0.0f, 1.0f));
            float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            float phi = std::acos(randomFloat(-1.0f, 1.0f));
            point = glm::vec3(
                r * std::sin(phi) * std::cos(theta),
                r * std::cos(phi),
                r * std::sin(phi) * std::sin(theta)
            );
            break;
        }
        case VolumeShape::Hemisphere: {
            // Upper hemisphere
            float r = radius * std::cbrt(randomFloat(0.0f, 1.0f));
            float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            float phi = std::acos(randomFloat(0.0f, 1.0f));  // Only upper half
            point = glm::vec3(
                r * std::sin(phi) * std::cos(theta),
                r * std::cos(phi),
                r * std::sin(phi) * std::sin(theta)
            );
            break;
        }
        case VolumeShape::Cone: {
            // Uniform in cone (apex at top)
            float h = randomFloat(0.0f, 1.0f);
            float r = radius * (1.0f - h) * std::sqrt(randomFloat(0.0f, 1.0f));
            float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            point = glm::vec3(
                r * std::cos(theta),
                h * height,
                r * std::sin(theta)
            );
            break;
        }
        case VolumeShape::Cylinder: {
            float r = radius * std::sqrt(randomFloat(0.0f, 1.0f));
            float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            float h = randomFloat(0.0f, height);
            point = glm::vec3(
                r * std::cos(theta),
                h,
                r * std::sin(theta)
            );
            break;
        }
        case VolumeShape::Ellipsoid: {
            // Uniform in ellipsoid
            float r = std::cbrt(randomFloat(0.0f, 1.0f));
            float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            float phi = std::acos(randomFloat(-1.0f, 1.0f));
            point = glm::vec3(
                r * radius * scale.x * std::sin(phi) * std::cos(theta),
                r * radius * scale.y * std::cos(phi),
                r * radius * scale.z * std::sin(phi) * std::sin(theta)
            );
            break;
        }
        case VolumeShape::Box: {
            point = glm::vec3(
                randomFloat(-radius, radius) * scale.x,
                randomFloat(0.0f, height) * scale.y,
                randomFloat(-radius, radius) * scale.z
            );
            break;
        }
    }

    return point;
}

bool TreeGenerator::isPointInVolume(const glm::vec3& point,
                                    const glm::vec3& center,
                                    VolumeShape shape,
                                    float radius,
                                    float height,
                                    const glm::vec3& scale,
                                    float exclusionRadius) {
    glm::vec3 local = point - center;

    // Check exclusion zone first
    if (exclusionRadius > 0.0f && glm::length(local) < exclusionRadius) {
        return false;
    }

    switch (shape) {
        case VolumeShape::Sphere:
            return glm::length(local) <= radius;

        case VolumeShape::Hemisphere:
            return glm::length(local) <= radius && local.y >= 0.0f;

        case VolumeShape::Cone: {
            if (local.y < 0.0f || local.y > height) return false;
            float allowedRadius = radius * (1.0f - local.y / height);
            float distXZ = std::sqrt(local.x * local.x + local.z * local.z);
            return distXZ <= allowedRadius;
        }

        case VolumeShape::Cylinder: {
            if (local.y < 0.0f || local.y > height) return false;
            float distXZ = std::sqrt(local.x * local.x + local.z * local.z);
            return distXZ <= radius;
        }

        case VolumeShape::Ellipsoid: {
            glm::vec3 normalized = local / (radius * scale);
            return glm::dot(normalized, normalized) <= 1.0f;
        }

        case VolumeShape::Box:
            return std::abs(local.x) <= radius * scale.x &&
                   local.y >= 0.0f && local.y <= height * scale.y &&
                   std::abs(local.z) <= radius * scale.z;
    }

    return false;
}

void TreeGenerator::generateAttractionPoints(const SpaceColonisationParams& scParams,
                                             const glm::vec3& center,
                                             bool isRoot,
                                             std::vector<glm::vec3>& outPoints) {
    VolumeShape shape = isRoot ? scParams.rootShape : scParams.crownShape;
    float radius = isRoot ? scParams.rootRadius : scParams.crownRadius;
    float height = isRoot ? scParams.rootDepth : scParams.crownHeight;
    int count = isRoot ? scParams.rootAttractionPointCount : scParams.attractionPointCount;
    float exclusion = isRoot ? 0.0f : scParams.crownExclusionRadius;

    outPoints.reserve(outPoints.size() + count);

    int attempts = 0;
    int maxAttempts = count * 10;

    while (static_cast<int>(outPoints.size()) < count && attempts < maxAttempts) {
        glm::vec3 localPoint = randomPointInVolume(shape, radius, height, scParams.crownScale);

        // For roots, flip Y to go downward
        if (isRoot) {
            localPoint.y = -std::abs(localPoint.y);
        }

        glm::vec3 worldPoint = center + localPoint;

        // Check exclusion zone
        if (exclusion > 0.0f) {
            glm::vec3 fromCenter = worldPoint - center;
            if (glm::length(fromCenter) < exclusion) {
                attempts++;
                continue;
            }
        }

        outPoints.push_back(worldPoint);
        attempts++;
    }
}

bool TreeGenerator::spaceColonisationStep(std::vector<TreeNode>& nodes,
                                          std::vector<glm::vec3>& attractionPoints,
                                          const SpaceColonisationParams& params,
                                          const glm::vec3& tropismDir,
                                          float tropismStrength) {
    if (attractionPoints.empty() || nodes.empty()) {
        return false;
    }

    // For each node, accumulate influence from nearby attraction points
    std::vector<glm::vec3> growthDirections(nodes.size(), glm::vec3(0.0f));
    std::vector<int> influenceCount(nodes.size(), 0);
    std::vector<bool> pointsToRemove(attractionPoints.size(), false);

    // Find closest node for each attraction point
    for (size_t pi = 0; pi < attractionPoints.size(); ++pi) {
        const glm::vec3& point = attractionPoints[pi];

        int closestNode = -1;
        float closestDist = params.attractionDistance;

        for (size_t ni = 0; ni < nodes.size(); ++ni) {
            float dist = glm::distance(nodes[ni].position, point);

            // Check kill distance
            if (dist < params.killDistance) {
                pointsToRemove[pi] = true;
                closestNode = -1;
                break;
            }

            if (dist < closestDist) {
                closestDist = dist;
                closestNode = static_cast<int>(ni);
            }
        }

        if (closestNode >= 0) {
            glm::vec3 dir = glm::normalize(point - nodes[closestNode].position);
            growthDirections[closestNode] += dir;
            influenceCount[closestNode]++;
        }
    }

    // Remove killed points
    std::vector<glm::vec3> remainingPoints;
    for (size_t i = 0; i < attractionPoints.size(); ++i) {
        if (!pointsToRemove[i]) {
            remainingPoints.push_back(attractionPoints[i]);
        }
    }
    attractionPoints = std::move(remainingPoints);

    // Grow new nodes
    bool grewAny = false;
    size_t originalNodeCount = nodes.size();

    for (size_t i = 0; i < originalNodeCount; ++i) {
        if (influenceCount[i] > 0) {
            glm::vec3 avgDir = glm::normalize(growthDirections[i]);

            // Apply tropism
            if (tropismStrength > 0.0f) {
                avgDir = glm::normalize(avgDir + tropismDir * tropismStrength);
            }

            // Create new node
            TreeNode newNode;
            newNode.position = nodes[i].position + avgDir * params.segmentLength;
            newNode.parentIndex = static_cast<int>(i);
            newNode.childCount = 0;
            newNode.thickness = params.minThickness;
            newNode.isTerminal = true;
            newNode.depth = nodes[i].depth + 1;

            // Update parent
            nodes[i].childCount++;
            nodes[i].isTerminal = false;

            nodes.push_back(newNode);
            grewAny = true;
        }
    }

    return grewAny;
}

void TreeGenerator::calculateBranchThickness(std::vector<TreeNode>& nodes,
                                              const SpaceColonisationParams& params) {
    if (nodes.empty()) return;

    // Calculate child count for each node by traversing from leaves to root
    // First, find all terminal nodes and propagate thickness upward

    // Using pipe model (da Vinci's rule):
    // parent_area = sum of children_areas
    // parent_radius^n = sum of children_radius^n

    // Start from terminal nodes with minimum thickness
    for (auto& node : nodes) {
        if (node.isTerminal || node.childCount == 0) {
            node.thickness = params.minThickness;
        }
    }

    // Propagate thickness from leaves to root
    // Process nodes in reverse order (children before parents)
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
        TreeNode& node = nodes[i];

        if (node.parentIndex >= 0 && node.parentIndex < static_cast<int>(nodes.size())) {
            TreeNode& parent = nodes[node.parentIndex];

            // Accumulate thickness using pipe model
            float childPow = std::pow(node.thickness, params.thicknessPower);
            float parentPow = std::pow(parent.thickness, params.thicknessPower);
            parent.thickness = std::pow(parentPow + childPow, 1.0f / params.thicknessPower);
        }
    }

    // Clamp maximum thickness
    for (auto& node : nodes) {
        node.thickness = std::min(node.thickness, params.baseThickness);
    }
}

void TreeGenerator::nodesToSegments(const std::vector<TreeNode>& nodes,
                                    const TreeParameters& params) {
    // Convert tree nodes to branch segments for rendering
    // Find maximum depth for level scaling
    int maxDepth = 0;
    for (const auto& node : nodes) {
        maxDepth = std::max(maxDepth, node.depth);
    }

    for (size_t i = 0; i < nodes.size(); ++i) {
        const TreeNode& node = nodes[i];

        if (node.parentIndex >= 0) {
            const TreeNode& parent = nodes[node.parentIndex];

            BranchSegment segment;
            segment.startPos = parent.position;
            segment.endPos = node.position;
            segment.startRadius = parent.thickness;
            segment.endRadius = node.thickness;

            // Calculate orientation from direction
            glm::vec3 dir = glm::normalize(node.position - parent.position);
            glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(dir, up)) > 0.99f) {
                up = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            glm::vec3 right = glm::normalize(glm::cross(up, dir));
            up = glm::cross(dir, right);
            glm::mat3 rotMat(right, dir, up);
            segment.orientation = glm::quat_cast(rotMat);

            // Use depth for level calculation, scaled to branchLevels
            if (maxDepth > 0) {
                segment.level = (node.depth * params.branchLevels) / maxDepth;
            } else {
                segment.level = 0;
            }
            segment.level = std::max(0, std::min(segment.level, params.branchLevels));

            segment.parentIndex = node.parentIndex;

            segments.push_back(segment);
        }
    }
}

void TreeGenerator::generateSpaceColonisation(const TreeParameters& params) {
    const auto& scParams = params.spaceColonisation;

    std::vector<TreeNode> nodes;
    std::vector<glm::vec3> attractionPoints;

    // Create initial trunk nodes
    glm::vec3 trunkBase(0.0f, 0.0f, 0.0f);
    int trunkSegmentCount = static_cast<int>(scParams.trunkSegments);
    float segmentHeight = scParams.trunkHeight / static_cast<float>(trunkSegmentCount);

    for (int i = 0; i <= trunkSegmentCount; ++i) {
        TreeNode node;
        node.position = trunkBase + glm::vec3(0.0f, i * segmentHeight, 0.0f);
        node.parentIndex = i > 0 ? i - 1 : -1;
        node.childCount = (i < trunkSegmentCount) ? 1 : 0;
        node.thickness = scParams.baseThickness;
        node.isTerminal = (i == trunkSegmentCount);
        node.depth = 0;  // Trunk is level 0
        nodes.push_back(node);
    }

    // Crown center is at top of trunk plus offset
    glm::vec3 crownCenter = glm::vec3(0.0f, scParams.trunkHeight, 0.0f) + scParams.crownOffset;

    // Generate attraction points for crown
    generateAttractionPoints(scParams, crownCenter, false, attractionPoints);

    SDL_Log("Space colonisation: Generated %zu attraction points for crown",
            attractionPoints.size());

    // Generate attraction points for roots if enabled
    std::vector<glm::vec3> rootAttractionPoints;
    if (scParams.generateRoots) {
        generateAttractionPoints(scParams, trunkBase, true, rootAttractionPoints);
        SDL_Log("Space colonisation: Generated %zu attraction points for roots",
                rootAttractionPoints.size());
    }

    // Run space colonisation algorithm for crown
    int iterations = 0;
    while (iterations < scParams.maxIterations && !attractionPoints.empty()) {
        bool grew = spaceColonisationStep(nodes, attractionPoints,
                                          scParams,
                                          scParams.tropismDirection,
                                          scParams.tropismStrength);
        if (!grew) break;
        iterations++;
    }

    SDL_Log("Space colonisation: Crown completed in %d iterations, %zu nodes",
            iterations, nodes.size());

    // Run space colonisation for roots
    if (scParams.generateRoots && !rootAttractionPoints.empty()) {
        // Add root base node
        TreeNode rootBase;
        rootBase.position = trunkBase;
        rootBase.parentIndex = 0;  // Connect to trunk base
        rootBase.childCount = 0;
        rootBase.thickness = scParams.baseThickness * 0.8f;
        rootBase.isTerminal = true;
        rootBase.depth = 0;  // Root base is level 0
        int rootBaseIdx = static_cast<int>(nodes.size());
        nodes.push_back(rootBase);

        // Create separate list for root nodes
        std::vector<TreeNode> rootNodes;
        rootNodes.push_back(rootBase);

        int rootIterations = 0;
        while (rootIterations < scParams.maxIterations / 2 && !rootAttractionPoints.empty()) {
            bool grew = spaceColonisationStep(rootNodes, rootAttractionPoints,
                                              scParams,
                                              glm::vec3(0.0f, -1.0f, 0.0f),
                                              scParams.rootTropismStrength);
            if (!grew) break;
            rootIterations++;
        }

        // Merge root nodes into main node list (adjusting parent indices)
        for (size_t i = 1; i < rootNodes.size(); ++i) {
            TreeNode node = rootNodes[i];
            node.parentIndex += rootBaseIdx;
            nodes.push_back(node);
        }

        SDL_Log("Space colonisation: Roots completed in %d iterations, %zu additional nodes",
                rootIterations, rootNodes.size() - 1);
    }

    // Calculate branch thicknesses
    calculateBranchThickness(nodes, scParams);

    // Build child index lists
    buildChildIndices(nodes);

    // Generate curved geometry directly (bypasses nodesToSegments)
    generateCurvedBranchGeometry(nodes, params);

    SDL_Log("Space colonisation: Total %zu vertices, %zu indices generated",
            branchVertices.size(), branchIndices.size());
}

// =============================================================================
// Curved Geometry Generation
// =============================================================================

void TreeGenerator::buildChildIndices(std::vector<TreeNode>& nodes) {
    // Clear existing child indices
    for (auto& node : nodes) {
        node.childIndices.clear();
    }

    // Build child index lists
    for (size_t i = 0; i < nodes.size(); ++i) {
        int parentIdx = nodes[i].parentIndex;
        if (parentIdx >= 0 && parentIdx < static_cast<int>(nodes.size())) {
            nodes[parentIdx].childIndices.push_back(static_cast<int>(i));
        }
    }
}

void TreeGenerator::findBranchChains(const std::vector<TreeNode>& nodes,
                                     std::vector<std::vector<int>>& chains) {
    chains.clear();

    // Find all branch starting points (nodes with != 1 child or root)
    std::vector<bool> visited(nodes.size(), false);

    for (size_t startIdx = 0; startIdx < nodes.size(); ++startIdx) {
        const TreeNode& startNode = nodes[startIdx];

        // Start a chain at branching points or unvisited nodes
        if (visited[startIdx]) continue;

        // Only start chains at nodes that are branching points or have no parent
        bool isBranchPoint = startNode.parentIndex < 0 ||
                            nodes[startNode.parentIndex].childIndices.size() > 1;

        if (!isBranchPoint && startNode.parentIndex >= 0) continue;

        // Follow each child to build chains
        for (int childIdx : startNode.childIndices) {
            std::vector<int> chain;
            chain.push_back(static_cast<int>(startIdx));

            int current = childIdx;
            while (current >= 0 && !visited[current]) {
                chain.push_back(current);
                visited[current] = true;

                const TreeNode& currentNode = nodes[current];
                if (currentNode.childIndices.size() == 1) {
                    current = currentNode.childIndices[0];
                } else {
                    break;  // Branching point or terminal
                }
            }

            if (chain.size() >= 2) {
                chains.push_back(chain);
            }
        }
    }
}

glm::vec3 TreeGenerator::catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                                    const glm::vec3& p2, const glm::vec3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;

    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void TreeGenerator::generateCurvedTube(const std::vector<glm::vec3>& points,
                                       const std::vector<float>& radii,
                                       int radialSegments,
                                       int level,
                                       const TreeParameters& params) {
    if (points.size() < 2) return;

    uint32_t baseVertexIndex = static_cast<uint32_t>(branchVertices.size());
    int numPoints = static_cast<int>(points.size());

    // Generate vertices along the tube
    for (int ring = 0; ring < numPoints; ++ring) {
        glm::vec3 center = points[ring];
        float radius = radii[ring];

        // Calculate tangent direction
        glm::vec3 tangent;
        if (ring == 0) {
            tangent = glm::normalize(points[1] - points[0]);
        } else if (ring == numPoints - 1) {
            tangent = glm::normalize(points[numPoints - 1] - points[numPoints - 2]);
        } else {
            tangent = glm::normalize(points[ring + 1] - points[ring - 1]);
        }

        // Build orthonormal basis
        glm::vec3 up = glm::abs(tangent.y) > 0.99f
            ? glm::vec3(1.0f, 0.0f, 0.0f)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(up, tangent));
        up = glm::cross(tangent, right);

        float t = static_cast<float>(ring) / static_cast<float>(numPoints - 1);

        for (int i = 0; i <= radialSegments; ++i) {
            float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(radialSegments);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            glm::vec3 offset = (right * cosA + up * sinA) * radius;
            glm::vec3 pos = center + offset;
            glm::vec3 normal = glm::normalize(offset);

            glm::vec2 uv(static_cast<float>(i) / static_cast<float>(radialSegments), t);
            glm::vec3 tangentDir = glm::normalize(-right * sinA + up * cosA);
            glm::vec4 tangent4(tangentDir, 1.0f);

            // Color based on level
            float levelColor = 1.0f - static_cast<float>(level) / static_cast<float>(params.branchLevels + 1);
            glm::vec4 color(0.4f * levelColor + 0.2f, 0.25f * levelColor + 0.1f, 0.1f, 1.0f);

            branchVertices.push_back({pos, normal, uv, tangent4, color});
        }
    }

    // Generate indices
    for (int ring = 0; ring < numPoints - 1; ++ring) {
        for (int i = 0; i < radialSegments; ++i) {
            uint32_t current = baseVertexIndex + ring * (radialSegments + 1) + i;
            uint32_t next = current + 1;
            uint32_t below = current + (radialSegments + 1);
            uint32_t belowNext = below + 1;

            branchIndices.push_back(current);
            branchIndices.push_back(next);
            branchIndices.push_back(below);

            branchIndices.push_back(next);
            branchIndices.push_back(belowNext);
            branchIndices.push_back(below);
        }
    }
}

void TreeGenerator::generateCurvedBranchGeometry(const std::vector<TreeNode>& nodes,
                                                 const TreeParameters& params) {
    if (nodes.empty()) return;

    const auto& scParams = params.spaceColonisation;
    int subdivisions = scParams.curveSubdivisions;
    int radialSegments = scParams.radialSegments;

    // Find maximum depth for level scaling
    int maxDepth = 0;
    for (const auto& node : nodes) {
        maxDepth = std::max(maxDepth, node.depth);
    }

    // Process each node that has a parent
    for (size_t i = 0; i < nodes.size(); ++i) {
        const TreeNode& node = nodes[i];
        if (node.parentIndex < 0) continue;

        const TreeNode& parent = nodes[node.parentIndex];

        // Get grandparent and child for spline control points
        glm::vec3 p0, p1, p2, p3;
        float r0, r1, r2, r3;

        p1 = parent.position;
        p2 = node.position;
        r1 = parent.thickness;
        r2 = node.thickness;

        // Grandparent (or extrapolate)
        if (parent.parentIndex >= 0) {
            p0 = nodes[parent.parentIndex].position;
            r0 = nodes[parent.parentIndex].thickness;
        } else {
            p0 = p1 - (p2 - p1);
            r0 = r1;
        }

        // Child (or extrapolate)
        if (!node.childIndices.empty()) {
            p3 = nodes[node.childIndices[0]].position;
            r3 = nodes[node.childIndices[0]].thickness;
        } else {
            p3 = p2 + (p2 - p1);
            r3 = r2;
        }

        // Generate subdivided points along spline
        std::vector<glm::vec3> curvePoints;
        std::vector<float> curveRadii;

        for (int s = 0; s <= subdivisions; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(subdivisions);

            glm::vec3 pos = catmullRom(p0, p1, p2, p3, t);
            float radius = glm::mix(r1, r2, t);

            curvePoints.push_back(pos);
            curveRadii.push_back(radius);
        }

        // Calculate level for this segment
        int level = 0;
        if (maxDepth > 0) {
            level = (node.depth * params.branchLevels) / maxDepth;
        }
        level = std::max(0, std::min(level, params.branchLevels));

        // Generate tube geometry
        generateCurvedTube(curvePoints, curveRadii, radialSegments, level, params);
    }
}
