#include "TubeBranchGeometry.h"
#include <SDL3/SDL.h>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void TubeBranchGeometry::generate(const TreeStructure& tree,
                                  const TreeParameters& params,
                                  std::vector<Vertex>& outVertices,
                                  std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    // Visit all branches and generate geometry
    tree.forEachBranch([this, &params, &outVertices, &outIndices](const Branch& branch) {
        generateBranchGeometry(branch, params, outVertices, outIndices);
    });

    SDL_Log("TubeBranchGeometry: Generated %zu vertices, %zu indices",
            outVertices.size(), outIndices.size());
}

void TubeBranchGeometry::generateBranchGeometry(const Branch& branch,
                                                 const TreeParameters& params,
                                                 std::vector<Vertex>& outVertices,
                                                 std::vector<uint32_t>& outIndices) {
    // Skip degenerate segments
    float length = glm::length(branch.getEndPosition() - branch.getStartPosition());
    if (length < 0.0001f) return;

    if (branch.getStartRadius() < 0.0001f && branch.getEndRadius() < 0.0001f) return;

    const auto& props = branch.getProperties();
    int radialSegments = props.radialSegments;
    int rings = props.lengthSegments;

    // Direction of branch
    glm::vec3 direction = (branch.getEndPosition() - branch.getStartPosition()) / length;

    // Build coordinate frame perpendicular to branch direction
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
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Degenerate coordinate frame for direction (%.3f,%.3f,%.3f)",
            direction.x, direction.y, direction.z);
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        right = right / rightLen;
    }
    up = glm::cross(direction, right);

    uint32_t baseVertexIndex = static_cast<uint32_t>(outVertices.size());

    // Texture scale
    glm::vec2 texScale = params.barkTextureScale;

    // Generate vertices for each ring
    for (int ring = 0; ring <= rings; ++ring) {
        float t = static_cast<float>(ring) / static_cast<float>(rings);
        glm::vec3 center = branch.getPositionAt(t);
        float radius = branch.getRadiusAt(t);

        for (int i = 0; i <= radialSegments; ++i) {
            float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(radialSegments);
            float cosA = std::cos(angle);
            float sinA = std::sin(angle);

            // Position on ring
            glm::vec3 radialDir = right * cosA + up * sinA;
            glm::vec3 offset = radialDir * radius;
            glm::vec3 pos = center + offset;

            // Normal points outward
            glm::vec3 normal = radialDir;

            // UV coordinates with texture scaling
            float u = static_cast<float>(i) / static_cast<float>(radialSegments);
            glm::vec2 uv(u * texScale.x, t * length * texScale.y * 0.1f);

            // Tangent along circumference
            glm::vec3 tangentDir = -right * sinA + up * cosA;
            glm::vec4 tangent(tangentDir, 1.0f);

            // Color: apply bark tint
            glm::vec4 color(params.barkTint, 1.0f);

            outVertices.push_back({pos, normal, uv, tangent, color});
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
            outIndices.push_back(current);
            outIndices.push_back(next);
            outIndices.push_back(below);

            outIndices.push_back(next);
            outIndices.push_back(belowNext);
            outIndices.push_back(below);
        }
    }
}
