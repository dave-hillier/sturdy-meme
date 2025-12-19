#include "BillboardLeafGenerator.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <functional>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Hash function for generating per-leaf phase offsets
static float hashLeafPosition(const glm::vec3& pos) {
    uint32_t h = 0;
    h ^= std::hash<float>{}(pos.x);
    h ^= std::hash<float>{}(pos.y) * 31;
    h ^= std::hash<float>{}(pos.z) * 127;
    return static_cast<float>(h % 10000) / 10000.0f * 2.0f * static_cast<float>(M_PI);
}

void BillboardLeafGenerator::generateLeaves(const TreeStructure& tree,
                                            const TreeParameters& params,
                                            std::mt19937& rng,
                                            std::vector<LeafInstance>& outLeaves) {
    outLeaves.clear();

    if (!params.generateLeaves) return;

    // Visit all branches and generate leaves on appropriate ones
    tree.forEachBranch([this, &params, &rng, &outLeaves](const Branch& branch) {
        if (branch.getLevel() >= params.leafStartLevel) {
            generateLeavesForBranch(branch, params, rng, outLeaves);
        }
    });

    SDL_Log("BillboardLeafGenerator: Generated %zu leaves", outLeaves.size());
}

void BillboardLeafGenerator::generateLeavesForBranch(const Branch& branch,
                                                      const TreeParameters& params,
                                                      std::mt19937& rng,
                                                      std::vector<LeafInstance>& outLeaves) {
    // Calculate branch direction safely
    glm::vec3 branchVec = branch.getEndPosition() - branch.getStartPosition();
    float branchLen = glm::length(branchVec);
    if (branchLen < 0.0001f) return;
    glm::vec3 branchDir = branchVec / branchLen;

    for (int i = 0; i < params.leavesPerBranch; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(std::max(1, params.leavesPerBranch));

        // Position along branch using leafStart parameter
        t = params.leafStart + t * (1.0f - params.leafStart);
        glm::vec3 pos = branch.getPositionAt(t);

        // Random offset from branch axis
        glm::vec3 offset = randomOnSphere(rng);
        offset -= branchDir * glm::dot(offset, branchDir);
        float offsetLen = glm::length(offset);
        if (offsetLen > 0.001f) {
            offset = offset / offsetLen;
        } else {
            offset = glm::abs(branchDir.y) > 0.99f
                ? glm::vec3(1.0f, 0.0f, 0.0f)
                : glm::normalize(glm::cross(branchDir, glm::vec3(0.0f, 1.0f, 0.0f)));
        }

        float radius = branch.getRadiusAt(t);
        pos += offset * (radius + params.leafSize * 0.5f);

        // Leaf normal
        float leafAngleRad = glm::radians(params.leafAngle);
        glm::vec3 normalVec = offset * std::cos(leafAngleRad) +
                              branchDir * std::sin(leafAngleRad) +
                              glm::vec3(0.0f, 0.2f, 0.0f);
        float normalLen = glm::length(normalVec);
        glm::vec3 normal = normalLen > 0.0001f ? normalVec / normalLen : glm::vec3(0.0f, 1.0f, 0.0f);

        // Size variance
        float sizeVariance = 1.0f - params.leafSizeVariance +
                            randomFloat(rng, 0.0f, 2.0f * params.leafSizeVariance);

        LeafInstance leaf;
        leaf.position = pos;
        leaf.normal = normal;
        leaf.size = params.leafSize * sizeVariance;
        leaf.rotation = randomFloat(rng, 0.0f, 2.0f * static_cast<float>(M_PI));

        outLeaves.push_back(leaf);
    }
}

void BillboardLeafGenerator::buildLeafMesh(const std::vector<LeafInstance>& leaves,
                                            const TreeParameters& params,
                                            std::vector<Vertex>& outVertices,
                                            std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    if (leaves.empty()) return;

    bool doubleBillboard = (params.leafBillboard == BillboardMode::Double);

    for (size_t i = 0; i < leaves.size(); ++i) {
        const auto& leaf = leaves[i];

        // Skip invalid leaves
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
        glm::vec4 color(params.leafTint, 1.0f);

        glm::vec2 uvs[4] = {
            {0.0f, 1.0f},
            {1.0f, 1.0f},
            {1.0f, 0.0f},
            {0.0f, 0.0f}
        };

        // First quad
        {
            uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());

            glm::vec3 corners[4] = {
                leaf.position + (-rotRight - rotUp) * halfSize,
                leaf.position + ( rotRight - rotUp) * halfSize,
                leaf.position + ( rotRight + rotUp) * halfSize,
                leaf.position + (-rotRight + rotUp) * halfSize
            };

            for (int j = 0; j < 4; ++j) {
                outVertices.push_back({
                    corners[j],
                    leaf.normal,
                    uvs[j],
                    glm::vec4(rotRight, 1.0f),
                    color
                });
            }

            outIndices.push_back(baseIdx);
            outIndices.push_back(baseIdx + 1);
            outIndices.push_back(baseIdx + 2);
            outIndices.push_back(baseIdx);
            outIndices.push_back(baseIdx + 2);
            outIndices.push_back(baseIdx + 3);
        }

        // Second quad for double billboard
        if (doubleBillboard) {
            uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());

            glm::vec3 rotRight2 = leaf.normal;
            glm::vec3 normal2 = -rotRight;

            glm::vec3 corners[4] = {
                leaf.position + (-rotRight2 - rotUp) * halfSize,
                leaf.position + ( rotRight2 - rotUp) * halfSize,
                leaf.position + ( rotRight2 + rotUp) * halfSize,
                leaf.position + (-rotRight2 + rotUp) * halfSize
            };

            for (int j = 0; j < 4; ++j) {
                outVertices.push_back({
                    corners[j],
                    normal2,
                    uvs[j],
                    glm::vec4(rotRight2, 1.0f),
                    color
                });
            }

            outIndices.push_back(baseIdx);
            outIndices.push_back(baseIdx + 1);
            outIndices.push_back(baseIdx + 2);
            outIndices.push_back(baseIdx);
            outIndices.push_back(baseIdx + 2);
            outIndices.push_back(baseIdx + 3);
        }
    }

    SDL_Log("BillboardLeafGenerator: Built mesh with %zu vertices, %zu indices",
            outVertices.size(), outIndices.size());
}

