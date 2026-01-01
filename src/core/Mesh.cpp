#include "Mesh.h"
#include "VmaResources.h"
#include "CommandBufferUtils.h"
#include "VulkanResourceFactory.h"
#include <vulkan/vulkan.hpp>
#include <cstring>
#include <stdexcept>
#include <cmath>
#include <map>
#include <unordered_map>
#include <functional>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
    // Simple hash-based 3D noise for procedural rock generation
    inline float hash1(uint32_t n) {
        n = (n << 13U) ^ n;
        n = n * (n * n * 15731U + 789221U) + 1376312589U;
        return float(n & 0x7fffffffU) / float(0x7fffffff);
    }

    inline float hash3to1(float x, float y, float z, uint32_t seed) {
        uint32_t ix = *reinterpret_cast<uint32_t*>(&x);
        uint32_t iy = *reinterpret_cast<uint32_t*>(&y);
        uint32_t iz = *reinterpret_cast<uint32_t*>(&z);
        return hash1(ix ^ (iy * 1597334673U) ^ (iz * 3812015801U) ^ seed);
    }

    // Gradient noise for smooth displacement
    float gradientNoise3D(float x, float y, float z, uint32_t seed) {
        int ix = static_cast<int>(std::floor(x));
        int iy = static_cast<int>(std::floor(y));
        int iz = static_cast<int>(std::floor(z));

        float fx = x - ix;
        float fy = y - iy;
        float fz = z - iz;

        // Smoothstep interpolation
        auto smoothstep = [](float t) { return t * t * (3.0f - 2.0f * t); };
        float sx = smoothstep(fx);
        float sy = smoothstep(fy);
        float sz = smoothstep(fz);

        // Hash at corners
        auto cornerHash = [seed](int cx, int cy, int cz) {
            uint32_t n = cx + cy * 57 + cz * 113 + seed;
            return hash1(n) * 2.0f - 1.0f;
        };

        // Trilinear interpolation
        float n000 = cornerHash(ix, iy, iz);
        float n100 = cornerHash(ix + 1, iy, iz);
        float n010 = cornerHash(ix, iy + 1, iz);
        float n110 = cornerHash(ix + 1, iy + 1, iz);
        float n001 = cornerHash(ix, iy, iz + 1);
        float n101 = cornerHash(ix + 1, iy, iz + 1);
        float n011 = cornerHash(ix, iy + 1, iz + 1);
        float n111 = cornerHash(ix + 1, iy + 1, iz + 1);

        float nx00 = n000 + sx * (n100 - n000);
        float nx10 = n010 + sx * (n110 - n010);
        float nx01 = n001 + sx * (n101 - n001);
        float nx11 = n011 + sx * (n111 - n011);

        float nxy0 = nx00 + sy * (nx10 - nx00);
        float nxy1 = nx01 + sy * (nx11 - nx01);

        return nxy0 + sz * (nxy1 - nxy0);
    }

    // Fractal Brownian Motion for natural rock displacement
    float fbm3D(float x, float y, float z, int octaves, float lacunarity, float persistence, uint32_t seed) {
        float value = 0.0f;
        float amplitude = 1.0f;
        float frequency = 1.0f;
        float maxValue = 0.0f;

        for (int i = 0; i < octaves; ++i) {
            value += amplitude * gradientNoise3D(x * frequency, y * frequency, z * frequency, seed + i * 1000);
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= lacunarity;
        }

        return value / maxValue;
    }

    // Voronoi noise for angular rock features
    float voronoi3D(float x, float y, float z, uint32_t seed) {
        int ix = static_cast<int>(std::floor(x));
        int iy = static_cast<int>(std::floor(y));
        int iz = static_cast<int>(std::floor(z));

        float minDist = 10.0f;

        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    int cx = ix + dx;
                    int cy = iy + dy;
                    int cz = iz + dz;

                    // Random point in cell
                    float px = cx + hash1(cx + cy * 57 + cz * 113 + seed);
                    float py = cy + hash1(cx * 31 + cy * 17 + cz * 89 + seed + 1000);
                    float pz = cz + hash1(cx * 73 + cy * 23 + cz * 47 + seed + 2000);

                    float dist = (x - px) * (x - px) + (y - py) * (y - py) + (z - pz) * (z - pz);
                    minDist = std::min(minDist, dist);
                }
            }
        }

        return std::sqrt(minDist);
    }

    // Edge pair for icosphere subdivision
    struct EdgeKey {
        uint32_t v0, v1;
        bool operator==(const EdgeKey& other) const {
            return v0 == other.v0 && v1 == other.v1;
        }
    };

    struct EdgeKeyHash {
        size_t operator()(const EdgeKey& k) const {
            return std::hash<uint64_t>()(static_cast<uint64_t>(k.v0) << 32 | k.v1);
        }
    };
}

