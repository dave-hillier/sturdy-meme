#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>

#include "Mesh.h"
#include "Texture.h"

struct SceneObject {
    glm::mat4 transform;
    Mesh* mesh;
    Texture* texture;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveIntensity = 0.0f;
    glm::vec3 emissiveColor = glm::vec3(1.0f);  // Default white (uses texture color)
    bool castsShadow = true;
};

// Holds all scene resources (meshes, textures) and provides scene objects
class SceneBuilder {
public:
    struct InitInfo {
        VmaAllocator allocator;
        VkDevice device;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        VkPhysicalDevice physicalDevice;
        std::string resourcePath;
    };

    SceneBuilder() = default;
    ~SceneBuilder() = default;

    bool init(const InitInfo& info);
    void destroy(VmaAllocator allocator, VkDevice device);

    // Access to built scene
    const std::vector<SceneObject>& getSceneObjects() const { return sceneObjects; }
    std::vector<SceneObject>& getSceneObjects() { return sceneObjects; }
    size_t getPlayerObjectIndex() const { return playerObjectIndex; }

    // Access to textures for descriptor set creation
    Texture& getGroundTexture() { return groundTexture; }
    Texture& getGroundNormalMap() { return groundNormalMap; }
    Texture& getCrateTexture() { return crateTexture; }
    Texture& getCrateNormalMap() { return crateNormalMap; }
    Texture& getMetalTexture() { return metalTexture; }
    Texture& getMetalNormalMap() { return metalNormalMap; }
    Texture& getDefaultEmissiveMap() { return defaultEmissiveMap; }

    // Update player transform
    void updatePlayerTransform(const glm::mat4& transform);

private:
    bool createMeshes(const InitInfo& info);
    bool loadTextures(const InitInfo& info);
    void createSceneObjects();

    // Meshes
    Mesh groundMesh;
    Mesh cubeMesh;
    Mesh sphereMesh;
    Mesh capsuleMesh;

    // Textures
    Texture crateTexture;
    Texture crateNormalMap;
    Texture groundTexture;
    Texture groundNormalMap;
    Texture metalTexture;
    Texture metalNormalMap;
    Texture defaultEmissiveMap;  // Black texture for objects without emissive

    // Scene objects
    std::vector<SceneObject> sceneObjects;
    size_t playerObjectIndex = 0;
};
