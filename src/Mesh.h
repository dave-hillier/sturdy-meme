#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 tangent;  // xyz = tangent direction, w = handedness (+1 or -1)
    glm::vec4 color = glm::vec4(1.0f);  // vertex color (glTF material baseColorFactor)

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 5> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, position);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, tangent);

        // Note: location 4, 5 reserved for bone data in SkinnedVertex
        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 6;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Vertex, color);

        return attributeDescriptions;
    }
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh() = default;

    void createCube();
    void createPlane(float width, float depth);
    void createDisc(float radius, int segments, float uvScale = 1.0f);
    void createSphere(float radius, int stacks, int slices);
    void createCapsule(float radius, float height, int stacks, int slices);
    void createCylinder(float radius, float height, int segments);
    void createRock(float baseRadius, int subdivisions, uint32_t seed, float roughness = 0.3f, float asymmetry = 0.2f);
    void setCustomGeometry(const std::vector<Vertex>& verts, const std::vector<uint32_t>& inds);
    void upload(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue);
    void destroy(VmaAllocator allocator);

    VkBuffer getVertexBuffer() const { return vertexBuffer; }
    VkBuffer getIndexBuffer() const { return indexBuffer; }
    uint32_t getIndexCount() const { return static_cast<uint32_t>(indices.size()); }

    // Access to vertex data for physics collision shapes
    const std::vector<Vertex>& getVertices() const { return vertices; }

private:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAllocation = VK_NULL_HANDLE;
};
