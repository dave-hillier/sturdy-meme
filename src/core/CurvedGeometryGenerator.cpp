#include "CurvedGeometryGenerator.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

glm::vec3 CurvedGeometryGenerator::catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                                               const glm::vec3& p2, const glm::vec3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;

    return 0.5f * ((2.0f * p1) +
                   (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void CurvedGeometryGenerator::findBranchChains(const std::vector<TreeNode>& nodes,
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

void CurvedGeometryGenerator::generateCurvedTube(const std::vector<glm::vec3>& points,
                                                  const std::vector<float>& radii,
                                                  int radialSegments,
                                                  int level,
                                                  const TreeParameters& params,
                                                  std::vector<Vertex>& outVertices,
                                                  std::vector<uint32_t>& outIndices) {
    if (points.size() < 2) return;

    uint32_t baseVertexIndex = static_cast<uint32_t>(outVertices.size());
    int numPoints = static_cast<int>(points.size());

    // Generate vertices along the tube
    for (int ring = 0; ring < numPoints; ++ring) {
        glm::vec3 center = points[ring];
        float radius = radii[ring];

        // Calculate tangent direction
        glm::vec3 tangent;
        glm::vec3 tangentDiff;
        if (ring == 0) {
            tangentDiff = points[1] - points[0];
        } else if (ring == numPoints - 1) {
            tangentDiff = points[numPoints - 1] - points[numPoints - 2];
        } else {
            tangentDiff = points[ring + 1] - points[ring - 1];
        }

        // Guard against zero-length tangent (degenerate case)
        float tangentLen = glm::length(tangentDiff);
        if (tangentLen < 0.0001f) {
            tangent = glm::vec3(0.0f, 1.0f, 0.0f);  // Default to up direction
        } else {
            tangent = tangentDiff / tangentLen;
        }

        // Build orthonormal basis
        glm::vec3 up = std::abs(tangent.y) > 0.99f
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

            // UV coordinates with texture scaling (matching generateBranchGeometry)
            float u = static_cast<float>(i) / static_cast<float>(radialSegments);
            glm::vec2 uv(u * params.barkTextureScale.x, t * params.barkTextureScale.y);
            glm::vec3 tangentDir = glm::normalize(-right * sinA + up * cosA);
            glm::vec4 tangent4(tangentDir, 1.0f);

            // Color: apply bark tint (shader will multiply with texture)
            glm::vec4 color(params.barkTint, 1.0f);

            outVertices.push_back({pos, normal, uv, tangent4, color});
        }
    }

    // Generate indices
    for (int ring = 0; ring < numPoints - 1; ++ring) {
        for (int i = 0; i < radialSegments; ++i) {
            uint32_t current = baseVertexIndex + ring * (radialSegments + 1) + i;
            uint32_t next = current + 1;
            uint32_t below = current + (radialSegments + 1);
            uint32_t belowNext = below + 1;

            outIndices.push_back(current);
            outIndices.push_back(next);
            outIndices.push_back(below);

            outIndices.push_back(next);
            outIndices.push_back(belowNext);
            outIndices.push_back(below);
        }
    }
}

void CurvedGeometryGenerator::generateCurvedBranchGeometry(const std::vector<TreeNode>& nodes,
                                                            const TreeParameters& params,
                                                            std::vector<Vertex>& outVertices,
                                                            std::vector<uint32_t>& outIndices) {
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

        // Skip degenerate segments where parent and child are at same position
        // This can happen with root base nodes that share position with trunk base
        float segmentLength = glm::distance(parent.position, node.position);
        if (segmentLength < 0.0001f) continue;

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
        generateCurvedTube(curvePoints, curveRadii, radialSegments, level, params,
                          outVertices, outIndices);
    }
}
