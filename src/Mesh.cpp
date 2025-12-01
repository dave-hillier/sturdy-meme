#include "Mesh.h"
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
}

void Mesh::setCustomGeometry(const std::vector<Vertex>& verts, const std::vector<uint32_t>& inds) {
    vertices = verts;
    indices = inds;
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
}

void Mesh::upload(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue) {
    VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
    VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = vertexBufferSize + indexBufferSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    vmaCreateBuffer(allocator, &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation, nullptr);

    void* data;
    vmaMapMemory(allocator, stagingAllocation, &data);
    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy(static_cast<char*>(data) + vertexBufferSize, indices.data(), indexBufferSize);
    vmaUnmapMemory(allocator, stagingAllocation);

    VkBufferCreateInfo vertexBufferInfo{};
    vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vertexBufferInfo.size = vertexBufferSize;
    vertexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo vertexAllocInfo{};
    vertexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    vmaCreateBuffer(allocator, &vertexBufferInfo, &vertexAllocInfo, &vertexBuffer, &vertexAllocation, nullptr);

    VkBufferCreateInfo indexBufferInfo{};
    indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    indexBufferInfo.size = indexBufferSize;
    indexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo indexAllocInfo{};
    indexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    vmaCreateBuffer(allocator, &indexBufferInfo, &indexAllocInfo, &indexBuffer, &indexAllocation, nullptr);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy vertexCopyRegion{};
    vertexCopyRegion.srcOffset = 0;
    vertexCopyRegion.dstOffset = 0;
    vertexCopyRegion.size = vertexBufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, vertexBuffer, 1, &vertexCopyRegion);

    VkBufferCopy indexCopyRegion{};
    indexCopyRegion.srcOffset = vertexBufferSize;
    indexCopyRegion.dstOffset = 0;
    indexCopyRegion.size = indexBufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, indexBuffer, 1, &indexCopyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

void Mesh::destroy(VmaAllocator allocator) {
    if (vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, vertexBuffer, vertexAllocation);
        vertexBuffer = VK_NULL_HANDLE;
    }
    if (indexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator, indexBuffer, indexAllocation);
        indexBuffer = VK_NULL_HANDLE;
    }
}