void BillboardLeafGenerator::buildLeafMeshWithWind(const std::vector<LeafInstance>& leaves,
                                                    const TreeParameters& params,
                                                    std::vector<TreeVertex>& outVertices,
                                                    std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    if (leaves.empty()) return;

    bool doubleBillboard = (params.leafBillboard == BillboardMode::Double);

    for (size_t i = 0; i < leaves.size(); ++i) {
        const auto& leaf = leaves[i];

        // Skip invalid leaves
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
        glm::vec4 color(params.leafTint, 1.0f);

        glm::vec2 uvs[4] = {
            {0.0f, 1.0f},
            {1.0f, 1.0f},
            {1.0f, 0.0f},
            {0.0f, 0.0f}
        };

        // Wind parameters for leaves:
        // - branchOrigin: leaf center position (attachment point for wind rotation)
        // - branchLevel: high value (3.0) to indicate leaf-level motion
        // - phase: per-leaf phase offset for varied flutter
        // - flexibility: 1.0 (fully flexible)
        // - length: leaf size (for scaling motion)
        glm::vec3 branchOrigin = leaf.position;
        float leafPhase = hashLeafPosition(leaf.position);
        glm::vec4 windParams(3.0f, leafPhase, 1.0f, leaf.size);

        // First quad
        {
            uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());

            glm::vec3 corners[4] = {
                leaf.position + (-rotRight - rotUp) * halfSize,
                leaf.position + ( rotRight - rotUp) * halfSize,
                leaf.position + ( rotRight + rotUp) * halfSize,
                leaf.position + (-rotRight + rotUp) * halfSize
            };

            for (int j = 0; j < 4; ++j) {
                TreeVertex vert;
                vert.position = corners[j];
                vert.normal = leaf.normal;
                vert.texCoord = uvs[j];
                vert.tangent = glm::vec4(rotRight, 1.0f);
                vert.color = color;
                vert.branchOrigin = branchOrigin;
                vert.windParams = windParams;
                outVertices.push_back(vert);
            }

            outIndices.push_back(baseIdx);
            outIndices.push_back(baseIdx + 1);
            outIndices.push_back(baseIdx + 2);
            outIndices.push_back(baseIdx);
            outIndices.push_back(baseIdx + 2);
            outIndices.push_back(baseIdx + 3);
        }

        // Second quad for double billboard
        if (doubleBillboard) {
            uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());

            glm::vec3 rotRight2 = leaf.normal;
            glm::vec3 normal2 = -rotRight;

            glm::vec3 corners[4] = {
                leaf.position + (-rotRight2 - rotUp) * halfSize,
                leaf.position + ( rotRight2 - rotUp) * halfSize,
                leaf.position + ( rotRight2 + rotUp) * halfSize,
                leaf.position + (-rotRight2 + rotUp) * halfSize
            };

            for (int j = 0; j < 4; ++j) {
                TreeVertex vert;
                vert.position = corners[j];
                vert.normal = normal2;
                vert.texCoord = uvs[j];
                vert.tangent = glm::vec4(rotRight2, 1.0f);
                vert.color = color;
                vert.branchOrigin = branchOrigin;
                vert.windParams = windParams;
                outVertices.push_back(vert);
            }

            outIndices.push_back(baseIdx);
            outIndices.push_back(baseIdx + 1);
            outIndices.push_back(baseIdx + 2);
            outIndices.push_back(baseIdx);
            outIndices.push_back(baseIdx + 2);
            outIndices.push_back(baseIdx + 3);
        }
    }

    SDL_Log("BillboardLeafGenerator: Built wind mesh with %zu vertices, %zu indices",
            outVertices.size(), outIndices.size());
}

float BillboardLeafGenerator::randomFloat(std::mt19937& rng, float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng);
}

glm::vec3 BillboardLeafGenerator::randomOnSphere(std::mt19937& rng) {
    float theta = randomFloat(rng, 0.0f, 2.0f * static_cast<float>(M_PI));
    float phi = std::acos(randomFloat(rng, -1.0f, 1.0f));

    float x = std::sin(phi) * std::cos(theta);
    float y = std::sin(phi) * std::sin(theta);
    float z = std::cos(phi);

    return glm::vec3(x, y, z);
}
