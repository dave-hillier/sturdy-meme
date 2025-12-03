#include "MaterialRegistry.h"
#include "DescriptorManager.h"
#include "Texture.h"
#include <SDL3/SDL_log.h>

MaterialRegistry::MaterialId MaterialRegistry::registerMaterial(const MaterialDef& def) {
    // Check for duplicate name
    if (nameToId.find(def.name) != nameToId.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "MaterialRegistry: Material '%s' already registered", def.name.c_str());
        return nameToId[def.name];
    }

    MaterialId id = static_cast<MaterialId>(materials.size());
    materials.push_back(def);
    nameToId[def.name] = id;

    SDL_Log("MaterialRegistry: Registered material '%s' (id=%u)", def.name.c_str(), id);
    return id;
}

MaterialRegistry::MaterialId MaterialRegistry::registerMaterial(
    const std::string& name, const Texture& diffuse, const Texture& normal) {

    MaterialDef def;
    def.name = name;
    def.diffuse = &diffuse;
    def.normal = &normal;
    return registerMaterial(def);
}

MaterialRegistry::MaterialId MaterialRegistry::getMaterialId(const std::string& name) const {
    auto it = nameToId.find(name);
    if (it != nameToId.end()) {
        return it->second;
    }
    return INVALID_MATERIAL_ID;
}

const MaterialRegistry::MaterialDef* MaterialRegistry::getMaterial(MaterialId id) const {
    if (id < materials.size()) {
        return &materials[id];
    }
    return nullptr;
}

void MaterialRegistry::createDescriptorSets(
    VkDevice device,
    DescriptorManager::Pool& pool,
    VkDescriptorSetLayout layout,
    uint32_t frames,
    const std::function<MaterialDescriptorFactory::CommonBindings(uint32_t frameIndex)>& getCommonBindings) {

    framesInFlight = frames;
    descriptorSets.resize(materials.size());

    MaterialDescriptorFactory factory(device);

    for (MaterialId id = 0; id < materials.size(); ++id) {
        const auto& mat = materials[id];

        // Allocate descriptor sets for all frames
        descriptorSets[id] = pool.allocate(layout, framesInFlight);
        if (descriptorSets[id].empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "MaterialRegistry: Failed to allocate descriptor sets for '%s'", mat.name.c_str());
            continue;
        }

        // Write descriptor sets for each frame
        for (uint32_t frameIndex = 0; frameIndex < framesInFlight; ++frameIndex) {
            MaterialDescriptorFactory::CommonBindings common = getCommonBindings(frameIndex);

            MaterialDescriptorFactory::MaterialTextures textures{};
            if (mat.diffuse) {
                textures.diffuseView = mat.diffuse->getImageView();
                textures.diffuseSampler = mat.diffuse->getSampler();
            }
            if (mat.normal) {
                textures.normalView = mat.normal->getImageView();
                textures.normalSampler = mat.normal->getSampler();
            }

            factory.writeDescriptorSet(descriptorSets[id][frameIndex], common, textures);
        }
    }

    SDL_Log("MaterialRegistry: Created descriptor sets for %zu materials", materials.size());
}

VkDescriptorSet MaterialRegistry::getDescriptorSet(MaterialId id, uint32_t frameIndex) const {
    if (id < descriptorSets.size() && frameIndex < framesInFlight) {
        return descriptorSets[id][frameIndex];
    }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "MaterialRegistry: Invalid material id=%u or frameIndex=%u", id, frameIndex);
    return VK_NULL_HANDLE;
}

void MaterialRegistry::updateCloudShadowBinding(VkDevice device, VkImageView view, VkSampler sampler) {
    MaterialDescriptorFactory factory(device);

    for (const auto& materialSets : descriptorSets) {
        for (VkDescriptorSet set : materialSets) {
            factory.updateCloudShadowBinding(set, view, sampler);
        }
    }

    SDL_Log("MaterialRegistry: Updated cloud shadow binding for %zu materials", materials.size());
}
