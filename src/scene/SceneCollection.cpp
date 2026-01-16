#include "SceneCollection.h"
#include "SceneMaterial.h"
#include <algorithm>

void SceneCollection::registerMaterial(SceneMaterial* material) {
    if (material == nullptr) return;

    // Avoid duplicates
    auto it = std::find(materials_.begin(), materials_.end(), material);
    if (it == materials_.end()) {
        materials_.push_back(material);
    }
}

void SceneCollection::unregisterMaterial(SceneMaterial* material) {
    auto it = std::find(materials_.begin(), materials_.end(), material);
    if (it != materials_.end()) {
        materials_.erase(it);
    }
}

void SceneCollection::clear() {
    materials_.clear();
}

std::vector<Renderable> SceneCollection::collectAllSceneObjects() const {
    std::vector<Renderable> allObjects;

    // Estimate total size for reservation
    size_t totalSize = 0;
    for (const auto* material : materials_) {
        if (material && material->hasContent()) {
            totalSize += material->getSceneObjects().size();
        }
    }
    allObjects.reserve(totalSize);

    // Collect from all materials
    for (const auto* material : materials_) {
        if (material && material->hasContent()) {
            const auto& objects = material->getSceneObjects();
            allObjects.insert(allObjects.end(), objects.begin(), objects.end());
        }
    }

    return allObjects;
}
