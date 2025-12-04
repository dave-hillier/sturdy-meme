#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "TreeGenerator.h"
#include "Mesh.h"
#include "UBOs.h"

// Push constants for tree rendering
struct TreePushConstants {
    glm::mat4 model;
    float roughness;
    float metallic;
    float padding[2];
};

class TreeEditSystem {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkRenderPass renderPass;
        VkDescriptorPool descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
    };

    TreeEditSystem() = default;
    ~TreeEditSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Update descriptor sets with shared resources
    void updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& sceneUniformBuffers);

    // Update extent after resize
    void updateExtent(VkExtent2D newExtent) { extent = newExtent; }

    // Record rendering commands
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex);

    // Tree edit mode control
    bool isEnabled() const { return enabled; }
    void setEnabled(bool value) { enabled = value; }
    void toggle() { enabled = !enabled; }

    // Wireframe mode
    void setWireframeMode(bool value) { wireframeMode = value; }
    bool isWireframeMode() const { return wireframeMode; }

    // Show leaves toggle
    void setShowLeaves(bool value) { showLeaves = value; }
    bool getShowLeaves() const { return showLeaves; }

    // Tree parameters access (for UI modification)
    TreeParameters& getParameters() { return treeParams; }
    const TreeParameters& getParameters() const { return treeParams; }

    // Regenerate tree with current parameters
    void regenerateTree();

    // Get tree position for camera focusing
    glm::vec3 getTreeCenter() const;
    float getTreeHeight() const { return treeParams.trunkHeight * 1.5f; }

    // Tree transform
    void setPosition(const glm::vec3& pos) { position = pos; }
    glm::vec3 getPosition() const { return position; }
    void setScale(float s) { scale = s; }
    float getScale() const { return scale; }

private:
    // Initialization helpers
    bool createDescriptorSetLayout();
    bool createDescriptorSets();
    bool createPipelines();
    bool createWireframePipeline();

    // Upload tree mesh to GPU
    void uploadTreeMesh();

    // Vulkan resources
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Pipeline resources
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline solidPipeline = VK_NULL_HANDLE;
    VkPipeline wireframePipeline = VK_NULL_HANDLE;
    VkPipeline leafPipeline = VK_NULL_HANDLE;

    // Descriptor sets (per frame)
    std::vector<VkDescriptorSet> descriptorSets;

    // Tree data
    TreeGenerator generator;
    TreeParameters treeParams;
    Mesh branchMesh;
    Mesh leafMesh;
    bool meshesUploaded = false;

    // Editor state
    bool enabled = false;
    bool wireframeMode = false;
    bool showLeaves = true;
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
    float scale = 1.0f;
};