void Mesh::calculateBounds() {
    bounds = AABB();  // Reset to default
    for (const auto& vertex : vertices) {
        bounds.expand(vertex.position);
    }
}

void Mesh::createPlane(float width, float depth) {
    float hw = width * 0.5f;
    float hd = depth * 0.5f;

    // For a Y-up plane, tangent points along +X (U direction), bitangent along -Z (V direction)
    glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    vertices = {
        {{-hw, 0.0f,  hd}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}, tangent},
        {{ hw, 0.0f,  hd}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}, tangent},
        {{ hw, 0.0f, -hd}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, tangent},
        {{-hw, 0.0f, -hd}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, tangent},
    };

    indices = {0, 1, 2, 2, 3, 0};
    calculateBounds();
}

void Mesh::createDisc(float radius, int segments, float uvScale) {
    vertices.clear();
    indices.clear();

    // For a Y-up disc, tangent points along +X
    glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    // Center vertex
    vertices.push_back({{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {uvScale * 0.5f, uvScale * 0.5f}, tangent});

    // Edge vertices
    for (int i = 0; i <= segments; ++i) {
        float angle = (float)i / (float)segments * 2.0f * (float)M_PI;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);

        // UV coordinates scaled for tiling - map position to UV space
        float u = (x / radius + 1.0f) * 0.5f * uvScale;
        float v = (z / radius + 1.0f) * 0.5f * uvScale;

        vertices.push_back({{x, 0.0f, z}, {0.0f, 1.0f, 0.0f}, {u, v}, tangent});
    }

    // Create triangles from center to edge (clockwise winding when viewed from above)
    for (int i = 1; i <= segments; ++i) {
        indices.push_back(0);           // Center
        indices.push_back(i + 1);       // Next edge vertex
        indices.push_back(i);           // Current edge vertex
    }
    calculateBounds();
}

void Mesh::createSphere(float radius, int stacks, int slices) {
    vertices.clear();
    indices.clear();

    // Generate vertices
    for (int i = 0; i <= stacks; ++i) {
        float phi = (float)M_PI * (float)i / (float)stacks;
        float y = radius * std::cos(phi);
        float ringRadius = radius * std::sin(phi);

        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * (float)M_PI * (float)j / (float)slices;
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(pos);
            glm::vec2 uv((float)j / (float)slices, (float)i / (float)stacks);

            // Tangent is perpendicular to the normal in the theta direction
            // For spherical coordinates, tangent = d(pos)/d(theta) normalized
            glm::vec3 tangentDir(-std::sin(theta), 0.0f, std::cos(theta));
            glm::vec4 tangent(glm::normalize(tangentDir), 1.0f);

            vertices.push_back({pos, normal, uv, tangent});
        }
    }

    // Generate indices (counter-clockwise winding for front faces)
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            int first = i * (slices + 1) + j;
            int second = first + slices + 1;

            // First triangle (reversed winding)
            indices.push_back(first);
            indices.push_back(first + 1);
            indices.push_back(second);

            // Second triangle (reversed winding)
            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second + 1);
        }
    }
    calculateBounds();
}

