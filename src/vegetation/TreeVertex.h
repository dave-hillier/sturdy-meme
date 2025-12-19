#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>

// Extended vertex structure for tree rendering with wind animation data
// Based on Ghost of Tsushima's approach where each vertex stores branch hierarchy info
// for GPU-driven wind sway animation.
//
// The wind animation uses a 3-level skeleton: trunk (0), branch (1), sub-branch (2)
// Each vertex knows its branch's origin point and can rotate around it based on wind.
struct TreeVertex {
    // Standard vertex data
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 tangent;  // xyz = tangent direction, w = handedness
    glm::vec4 color;    // rgba vertex color

    // Wind animation data (inspired by Ghost of Tsushima)
    glm::vec3 branchOrigin;   // Origin point of the branch this vertex belongs to
    glm::vec4 windParams;     // x = branch level (0=trunk, 1=branch, 2+=sub-branch)
                              // y = phase offset (for varied motion)
                              // z = flexibility (0=rigid at base, 1=fully flexible at tip)
                              // w = branch length (for scaling motion)

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(TreeVertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 7> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 7> attrs{};

        // location 0: position
        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(TreeVertex, position);

        // location 1: normal
        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(TreeVertex, normal);

        // location 2: texCoord
        attrs[2].binding = 0;
        attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = offsetof(TreeVertex, texCoord);

        // location 3: tangent
        attrs[3].binding = 0;
        attrs[3].location = 3;
        attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[3].offset = offsetof(TreeVertex, tangent);

        // location 6: color (skip 4,5 reserved for bone data)
        attrs[4].binding = 0;
        attrs[4].location = 6;
        attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[4].offset = offsetof(TreeVertex, color);

        // location 7: branchOrigin (wind animation)
        attrs[5].binding = 0;
        attrs[5].location = 7;
        attrs[5].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[5].offset = offsetof(TreeVertex, branchOrigin);

        // location 8: windParams (wind animation)
        attrs[6].binding = 0;
        attrs[6].location = 8;
        attrs[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[6].offset = offsetof(TreeVertex, windParams);

        return attrs;
    }
};
