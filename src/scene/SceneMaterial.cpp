#include "SceneMaterial.h"
#include <SDL3/SDL_log.h>

SceneMaterial::~SceneMaterial() {
    cleanup();
}

void SceneMaterial::init(const InitInfo& info, const MaterialProperties& matProps) {
    storedAllocator_ = info.allocator;
    storedDevice_ = info.device;
    materialProps_ = matProps;
    initialized_ = true;
}

void SceneMaterial::setMeshes(std::vector<Mesh>&& meshes) {
    // Clean up existing meshes first
    for (auto& mesh : meshes_) {
        mesh.releaseGPUResources();
    }
    meshes_ = std::move(meshes);
}

void SceneMaterial::setDiffuseTexture(std::unique_ptr<Texture> texture) {
    diffuseTexture_ = std::move(texture);
}

void SceneMaterial::setNormalTexture(std::unique_ptr<Texture> texture) {
    normalTexture_ = std::move(texture);
}

void SceneMaterial::addInstance(const SceneObjectInstance& instance) {
    instances_.push_back(instance);
}

void SceneMaterial::setInstances(std::vector<SceneObjectInstance>&& instances) {
    instances_ = std::move(instances);
}

void SceneMaterial::clearInstances() {
    instances_.clear();
    sceneObjects_.clear();
}

void SceneMaterial::rebuildSceneObjects(
    std::function<glm::mat4(const SceneObjectInstance&, const glm::mat4&)> transformModifier) {

    sceneObjects_.clear();
    sceneObjects_.reserve(instances_.size());

    for (const auto& instance : instances_) {
        if (instance.meshVariation >= meshes_.size()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "SceneMaterial: Instance mesh variation %u out of range (have %zu meshes)",
                instance.meshVariation, meshes_.size());
            continue;
        }

        glm::mat4 transform = instance.getTransformMatrix();

        // Apply optional transform modification (e.g., sinking into ground, terrain conforming)
        if (transformModifier) {
            transform = transformModifier(instance, transform);
        }

        sceneObjects_.push_back(RenderableBuilder()
            .withTransform(transform)
            .withMesh(&meshes_[instance.meshVariation])
            .withTexture(diffuseTexture_.get())
            .withRoughness(materialProps_.roughness)
            .withMetallic(materialProps_.metallic)
            .withCastsShadow(materialProps_.castsShadow)
            .build());
    }
}

void SceneMaterial::cleanup() {
    if (storedDevice_ == VK_NULL_HANDLE) return;

    // Release RAII-managed textures
    diffuseTexture_.reset();
    normalTexture_.reset();

    // Manually release mesh GPU resources
    for (auto& mesh : meshes_) {
        mesh.releaseGPUResources();
    }
    meshes_.clear();

    instances_.clear();
    sceneObjects_.clear();

    initialized_ = false;
}
