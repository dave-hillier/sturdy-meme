#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>

#include "TownGenerator.h"
#include "BuildingMeshGenerator.h"
#include "ModuleMeshGenerator.h"
#include "Mesh.h"
#include "UBOs.h"

// Instance data for GPU rendering
struct TownBuildingInstance {
    glm::mat4 modelMatrix;
    glm::vec4 colorTint;      // RGB = color variation, A = roughness
    glm::vec4 params;         // x = metallic, y = building type, zw = unused
};

struct TownRoadInstance {
    glm::mat4 modelMatrix;
    glm::vec4 params;         // x = width, y = isMainRoad, zw = unused
};

// Push constants for town rendering
struct TownPushConstants {
    glm::mat4 model;
    float roughness;
    float metallic;
    float padding[2];
};

class TownSystem {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkRenderPass renderPass;
        VkRenderPass shadowRenderPass;
        VkDescriptorPool descriptorPool;
        VkExtent2D extent;
        uint32_t shadowMapSize;
        std::string shaderPath;
        std::string texturePath;
        uint32_t framesInFlight;
        VkQueue graphicsQueue;
        VkCommandPool commandPool;
    };

    TownSystem() = default;
    ~TownSystem() = default;

    bool init(const InitInfo& info);
    void destroy(VkDevice device, VmaAllocator allocator);

    // Generate town using terrain height function
    // heightFunc: returns terrain height at world (x, z)
    void generate(const TownConfig& config, std::function<float(float, float)> heightFunc);

    // Update descriptor sets with shared resources
    void updateDescriptorSets(VkDevice device,
                              const std::vector<VkBuffer>& sceneUniformBuffers,
                              VkImageView shadowMapView,
                              VkSampler shadowSampler);

    // Record rendering commands
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex);
    void recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                          const glm::mat4& lightViewProj, int cascadeIndex);

    // Accessors
    const TownGenerator& getGenerator() const { return generator; }
    bool isGenerated() const { return generated; }

    // Debug visualization
    void setShowVoronoi(bool show) { showVoronoi = show; }
    bool isShowingVoronoi() const { return showVoronoi; }

private:
    bool createBuildingMeshes();
    bool createRoadMesh();
    bool createTextures();
    bool createDescriptorSetLayout();
    bool createGraphicsPipeline();
    bool createShadowPipeline();
    bool createDescriptorSets();
    bool createInstanceBuffers();

    void uploadMeshes();
    void updateInstanceData();

    // Vulkan resources
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    uint32_t shadowMapSize = 0;
    std::string shaderPath;
    std::string texturePath;
    uint32_t framesInFlight = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;

    // Building mesh - single combined mesh for all buildings
    Mesh buildingsMesh;
    Mesh roadMesh;

    // Building meshes (one per building type) - kept for fallback
    static constexpr size_t NUM_BUILDING_TYPES = 10;
    std::array<Mesh, NUM_BUILDING_TYPES> buildingMeshes;

    // Textures
    VkImage buildingTexture = VK_NULL_HANDLE;
    VmaAllocation buildingTextureAlloc = VK_NULL_HANDLE;
    VkImageView buildingTextureView = VK_NULL_HANDLE;
    VkSampler buildingTextureSampler = VK_NULL_HANDLE;

    VkImage roofTexture = VK_NULL_HANDLE;
    VmaAllocation roofTextureAlloc = VK_NULL_HANDLE;
    VkImageView roofTextureView = VK_NULL_HANDLE;

    VkImage roadTexture = VK_NULL_HANDLE;
    VmaAllocation roadTextureAlloc = VK_NULL_HANDLE;
    VkImageView roadTextureView = VK_NULL_HANDLE;

    // Pipeline resources
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout shadowDescriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline shadowPipeline = VK_NULL_HANDLE;

    // Descriptor sets (per frame)
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<VkDescriptorSet> shadowDescriptorSets;

    // Instance buffers for buildings
    VkBuffer buildingInstanceBuffer = VK_NULL_HANDLE;
    VmaAllocation buildingInstanceAlloc = VK_NULL_HANDLE;

    // Instance data organized by building type
    std::array<std::vector<TownBuildingInstance>, NUM_BUILDING_TYPES> buildingInstances;
    std::array<uint32_t, NUM_BUILDING_TYPES> buildingInstanceOffsets;
    std::array<uint32_t, NUM_BUILDING_TYPES> buildingInstanceCounts;
    uint32_t totalBuildingInstances = 0;

    // Road segment data
    std::vector<glm::mat4> roadTransforms;
    std::vector<float> roadWidths;

    // Generation
    TownGenerator generator;
    BuildingMeshGenerator meshGenerator;
    ModuleMeshGenerator moduleMeshGenerator;
    bool generated = false;

    // Generate combined mesh for all buildings using modular system
    void generateCombinedBuildingMesh();

    // Debug
    bool showVoronoi = false;
};
