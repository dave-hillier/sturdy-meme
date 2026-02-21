#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>

#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"
#include "scene/SceneMaterial.h"
#include "scene/SceneObjectInstance.h"
#include "core/material/MaterialDescriptorFactory.h"
#include "DescriptorManager.h"
#include "ecs/World.h"

/**
 * ScatterSystem - Generic system for scattered decoration objects
 *
 * A unified system replacing RockSystem and DetritusSystem. It takes
 * pre-created meshes and instance placements, handling:
 * - Texture loading and management
 * - SceneMaterial composition
 * - Descriptor set creation
 * - Rendering interface
 *
 * Use ScatterSystemFactory to create systems with specific mesh types
 * and placement algorithms.
 */
class ScatterSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit ScatterSystem(ConstructToken) {}

    struct Config {
        std::string name;                  // System name for logging (e.g., "rocks", "detritus")
        std::string diffuseTexturePath;    // Path relative to resourcePath
        std::string normalTexturePath;     // Path relative to resourcePath
        float materialRoughness = 0.7f;
        float materialMetallic = 0.0f;
        bool castsShadow = true;
    };

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

    /**
     * Factory: Create and initialize ScatterSystem.
     *
     * @param info Vulkan context and terrain info
     * @param config System configuration (textures, material properties)
     * @param meshes Pre-created and uploaded meshes (moved in)
     * @param instances Pre-generated instance placements (moved in)
     * @param transformModifier Optional callback to modify transforms during scene object creation
     * @return Initialized system or nullptr on failure
     */
    static std::unique_ptr<ScatterSystem> create(
        const InitInfo& info,
        const Config& config,
        std::vector<Mesh>&& meshes,
        std::vector<SceneObjectInstance>&& instances,
        std::function<glm::mat4(const SceneObjectInstance&, const glm::mat4&)> transformModifier = nullptr);

    ~ScatterSystem();

    // Non-copyable, non-movable
    ScatterSystem(const ScatterSystem&) = delete;
    ScatterSystem& operator=(const ScatterSystem&) = delete;
    ScatterSystem(ScatterSystem&&) = delete;
    ScatterSystem& operator=(ScatterSystem&&) = delete;

    // Get scene objects for rendering
    const std::vector<Renderable>& getSceneObjects() const { return material_.getSceneObjects(); }
    std::vector<Renderable>& getSceneObjects() { return material_.getSceneObjects(); }

    // Access the underlying material
    SceneMaterial& getMaterial() { return material_; }
    const SceneMaterial& getMaterial() const { return material_; }

    // Access textures for descriptor set binding
    Texture& getDiffuseTexture() { return *material_.getDiffuseTexture(); }
    Texture& getNormalTexture() { return *material_.getNormalTexture(); }

    // Statistics
    size_t getInstanceCount() const { return material_.getInstanceCount(); }
    size_t getMeshVariationCount() const { return material_.getMeshVariationCount(); }

    // System name for logging/debugging
    const std::string& getName() const { return name_; }

    // Descriptor set management
    bool createDescriptorSets(
        VkDevice device,
        DescriptorManager::Pool& pool,
        VkDescriptorSetLayout layout,
        uint32_t frameCount,
        std::function<MaterialDescriptorFactory::CommonBindings(uint32_t)> getCommonBindings);

    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const {
        return (frameIndex < descriptorSets_.size()) ? descriptorSets_[frameIndex] : VK_NULL_HANDLE;
    }

    bool hasDescriptorSets() const { return !descriptorSets_.empty(); }

    // ECS area entity (set after ECS world is available)
    void setAreaEntity(ecs::Entity entity) { areaEntity_ = entity; }
    ecs::Entity getAreaEntity() const { return areaEntity_; }

    // Create per-instance ECS entities as children of the area entity.
    // isRock: true for RockTag, false for DetritusTag on each instance.
    // Returns number of entities created.
    size_t createInstanceEntities(ecs::World& world, bool isRock);

    // Get created instance entity handles
    const std::vector<ecs::Entity>& getInstanceEntities() const { return instanceEntities_; }

    // Rebuild renderables from ECS entity transforms instead of internal instances.
    // Uses Transform and MeshRef components from instance entities.
    // Replaces the SceneMaterial::rebuildSceneObjects() call path.
    void rebuildFromECS(ecs::World& world);

private:
    bool initInternal(
        const InitInfo& info,
        const Config& config,
        std::vector<Mesh>&& meshes,
        std::vector<SceneObjectInstance>&& instances,
        std::function<glm::mat4(const SceneObjectInstance&, const glm::mat4&)> transformModifier);

    bool loadTextures(const InitInfo& info, const Config& config);

    std::string name_;
    SceneMaterial material_;
    std::vector<VkDescriptorSet> descriptorSets_;
    ecs::Entity areaEntity_ = ecs::NullEntity;
    std::vector<ecs::Entity> instanceEntities_;
};
