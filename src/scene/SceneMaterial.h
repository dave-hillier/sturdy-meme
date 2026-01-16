#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <cstdint>

#include "SceneObjectInstance.h"
#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"

/**
 * SceneMaterial - Represents a material with textures, properties, mesh variations, and instances
 *
 * Manages the complete rendering data for a material type:
 * - Multiple mesh variations (e.g., different rock shapes)
 * - Diffuse and normal textures
 * - Instance transforms (position, rotation, scale, mesh variation)
 * - Renderable generation for the rendering pipeline
 * - Material properties (roughness, metallic, shadow casting)
 *
 * This class uses composition rather than inheritance. Systems like RockSystem
 * and DetritusSystem own a SceneMaterial and delegate common operations to it.
 */
class SceneMaterial {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        VkPhysicalDevice physicalDevice;
        std::string resourcePath;
        std::function<float(float, float)> getTerrainHeight;
        float terrainSize;
    };

    struct MaterialProperties {
        float roughness = 0.7f;
        float metallic = 0.0f;
        bool castsShadow = true;

        static MaterialProperties defaults() { return {0.7f, 0.0f, true}; }
    };

    SceneMaterial() = default;
    ~SceneMaterial();

    // Non-copyable, non-movable (owns GPU resources)
    SceneMaterial(const SceneMaterial&) = delete;
    SceneMaterial& operator=(const SceneMaterial&) = delete;
    SceneMaterial(SceneMaterial&&) = delete;
    SceneMaterial& operator=(SceneMaterial&&) = delete;

    /**
     * Initialize with Vulkan context for resource management
     */
    void init(const InitInfo& info, const MaterialProperties& matProps = MaterialProperties::defaults());

    /**
     * Set the meshes for this material (transfers ownership)
     * Caller should have already uploaded meshes to GPU
     */
    void setMeshes(std::vector<Mesh>&& meshes);

    /**
     * Set the diffuse texture (transfers ownership)
     */
    void setDiffuseTexture(std::unique_ptr<Texture> texture);

    /**
     * Set the normal map texture (transfers ownership)
     */
    void setNormalTexture(std::unique_ptr<Texture> texture);

    /**
     * Add an instance to the material
     */
    void addInstance(const SceneObjectInstance& instance);

    /**
     * Set all instances at once (replaces existing)
     */
    void setInstances(std::vector<SceneObjectInstance>&& instances);

    /**
     * Clear all instances
     */
    void clearInstances();

    /**
     * Rebuild renderable scene objects from current instances and meshes
     * Must be called after adding/modifying instances
     *
     * @param transformModifier Optional callback to modify the transform matrix
     *        (e.g., for sinking rocks into ground or adding terrain-conform tilt)
     */
    void rebuildSceneObjects(
        std::function<glm::mat4(const SceneObjectInstance&, const glm::mat4&)> transformModifier = nullptr);

    /**
     * Release all GPU resources
     */
    void cleanup();

    // ========================================================================
    // Accessors
    // ========================================================================

    // Get scene objects for rendering
    const std::vector<Renderable>& getSceneObjects() const { return sceneObjects_; }
    std::vector<Renderable>& getSceneObjects() { return sceneObjects_; }

    // Get instances for physics/other systems
    const std::vector<SceneObjectInstance>& getInstances() const { return instances_; }

    // Get meshes for physics collision shapes
    const std::vector<Mesh>& getMeshes() const { return meshes_; }

    // Access textures for descriptor set binding
    Texture* getDiffuseTexture() { return diffuseTexture_.get(); }
    const Texture* getDiffuseTexture() const { return diffuseTexture_.get(); }
    Texture* getNormalTexture() { return normalTexture_.get(); }
    const Texture* getNormalTexture() const { return normalTexture_.get(); }

    // Statistics
    size_t getInstanceCount() const { return instances_.size(); }
    size_t getMeshVariationCount() const { return meshes_.size(); }

    // Check if initialized and has content
    bool isInitialized() const { return initialized_; }
    bool hasContent() const { return !instances_.empty() && !meshes_.empty(); }

private:
    bool initialized_ = false;

    // Vulkan context (stored for cleanup)
    VmaAllocator storedAllocator_ = VK_NULL_HANDLE;
    VkDevice storedDevice_ = VK_NULL_HANDLE;

    // Material properties for renderables
    MaterialProperties materialProps_;

    // Mesh variations
    std::vector<Mesh> meshes_;

    // Textures (RAII-managed)
    std::unique_ptr<Texture> diffuseTexture_;
    std::unique_ptr<Texture> normalTexture_;

    // Instance transforms
    std::vector<SceneObjectInstance> instances_;

    // Scene objects for rendering (generated from instances + meshes)
    std::vector<Renderable> sceneObjects_;
};