void Mesh::createCapsule(float radius, float height, int stacks, int slices) {
    vertices.clear();
    indices.clear();

    // A capsule is a cylinder with two hemisphere caps
    // Height is the total height including caps
    // The cylindrical part height is: height - 2*radius
    float cylinderHeight = height - 2.0f * radius;
    if (cylinderHeight < 0.0f) cylinderHeight = 0.0f;

    int halfStacks = stacks / 2;

    // Generate top hemisphere (from top pole down to equator)
    for (int i = 0; i <= halfStacks; ++i) {
        float phi = (float)M_PI * 0.5f * (1.0f - (float)i / (float)halfStacks);  // PI/2 to 0
        float y = radius * std::sin(phi) + cylinderHeight * 0.5f;
        float ringRadius = radius * std::cos(phi);

        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * (float)M_PI * (float)j / (float)slices;
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            glm::vec3 pos(x, y, z);
            // Normal for hemisphere points outward from sphere center (offset for top hemisphere)
            glm::vec3 sphereCenter(0.0f, cylinderHeight * 0.5f, 0.0f);
            glm::vec3 normal = glm::normalize(pos - sphereCenter);
            glm::vec2 uv((float)j / (float)slices, (float)i / (float)(stacks + 1));

            glm::vec3 tangentDir(-std::sin(theta), 0.0f, std::cos(theta));
            glm::vec4 tangent(glm::normalize(tangentDir), 1.0f);

            vertices.push_back({pos, normal, uv, tangent});
        }
    }

    // Generate cylinder body
    int cylinderRings = stacks / 2;
    for (int i = 0; i <= cylinderRings; ++i) {
        float t = (float)i / (float)cylinderRings;
        float y = cylinderHeight * 0.5f - t * cylinderHeight;

        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * (float)M_PI * (float)j / (float)slices;
            float x = radius * std::cos(theta);
            float z = radius * std::sin(theta);

            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
            glm::vec2 uv((float)j / (float)slices, (float)(halfStacks + i) / (float)(stacks + 1));

            glm::vec3 tangentDir(-std::sin(theta), 0.0f, std::cos(theta));
            glm::vec4 tangent(glm::normalize(tangentDir), 1.0f);

            vertices.push_back({pos, normal, uv, tangent});
        }
    }

    // Generate bottom hemisphere (from equator down to bottom pole)
    for (int i = 1; i <= halfStacks; ++i) {
        float phi = (float)M_PI * 0.5f * (float)i / (float)halfStacks;  // 0 to PI/2
        float y = -radius * std::sin(phi) - cylinderHeight * 0.5f;
        float ringRadius = radius * std::cos(phi);

        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * (float)M_PI * (float)j / (float)slices;
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            glm::vec3 pos(x, y, z);
            // Normal for hemisphere points outward from sphere center (offset for bottom hemisphere)
            glm::vec3 sphereCenter(0.0f, -cylinderHeight * 0.5f, 0.0f);
            glm::vec3 normal = glm::normalize(pos - sphereCenter);
            glm::vec2 uv((float)j / (float)slices, (float)(halfStacks + cylinderRings + i) / (float)(stacks + 1));

            glm::vec3 tangentDir(-std::sin(theta), 0.0f, std::cos(theta));
            glm::vec4 tangent(glm::normalize(tangentDir), 1.0f);

            vertices.push_back({pos, normal, uv, tangent});
        }
    }

    // Generate indices
    // Total rings: halfStacks + 1 (top hemi) + cylinderRings + 1 (cylinder) + halfStacks (bottom hemi)
    int totalRings = halfStacks + 1 + cylinderRings + 1 + halfStacks;
    for (int i = 0; i < totalRings - 1; ++i) {
        for (int j = 0; j < slices; ++j) {
            int first = i * (slices + 1) + j;
            int second = first + slices + 1;

            indices.push_back(first);
            indices.push_back(first + 1);
            indices.push_back(second);

            indices.push_back(second);
            indices.push_back(first + 1);
            indices.push_back(second + 1);
        }
    }
    calculateBounds();
}

