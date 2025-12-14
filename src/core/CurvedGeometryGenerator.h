#pragma once

#include "TreeParameters.h"
#include "TreeGeometry.h"
#include "Mesh.h"
#include <vector>

// Generates smooth curved geometry for tree branches
class CurvedGeometryGenerator {
public:
    // Generate curved geometry from tree nodes
    void generateCurvedBranchGeometry(const std::vector<TreeNode>& nodes,
                                      const TreeParameters& params,
                                      std::vector<Vertex>& outVertices,
                                      std::vector<uint32_t>& outIndices);

    // Find branch chains (sequences of single-child nodes)
    static void findBranchChains(const std::vector<TreeNode>& nodes,
                                 std::vector<std::vector<int>>& chains);

    // Catmull-Rom spline interpolation
    static glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1,
                                const glm::vec3& p2, const glm::vec3& p3, float t);

private:
    // Generate a curved tube along a path of points
    void generateCurvedTube(const std::vector<glm::vec3>& points,
                            const std::vector<float>& radii,
                            int radialSegments,
                            int level,
                            const TreeParameters& params,
                            std::vector<Vertex>& outVertices,
                            std::vector<uint32_t>& outIndices);
};
