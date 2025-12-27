#pragma once

#include <vulkan/vulkan.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include "MaterialDescriptorFactory.h"

class DescriptorManager;
class Texture;

/**
 * MaterialRegistry - Data-driven material management system
 *
 * Replaces texture pointer comparison with MaterialId-based lookup.
 * Materials are registered at runtime and descriptor sets are created
 * automatically when createDescriptorSets() is called.
 *
 * Usage:
 *   MaterialRegistry registry;
 *   auto crateId = registry.registerMaterial("crate", crateTexture, crateNormal);
 *   auto groundId = registry.registerMaterial("ground", groundTexture, groundNormal);
 *   registry.createDescriptorSets(device, pool, layout, commonBindings);
 *   ...
 *   VkDescriptorSet set = registry.getDescriptorSet(materialId, frameIndex);
 */
class MaterialRegistry {
public:
    using MaterialId = uint32_t;
    static constexpr MaterialId INVALID_MATERIAL_ID = ~0u;

    struct MaterialDef {
        std::string name;
        const Texture* diffuse = nullptr;
        const Texture* normal = nullptr;
        float roughness = 0.5f;
        float metallic = 0.0f;

        // Optional PBR textures (for Substance/PBR materials)
        // If set, these override the scalar roughness/metallic values
        const Texture* roughnessMap = nullptr;
        const Texture* metallicMap = nullptr;
        const Texture* aoMap = nullptr;
        const Texture* heightMap = nullptr;
    };

    MaterialRegistry() = default;

    // Register a material and get its ID
    MaterialId registerMaterial(const MaterialDef& def);

    // Convenience: register with just textures
    MaterialId registerMaterial(const std::string& name, const Texture& diffuse, const Texture& normal);

    // Get material ID by name (returns INVALID_MATERIAL_ID if not found)
    MaterialId getMaterialId(const std::string& name) const;

    // Get material definition
    const MaterialDef* getMaterial(MaterialId id) const;

    // Create descriptor sets for all registered materials
    // Must be called after all materials are registered and resources are ready
    void createDescriptorSets(
        VkDevice device,
        DescriptorManager::Pool& pool,
        VkDescriptorSetLayout layout,
        uint32_t framesInFlight,
        const std::function<MaterialDescriptorFactory::CommonBindings(uint32_t frameIndex)>& getCommonBindings);

    // Get descriptor set for a material at a specific frame
    VkDescriptorSet getDescriptorSet(MaterialId id, uint32_t frameIndex) const;

    // Update cloud shadow binding for all materials (for late initialization)
    void updateCloudShadowBinding(VkDevice device, VkImageView view, VkSampler sampler);

    // Get number of registered materials
    size_t getMaterialCount() const { return materials.size(); }

    // Check if registry has descriptor sets created
    bool hasDescriptorSets() const { return !descriptorSets.empty(); }

private:
    std::vector<MaterialDef> materials;
    std::unordered_map<std::string, MaterialId> nameToId;

    // descriptorSets[materialId][frameIndex]
    std::vector<std::vector<VkDescriptorSet>> descriptorSets;
    uint32_t framesInFlight = 0;
};