void Mesh::createCube() {
    // Tangents are computed based on UV layout - tangent points in +U direction
    glm::vec4 tangentPosX( 0.0f,  0.0f, -1.0f, 1.0f);  // Front face: +U is +X, tangent is +X
    glm::vec4 tangentNegX( 0.0f,  0.0f,  1.0f, 1.0f);  // Back face: +U is -X, tangent is -X
    glm::vec4 tangentPosY( 1.0f,  0.0f,  0.0f, 1.0f);  // Top face: +U is +X
    glm::vec4 tangentNegY( 1.0f,  0.0f,  0.0f, 1.0f);  // Bottom face: +U is +X
    glm::vec4 tangentPosZ( 1.0f,  0.0f,  0.0f, 1.0f);  // Right face: +U is -Z
    glm::vec4 tangentNegZ(-1.0f,  0.0f,  0.0f, 1.0f);  // Left face: +U is +Z

    vertices = {
        // Front face (Z+) - tangent along +X
        {{-0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},

        // Back face (Z-) - tangent along -X
        {{ 0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 1.0f}},

        // Top face (Y+) - tangent along +X
        {{-0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},

        // Bottom face (Y-) - tangent along +X
        {{-0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},

        // Right face (X+) - tangent along -Z
        {{ 0.5f, -0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f, 1.0f}},

        // Left face (X-) - tangent along +Z
        {{-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };

    indices = {
        0,  1,  2,  2,  3,  0,   // Front
        4,  5,  6,  6,  7,  4,   // Back
        8,  9,  10, 10, 11, 8,   // Top
        12, 13, 14, 14, 15, 12,  // Bottom
        16, 17, 18, 18, 19, 16,  // Right
        20, 21, 22, 22, 23, 20   // Left
    };
    calculateBounds();
}

void Mesh::setCustomGeometry(const std::vector<Vertex>& verts, const std::vector<uint32_t>& inds) {
    vertices = verts;
    indices = inds;
    calculateBounds();
}

void Mesh::createCylinder(float radius, float height, int segments) {
    vertices.clear();
    indices.clear();

    float halfHeight = height * 0.5f;

    // Create vertices for the cylinder body (two rings of vertices)
    for (int ring = 0; ring <= 1; ++ring) {
        float y = ring == 0 ? halfHeight : -halfHeight;

        for (int i = 0; i <= segments; ++i) {
            float theta = 2.0f * (float)M_PI * (float)i / (float)segments;
            float x = radius * std::cos(theta);
            float z = radius * std::sin(theta);

            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
            glm::vec2 uv((float)i / (float)segments, (float)ring);

            // Tangent points in the direction of theta increase
            glm::vec3 tangentDir(-std::sin(theta), 0.0f, std::cos(theta));
            glm::vec4 tangent(glm::normalize(tangentDir), 1.0f);

            vertices.push_back({pos, normal, uv, tangent});
        }
    }

    // Create indices for cylinder body
    for (int i = 0; i < segments; ++i) {
        int topLeft = i;
        int topRight = i + 1;
        int bottomLeft = (segments + 1) + i;
        int bottomRight = (segments + 1) + i + 1;

        // First triangle
        indices.push_back(topLeft);
        indices.push_back(topRight);
        indices.push_back(bottomLeft);

        // Second triangle
        indices.push_back(bottomLeft);
        indices.push_back(topRight);
        indices.push_back(bottomRight);
    }

    // Add top cap
    int topCenterIdx = vertices.size();
    vertices.push_back({{0.0f, halfHeight, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}});

    for (int i = 0; i < segments; ++i) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)segments;
        float x = radius * std::cos(theta);
        float z = radius * std::sin(theta);
        glm::vec2 uv((std::cos(theta) + 1.0f) * 0.5f, (std::sin(theta) + 1.0f) * 0.5f);
        vertices.push_back({{x, halfHeight, z}, {0.0f, 1.0f, 0.0f}, uv, {1.0f, 0.0f, 0.0f, 1.0f}});
    }

    for (int i = 0; i < segments; ++i) {
        indices.push_back(topCenterIdx);
        indices.push_back(topCenterIdx + i + 1);
        indices.push_back(topCenterIdx + ((i + 1) % segments) + 1);
    }

    // Add bottom cap
    int bottomCenterIdx = vertices.size();
    vertices.push_back({{0.0f, -halfHeight, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.5f}, {1.0f, 0.0f, 0.0f, 1.0f}});

    for (int i = 0; i < segments; ++i) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)segments;
        float x = radius * std::cos(theta);
        float z = radius * std::sin(theta);
        glm::vec2 uv((std::cos(theta) + 1.0f) * 0.5f, (std::sin(theta) + 1.0f) * 0.5f);
        vertices.push_back({{x, -halfHeight, z}, {0.0f, -1.0f, 0.0f}, uv, {1.0f, 0.0f, 0.0f, 1.0f}});
    }

    for (int i = 0; i < segments; ++i) {
        indices.push_back(bottomCenterIdx);
        indices.push_back(bottomCenterIdx + ((i + 1) % segments) + 1);
        indices.push_back(bottomCenterIdx + i + 1);
    }
    calculateBounds();
}

void Mesh::createRock(float baseRadius, int subdivisions, uint32_t seed, float roughness, float asymmetry) {
    vertices.clear();
    indices.clear();

    // Start with an icosahedron
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

    std::vector<glm::vec3> positions = {
        glm::normalize(glm::vec3(-1,  t,  0)),
        glm::normalize(glm::vec3( 1,  t,  0)),
        glm::normalize(glm::vec3(-1, -t,  0)),
        glm::normalize(glm::vec3( 1, -t,  0)),
        glm::normalize(glm::vec3( 0, -1,  t)),
        glm::normalize(glm::vec3( 0,  1,  t)),
        glm::normalize(glm::vec3( 0, -1, -t)),
        glm::normalize(glm::vec3( 0,  1, -t)),
        glm::normalize(glm::vec3( t,  0, -1)),
        glm::normalize(glm::vec3( t,  0,  1)),
        glm::normalize(glm::vec3(-t,  0, -1)),
        glm::normalize(glm::vec3(-t,  0,  1))
    };

    std::vector<uint32_t> tempIndices = {
        0, 11, 5,  0, 5, 1,  0, 1, 7,  0, 7, 10,  0, 10, 11,
        1, 5, 9,  5, 11, 4,  11, 10, 2,  10, 7, 6,  7, 1, 8,
        3, 9, 4,  3, 4, 2,  3, 2, 6,  3, 6, 8,  3, 8, 9,
        4, 9, 5,  2, 4, 11,  6, 2, 10,  8, 6, 7,  9, 8, 1
    };

    // Subdivide the icosahedron
    for (int i = 0; i < subdivisions; ++i) {
        std::unordered_map<EdgeKey, uint32_t, EdgeKeyHash> edgeMidpoints;
        std::vector<uint32_t> newIndices;

        auto getMidpoint = [&](uint32_t v0, uint32_t v1) -> uint32_t {
            EdgeKey key = v0 < v1 ? EdgeKey{v0, v1} : EdgeKey{v1, v0};
            auto it = edgeMidpoints.find(key);
            if (it != edgeMidpoints.end()) {
                return it->second;
            }

            glm::vec3 mid = glm::normalize((positions[v0] + positions[v1]) * 0.5f);
            uint32_t idx = static_cast<uint32_t>(positions.size());
            positions.push_back(mid);
            edgeMidpoints[key] = idx;
            return idx;
        };

        for (size_t j = 0; j < tempIndices.size(); j += 3) {
            uint32_t v0 = tempIndices[j];
            uint32_t v1 = tempIndices[j + 1];
            uint32_t v2 = tempIndices[j + 2];

            uint32_t m01 = getMidpoint(v0, v1);
            uint32_t m12 = getMidpoint(v1, v2);
            uint32_t m20 = getMidpoint(v2, v0);

            newIndices.push_back(v0);  newIndices.push_back(m01); newIndices.push_back(m20);
            newIndices.push_back(v1);  newIndices.push_back(m12); newIndices.push_back(m01);
            newIndices.push_back(v2);  newIndices.push_back(m20); newIndices.push_back(m12);
            newIndices.push_back(m01); newIndices.push_back(m12); newIndices.push_back(m20);
        }

        tempIndices = std::move(newIndices);
    }

    // Apply asymmetry scaling to create non-spherical base shape
    glm::vec3 scaleFactors(
        1.0f + asymmetry * (hash1(seed) * 2.0f - 1.0f),
        1.0f + asymmetry * (hash1(seed + 100) * 2.0f - 1.0f) * 0.5f,  // Less vertical stretch
        1.0f + asymmetry * (hash1(seed + 200) * 2.0f - 1.0f)
    );

    // Apply noise displacement to each vertex
    float noiseScale = 2.0f;  // Controls frequency of noise
    for (auto& pos : positions) {
        // Scale for asymmetry first
        glm::vec3 scaledPos = pos * scaleFactors;
        float len = glm::length(scaledPos);
        glm::vec3 dir = scaledPos / len;

        // Sample position for noise (use original direction for consistent noise)
        glm::vec3 samplePos = pos * noiseScale;

        // FBM displacement - creates natural rock surface
        float fbmDisp = fbm3D(samplePos.x, samplePos.y, samplePos.z, 5, 2.0f, 0.5f, seed);

        // Voronoi displacement - creates angular features
        float voronoiDisp = voronoi3D(samplePos.x * 1.5f, samplePos.y * 1.5f, samplePos.z * 1.5f, seed + 5000);
        voronoiDisp = 1.0f - voronoiDisp;  // Invert for convex features

        // Combine displacements
        float displacement = roughness * (fbmDisp * 0.7f + voronoiDisp * 0.3f);

        // Apply displacement along direction
        pos = dir * baseRadius * (1.0f + displacement);
    }

    // Flatten bottom slightly to make rocks sit better
    float minY = 0.0f;
    for (const auto& pos : positions) {
        minY = std::min(minY, pos.y);
    }
    float flattenThreshold = minY + baseRadius * 0.15f;
    for (auto& pos : positions) {
        if (pos.y < flattenThreshold) {
            float t = (flattenThreshold - pos.y) / (flattenThreshold - minY);
            pos.y = minY + (pos.y - minY) * (1.0f - t * 0.7f);
        }
    }

    // Calculate normals by averaging face normals at each vertex
    std::vector<glm::vec3> normals(positions.size(), glm::vec3(0.0f));
    for (size_t i = 0; i < tempIndices.size(); i += 3) {
        const glm::vec3& p0 = positions[tempIndices[i]];
        const glm::vec3& p1 = positions[tempIndices[i + 1]];
        const glm::vec3& p2 = positions[tempIndices[i + 2]];

        glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);
        float area = glm::length(faceNormal);
        if (area > 0.0001f) {
            faceNormal /= area;  // Normalize
            normals[tempIndices[i]] += faceNormal;
            normals[tempIndices[i + 1]] += faceNormal;
            normals[tempIndices[i + 2]] += faceNormal;
        }
    }

    for (auto& n : normals) {
        float len = glm::length(n);
        if (len > 0.0001f) {
            n /= len;
        } else {
            n = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    // Create vertices with proper attributes
    vertices.reserve(positions.size());
    for (size_t i = 0; i < positions.size(); ++i) {
        const glm::vec3& pos = positions[i];
        const glm::vec3& normal = normals[i];

        // Triplanar UV projection for rock texturing
        glm::vec3 absNormal = glm::abs(normal);
        glm::vec2 uv;
        if (absNormal.y > absNormal.x && absNormal.y > absNormal.z) {
            // Y-dominant: project from top/bottom
            uv = glm::vec2(pos.x, pos.z) * 0.5f;
        } else if (absNormal.x > absNormal.z) {
            // X-dominant: project from sides
            uv = glm::vec2(pos.z, pos.y) * 0.5f;
        } else {
            // Z-dominant: project from front/back
            uv = glm::vec2(pos.x, pos.y) * 0.5f;
        }

        // Compute tangent (perpendicular to normal, in dominant plane)
        glm::vec3 tangent;
        if (std::abs(normal.y) > 0.99f) {
            tangent = glm::vec3(1.0f, 0.0f, 0.0f);
        } else {
            tangent = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), normal));
        }

        vertices.push_back({pos, normal, uv, glm::vec4(tangent, 1.0f)});
    }

    indices = std::move(tempIndices);
    calculateBounds();
}

