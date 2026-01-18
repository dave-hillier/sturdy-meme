#include "ScatterSystem.h"
#include <SDL3/SDL_log.h>

std::unique_ptr<ScatterSystem> ScatterSystem::create(
    const InitInfo& info,
    const Config& config,
    std::vector<Mesh>&& meshes,
    std::vector<SceneObjectInstance>&& instances,
    std::function<glm::mat4(const SceneObjectInstance&, const glm::mat4&)> transformModifier)
{
    auto system = std::make_unique<ScatterSystem>(ConstructToken{});
    if (!system->initInternal(info, config, std::move(meshes), std::move(instances), transformModifier)) {
        return nullptr;
    }
    return system;
}

ScatterSystem::~ScatterSystem() {
    material_.cleanup();
}

bool ScatterSystem::initInternal(
    const InitInfo& info,
    const Config& config,
    std::vector<Mesh>&& meshes,
    std::vector<SceneObjectInstance>&& instances,
    std::function<glm::mat4(const SceneObjectInstance&, const glm::mat4&)> transformModifier)
{
    name_ = config.name;

    // Initialize the material with Vulkan context
    SceneMaterial::InitInfo materialInfo;
    materialInfo.device = info.device;
    materialInfo.allocator = info.allocator;
    materialInfo.commandPool = info.commandPool;
    materialInfo.graphicsQueue = info.graphicsQueue;
    materialInfo.physicalDevice = info.physicalDevice;
    materialInfo.resourcePath = info.resourcePath;
    materialInfo.getTerrainHeight = info.getTerrainHeight;
    materialInfo.terrainSize = info.terrainSize;

    SceneMaterial::MaterialProperties matProps;
    matProps.roughness = config.materialRoughness;
    matProps.metallic = config.materialMetallic;
    matProps.castsShadow = config.castsShadow;

    material_.init(materialInfo, matProps);

    if (!loadTextures(info, config)) {
        SDL_Log("ScatterSystem[%s]: Failed to load textures", name_.c_str());
        return false;
    }

    // Set meshes and instances
    material_.setMeshes(std::move(meshes));
    material_.setInstances(std::move(instances));

    // Build scene objects with optional transform modifier
    material_.rebuildSceneObjects(transformModifier);

    SDL_Log("ScatterSystem[%s]: Initialized with %zu instances (%zu mesh variations)",
            name_.c_str(), material_.getInstanceCount(), material_.getMeshVariationCount());

    return true;
}

bool ScatterSystem::loadTextures(const InitInfo& info, const Config& config) {
    std::string diffusePath = info.resourcePath + "/" + config.diffuseTexturePath;
    auto diffuseTexture = Texture::loadFromFile(diffusePath, info.allocator, info.device,
                                                 info.commandPool, info.graphicsQueue, info.physicalDevice);
    if (!diffuseTexture) {
        SDL_Log("ScatterSystem[%s]: Failed to load diffuse texture: %s", name_.c_str(), diffusePath.c_str());
        return false;
    }
    material_.setDiffuseTexture(std::move(diffuseTexture));

    std::string normalPath = info.resourcePath + "/" + config.normalTexturePath;
    auto normalTexture = Texture::loadFromFile(normalPath, info.allocator, info.device,
                                                info.commandPool, info.graphicsQueue, info.physicalDevice, false);
    if (!normalTexture) {
        SDL_Log("ScatterSystem[%s]: Failed to load normal texture: %s", name_.c_str(), normalPath.c_str());
        return false;
    }
    material_.setNormalTexture(std::move(normalTexture));

    return true;
}

bool ScatterSystem::createDescriptorSets(
    VkDevice device,
    DescriptorManager::Pool& pool,
    VkDescriptorSetLayout layout,
    uint32_t frameCount,
    std::function<MaterialDescriptorFactory::CommonBindings(uint32_t)> getCommonBindings)
{
    descriptorSets_ = pool.allocate(layout, frameCount);
    if (descriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ScatterSystem[%s]: Failed to allocate descriptor sets", name_.c_str());
        return false;
    }

    MaterialDescriptorFactory factory(device);
    for (uint32_t i = 0; i < frameCount; i++) {
        MaterialDescriptorFactory::CommonBindings common = getCommonBindings(i);

        MaterialDescriptorFactory::MaterialTextures mat{};
        mat.diffuseView = getDiffuseTexture().getImageView();
        mat.diffuseSampler = getDiffuseTexture().getSampler();
        mat.normalView = getNormalTexture().getImageView();
        mat.normalSampler = getNormalTexture().getSampler();

        factory.writeDescriptorSet(descriptorSets_[i], common, mat);
    }

    SDL_Log("ScatterSystem[%s]: Created %u descriptor sets", name_.c_str(), frameCount);
    return true;
}
