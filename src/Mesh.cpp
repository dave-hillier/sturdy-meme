#include "Mesh.h"
#include <cstring>
#include <stdexcept>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