void Mesh::createBranch(float radius, float length, int sections, int segments, uint32_t seed,
                        float taper, float gnarliness) {
    vertices.clear();
    indices.clear();

    // Simple hash function for reproducible randomness
    auto hash = [seed](int a, int b) -> float {
        uint32_t n = static_cast<uint32_t>(a) * 374761393U + static_cast<uint32_t>(b) * 668265263U + seed;
        n = (n ^ (n >> 13)) * 1274126177U;
        return static_cast<float>(n & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
    };

    // Create rings along the branch
    for (int section = 0; section <= sections; ++section) {
        float t = static_cast<float>(section) / static_cast<float>(sections);
        float y = t * length;

        // Taper radius along length
        float sectionRadius = radius * (1.0f - t * (1.0f - taper));

        // Add gnarliness - offset the ring center slightly
        float offsetX = (hash(section, 0) - 0.5f) * gnarliness * radius;
        float offsetZ = (hash(section, 1) - 0.5f) * gnarliness * radius;

        for (int seg = 0; seg <= segments; ++seg) {
            float theta = 2.0f * static_cast<float>(M_PI) * static_cast<float>(seg) / static_cast<float>(segments);

            // Add per-vertex gnarliness
            float vertGnarl = 1.0f + (hash(section * 100 + seg, 2) - 0.5f) * gnarliness * 0.5f;

            float x = sectionRadius * vertGnarl * std::cos(theta) + offsetX;
            float z = sectionRadius * vertGnarl * std::sin(theta) + offsetZ;

            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(glm::vec3(std::cos(theta), 0.0f, std::sin(theta)));
            glm::vec2 uv(static_cast<float>(seg) / static_cast<float>(segments), t * 2.0f);

            // Tangent along the branch
            glm::vec3 tangentDir(-std::sin(theta), 0.0f, std::cos(theta));
            glm::vec4 tangent(glm::normalize(tangentDir), 1.0f);

            vertices.push_back({pos, normal, uv, tangent});
        }
    }

    // Create indices connecting rings
    int vertsPerRing = segments + 1;
    for (int section = 0; section < sections; ++section) {
        for (int seg = 0; seg < segments; ++seg) {
            int v0 = section * vertsPerRing + seg;
            int v1 = v0 + 1;
            int v2 = v0 + vertsPerRing;
            int v3 = v2 + 1;

            // First triangle
            indices.push_back(v0);
            indices.push_back(v2);
            indices.push_back(v1);

            // Second triangle
            indices.push_back(v1);
            indices.push_back(v2);
            indices.push_back(v3);
        }
    }

    calculateBounds();
}

void Mesh::createForkedBranch(float radius, float length, int sections, int segments, uint32_t seed,
                              float taper, float gnarliness, float forkAngle) {
    vertices.clear();
    indices.clear();

    // Hash function for reproducible randomness
    auto hash = [seed](int a, int b) -> float {
        uint32_t n = static_cast<uint32_t>(a) * 374761393U + static_cast<uint32_t>(b) * 668265263U + seed;
        n = (n ^ (n >> 13)) * 1274126177U;
        return static_cast<float>(n & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
    };

    // Fork point is 30-60% along the main trunk
    float forkT = 0.3f + hash(0, 0) * 0.3f;
    int forkSection = static_cast<int>(forkT * sections);
    float forkY = forkT * length;

    // Child branch parameters
    float childLength = length * (0.5f + hash(1, 0) * 0.3f);  // 50-80% of main length
    float childRadius = radius * 0.7f;  // 70% of main radius
    int childSections = sections / 2 + 1;

    // Vary fork angles slightly
    float leftAngle = forkAngle + (hash(2, 0) - 0.5f) * 0.2f;
    float rightAngle = forkAngle + (hash(3, 0) - 0.5f) * 0.2f;
    float leftYaw = hash(4, 0) * 2.0f * static_cast<float>(M_PI);
    float rightYaw = leftYaw + static_cast<float>(M_PI) * (0.8f + hash(5, 0) * 0.4f);  // Roughly opposite

    int vertsPerRing = segments + 1;

    // Helper to create a branch segment
    auto createBranchSegment = [&](glm::vec3 basePos, glm::vec3 direction, float baseRadius, float segLength,
                                   int numSections, float segTaper, int baseVertexOffset) {
        glm::vec3 up = glm::normalize(direction);
        glm::vec3 right = glm::normalize(glm::cross(up, glm::vec3(0.0f, 1.0f, 0.1f)));
        glm::vec3 forward = glm::normalize(glm::cross(right, up));

        for (int section = 0; section <= numSections; ++section) {
            float t = static_cast<float>(section) / static_cast<float>(numSections);
            float sectionRadius = baseRadius * (1.0f - t * (1.0f - segTaper));

            glm::vec3 center = basePos + up * (t * segLength);

            // Add gnarliness
            float offsetX = (hash(section + baseVertexOffset, 10) - 0.5f) * gnarliness * baseRadius;
            float offsetZ = (hash(section + baseVertexOffset, 11) - 0.5f) * gnarliness * baseRadius;
            center += right * offsetX + forward * offsetZ;

            for (int seg = 0; seg <= segments; ++seg) {
                float theta = 2.0f * static_cast<float>(M_PI) * static_cast<float>(seg) / static_cast<float>(segments);

                float vertGnarl = 1.0f + (hash((section + baseVertexOffset) * 100 + seg, 12) - 0.5f) * gnarliness * 0.5f;

                glm::vec3 localOffset = right * (std::cos(theta) * sectionRadius * vertGnarl)
                                      + forward * (std::sin(theta) * sectionRadius * vertGnarl);
                glm::vec3 pos = center + localOffset;

                glm::vec3 normal = glm::normalize(localOffset);
                glm::vec2 uv(static_cast<float>(seg) / static_cast<float>(segments), t * 2.0f);
                glm::vec3 tangentDir = glm::normalize(glm::cross(normal, up));
                glm::vec4 tangent(tangentDir, 1.0f);

                vertices.push_back({pos, normal, uv, tangent});
            }
        }
    };

    // Helper to create indices for a branch segment
    auto createBranchIndices = [&](int startVertex, int numSections) {
        for (int section = 0; section < numSections; ++section) {
            for (int seg = 0; seg < segments; ++seg) {
                int v0 = startVertex + section * vertsPerRing + seg;
                int v1 = v0 + 1;
                int v2 = v0 + vertsPerRing;
                int v3 = v2 + 1;

                indices.push_back(v0);
                indices.push_back(v2);
                indices.push_back(v1);

                indices.push_back(v1);
                indices.push_back(v2);
                indices.push_back(v3);
            }
        }
    };

    // Create main trunk (up to fork point)
    int trunkVertexStart = 0;
    createBranchSegment(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), radius, forkY, forkSection, taper, 0);
    createBranchIndices(trunkVertexStart, forkSection);

    // Fork position
    glm::vec3 forkPos(0.0f, forkY, 0.0f);
    float forkRadius = radius * (1.0f - forkT * (1.0f - taper));

    // Left fork direction
    glm::vec3 leftDir(std::sin(leftAngle) * std::cos(leftYaw),
                      std::cos(leftAngle),
                      std::sin(leftAngle) * std::sin(leftYaw));
    leftDir = glm::normalize(leftDir);

    // Right fork direction
    glm::vec3 rightDir(std::sin(rightAngle) * std::cos(rightYaw),
                       std::cos(rightAngle),
                       std::sin(rightAngle) * std::sin(rightYaw));
    rightDir = glm::normalize(rightDir);

    // Create left fork
    int leftForkStart = static_cast<int>(vertices.size());
    createBranchSegment(forkPos, leftDir, forkRadius * 0.85f, childLength, childSections, taper, 1000);
    createBranchIndices(leftForkStart, childSections);

    // Create right fork
    int rightForkStart = static_cast<int>(vertices.size());
    createBranchSegment(forkPos, rightDir, forkRadius * 0.85f, childLength * 0.9f, childSections, taper, 2000);
    createBranchIndices(rightForkStart, childSections);

    calculateBounds();
}

bool Mesh::upload(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue) {
    if (vertices.empty() || indices.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Mesh::upload: No vertex or index data");
        return false;
    }

    VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();

    // Create staging buffer using RAII
    ManagedBuffer stagingBuffer;
    if (!VulkanResourceFactory::createStagingBuffer(allocator, vertexBufferSize + indexBufferSize, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Mesh::upload: Failed to create staging buffer");
        return false;
    }

    // Copy data to staging buffer
    void* data = stagingBuffer.map();
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Mesh::upload: Failed to map staging buffer");
        return false;
    }
    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy(static_cast<char*>(data) + vertexBufferSize, indices.data(), indexBufferSize);
    stagingBuffer.unmap();

    // Create vertex buffer using RAII
    ManagedBuffer managedVertexBuffer;
    if (!VulkanResourceFactory::createVertexBuffer(allocator, vertexBufferSize, managedVertexBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Mesh::upload: Failed to create vertex buffer");
        return false;
    }

    // Create index buffer using RAII
    ManagedBuffer managedIndexBuffer;
    if (!VulkanResourceFactory::createIndexBuffer(allocator, indexBufferSize, managedIndexBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Mesh::upload: Failed to create index buffer");
        return false;
    }

    // Copy using command scope
    CommandScope cmd(device, commandPool, queue);
    if (!cmd.begin()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Mesh::upload: Failed to begin command buffer");
        return false;
    }

    vk::CommandBuffer vkCmd(cmd.get());

    auto vertexCopy = vk::BufferCopy{}.setSrcOffset(0).setDstOffset(0).setSize(vertexBufferSize);
    vkCmd.copyBuffer(stagingBuffer.get(), managedVertexBuffer.get(), vertexCopy);

    auto indexCopy = vk::BufferCopy{}.setSrcOffset(vertexBufferSize).setDstOffset(0).setSize(indexBufferSize);
    vkCmd.copyBuffer(stagingBuffer.get(), managedIndexBuffer.get(), indexCopy);

    if (!cmd.end()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Mesh::upload: Failed to submit command buffer");
        return false;
    }

    // Success - store allocator and transfer ownership to member variables
    allocator_ = allocator;
    managedVertexBuffer.releaseToRaw(vertexBuffer, vertexAllocation);
    managedIndexBuffer.releaseToRaw(indexBuffer, indexAllocation);

    return true;
}

Mesh::~Mesh() {
    releaseGPUResources();
}

void Mesh::releaseGPUResources() {
    if (allocator_) {
        if (vertexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, vertexBuffer, vertexAllocation);
            vertexBuffer = VK_NULL_HANDLE;
            vertexAllocation = VK_NULL_HANDLE;
        }
        if (indexBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator_, indexBuffer, indexAllocation);
            indexBuffer = VK_NULL_HANDLE;
            indexAllocation = VK_NULL_HANDLE;
        }
    }
}

Mesh::Mesh(Mesh&& other) noexcept
    : allocator_(other.allocator_)
    , vertices(std::move(other.vertices))
    , indices(std::move(other.indices))
    , bounds(other.bounds)
    , vertexBuffer(other.vertexBuffer)
    , vertexAllocation(other.vertexAllocation)
    , indexBuffer(other.indexBuffer)
    , indexAllocation(other.indexAllocation)
{
    other.allocator_ = VK_NULL_HANDLE;
    other.vertexBuffer = VK_NULL_HANDLE;
    other.indexBuffer = VK_NULL_HANDLE;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        // Clean up current resources
        if (allocator_) {
            if (vertexBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(allocator_, vertexBuffer, vertexAllocation);
            if (indexBuffer != VK_NULL_HANDLE) vmaDestroyBuffer(allocator_, indexBuffer, indexAllocation);
        }

        // Move from other
        allocator_ = other.allocator_;
        vertices = std::move(other.vertices);
        indices = std::move(other.indices);
        bounds = other.bounds;
        vertexBuffer = other.vertexBuffer;
        vertexAllocation = other.vertexAllocation;
        indexBuffer = other.indexBuffer;
        indexAllocation = other.indexAllocation;

        other.allocator_ = VK_NULL_HANDLE;
        other.vertexBuffer = VK_NULL_HANDLE;
        other.indexBuffer = VK_NULL_HANDLE;
    }
    return *this;
}
