#pragma once

#include <vector>
#include "RenderableBuilder.h"

class SceneMaterial;

/**
 * SceneCollection - Registry for unified iteration over all scene materials
 *
 * Holds non-owning references to SceneMaterial instances (e.g., rock, detritus)
 * and provides a unified way to collect all scene objects for rendering passes
 * like shadow mapping.
 *
 * Usage:
 *   SceneCollection collection;
 *   collection.registerMaterial(&rockMaterial);
 *   collection.registerMaterial(&detritusMaterial);
 *
 *   // Shadow pass: iterate all shadow-casting objects
 *   for (const auto& obj : collection.collectAllSceneObjects()) {
 *       if (obj.castsShadow) { ... }
 *   }
 */
class SceneCollection {
public:
    SceneCollection() = default;
    ~SceneCollection() = default;

    // Non-copyable but movable
    SceneCollection(const SceneCollection&) = delete;
    SceneCollection& operator=(const SceneCollection&) = delete;
    SceneCollection(SceneCollection&&) = default;
    SceneCollection& operator=(SceneCollection&&) = default;

    /**
     * Register a material to be included in scene iteration
     * @param material Non-owning pointer to SceneMaterial (must outlive this collection)
     */
    void registerMaterial(SceneMaterial* material);

    /**
     * Unregister a material from the collection
     */
    void unregisterMaterial(SceneMaterial* material);

    /**
     * Clear all registered materials
     */
    void clear();

    /**
     * Collect all scene objects from all registered materials
     * Returns a vector combining renderables from all materials
     */
    std::vector<Renderable> collectAllSceneObjects() const;

    /**
     * Get read-only access to registered materials
     */
    const std::vector<SceneMaterial*>& getMaterials() const { return materials_; }

    /**
     * Get the number of registered materials
     */
    size_t getMaterialCount() const { return materials_.size(); }

private:
    std::vector<SceneMaterial*> materials_;
};
