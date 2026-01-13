#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

#include "SceneMaterial.h"

/**
 * SceneCollection - A collection of scene materials
 *
 * Groups all scene objects by their material. Each material has its own
 * textures, mesh variations, and instances. The renderer can iterate over
 * materials uniformly without knowing about specific systems (rock, detritus, etc).
 *
 * Usage:
 *   SceneCollection scene;
 *   scene.init(info);
 *
 *   // Add a material
 *   auto& rock = scene.createMaterial("rock", materialProps);
 *   rock.setDiffuseTexture(std::move(rockTexture));
 *   rock.setMeshes(std::move(rockMeshes));
 *   rock.setInstances(std::move(rockInstances));
 *   rock.rebuildSceneObjects();
 *
 *   // Render all materials
 *   for (const auto& [name, material] : scene.getMaterials()) {
 *       for (const auto& obj : material->getSceneObjects()) {
 *           render(obj, scene.getDescriptorSet(name, frameIndex));
 *       }
 *   }
 */
class SceneCollection {
public:
    using InitInfo = SceneMaterial::InitInfo;
    using MaterialProperties = SceneMaterial::MaterialProperties;

    SceneCollection() = default;
    ~SceneCollection() = default;

    // Non-copyable, non-movable (owns materials with GPU resources)
    SceneCollection(const SceneCollection&) = delete;
    SceneCollection& operator=(const SceneCollection&) = delete;
    SceneCollection(SceneCollection&&) = delete;
    SceneCollection& operator=(SceneCollection&&) = delete;

    /**
     * Initialize with Vulkan context
     */
    void init(const InitInfo& info);

    /**
     * Create a new material in the collection (takes ownership)
     * @param name Unique name for the material (e.g., "rock", "detritus")
     * @param props Material properties (roughness, metallic, shadow casting)
     * @return Reference to the created material
     */
    SceneMaterial& createMaterial(const std::string& name,
                                   const MaterialProperties& props = MaterialProperties::defaults());

    /**
     * Register an external material (non-owning reference)
     * Use this to register materials owned by other systems (RockSystem, DetritusSystem)
     * for unified iteration.
     * @param name Unique name for the material
     * @param material Reference to the material (must outlive this collection)
     */
    void registerMaterial(const std::string& name, SceneMaterial& material);

    /**
     * Get a material by name (returns nullptr if not found)
     * Searches both owned and registered materials.
     */
    SceneMaterial* getMaterial(const std::string& name);
    const SceneMaterial* getMaterial(const std::string& name) const;

    /**
     * Check if a material exists
     */
    bool hasMaterial(const std::string& name) const;

    /**
     * Get all materials
     */
    const std::unordered_map<std::string, std::unique_ptr<SceneMaterial>>& getMaterials() const {
        return materials_;
    }

    /**
     * Get material names in order of creation
     */
    const std::vector<std::string>& getMaterialNames() const { return materialOrder_; }

    /**
     * Collect all scene objects from all materials
     * Useful for shadow passes that need all objects
     */
    std::vector<Renderable> collectAllSceneObjects() const;

    /**
     * Set descriptor sets for a material
     * Called by Renderer after allocating descriptor sets
     */
    void setDescriptorSets(const std::string& name, std::vector<VkDescriptorSet>&& sets);

    /**
     * Get descriptor set for a material at a specific frame
     */
    VkDescriptorSet getDescriptorSet(const std::string& name, uint32_t frameIndex) const;

    /**
     * Get descriptor sets vector for a material (for writing)
     */
    std::vector<VkDescriptorSet>& getDescriptorSets(const std::string& name);
    const std::vector<VkDescriptorSet>& getDescriptorSets(const std::string& name) const;

    /**
     * Check if descriptor sets have been allocated for a material
     */
    bool hasDescriptorSets(const std::string& name) const;

    /**
     * Get total instance count across all materials
     */
    size_t getTotalInstanceCount() const;

    /**
     * Get number of materials
     */
    size_t getMaterialCount() const { return materials_.size(); }

    /**
     * Cleanup all materials and resources
     */
    void cleanup();

private:
    InitInfo initInfo_;
    bool initialized_ = false;

    // Owned materials (created via createMaterial)
    std::unordered_map<std::string, std::unique_ptr<SceneMaterial>> materials_;

    // Non-owning references to external materials (registered via registerMaterial)
    std::unordered_map<std::string, SceneMaterial*> registeredMaterials_;

    // Preserve creation/registration order for deterministic iteration
    std::vector<std::string> materialOrder_;

    // Descriptor sets per material: descriptorSets_[name][frameIndex]
    std::unordered_map<std::string, std::vector<VkDescriptorSet>> descriptorSets_;
};
