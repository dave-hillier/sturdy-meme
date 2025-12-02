#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>

#include "GLTFLoader.h"

// Extended vertex format with bone influences
struct SkinnedVertex {
    glm::vec3 position;     // location 0
    glm::vec3 normal;       // location 1
    glm::vec2 texCoord;     // location 2
    glm::vec4 tangent;      // location 3 (xyz = direction, w = handedness)
    glm::uvec4 boneIndices; // location 4 (4 bone influences)
    glm::vec4 boneWeights;  // location 5

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(SkinnedVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 6> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 6> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(SkinnedVertex, position);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(SkinnedVertex, normal);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(SkinnedVertex, texCoord);

        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(SkinnedVertex, tangent);

        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_UINT;
        attributeDescriptions[4].offset = offsetof(SkinnedVertex, boneIndices);

        attributeDescriptions[5].binding = 0;
        attributeDescriptions[5].location = 5;
        attributeDescriptions[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributeDescriptions[5].offset = offsetof(SkinnedVertex, boneWeights);

        return attributeDescriptions;
    }
};

// Result of loading a skinned mesh from glTF
struct SkinnedMeshData {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    Skeleton skeleton;
    std::string baseColorTexturePath;
    std::string normalTexturePath;
};

// Mesh with skinning data and skeleton reference
class SkinnedMesh {
public:
    SkinnedMesh() = default;
    ~SkinnedMesh() = default;

    void setData(const SkinnedMeshData& data);
    void upload(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue);
    void destroy(VmaAllocator allocator);

    VkBuffer getVertexBuffer() const { return vertexBuffer; }
    VkBuffer getIndexBuffer() const { return indexBuffer; }
    uint32_t getIndexCount() const { return static_cast<uint32_t>(indices.size()); }

    const Skeleton& getSkeleton() const { return skeleton; }
    Skeleton& getSkeleton() { return skeleton; }

    // Access to vertex data for debugging
    const std::vector<SkinnedVertex>& getVertices() const { return vertices; }

private:
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    Skeleton skeleton;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAllocation = VK_NULL_HANDLE;
};

// Maximum number of bones supported in the shader
constexpr uint32_t MAX_BONES = 128;

// Bone matrices UBO (binding 10)
struct BoneMatricesUBO {
    glm::mat4 bones[MAX_BONES];
};
