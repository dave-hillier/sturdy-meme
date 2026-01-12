#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <optional>
#include "Components.h"

// Forward declarations
class Mesh;
class Texture;
struct MaterialId;

// Resource Registry - Maps handles to actual GPU resources
// Provides stable handles that can be stored in ECS components while
// allowing the underlying resources to be managed separately
class ResourceRegistry {
public:
    // ========================================================================
    // Mesh Registration
    // ========================================================================

    // Register a mesh and get a handle
    MeshHandle registerMesh(Mesh* mesh, const std::string& name = "") {
        MeshHandle handle = static_cast<MeshHandle>(meshes_.size());
        meshes_.push_back(mesh);
        meshNames_.push_back(name);
        if (!name.empty()) {
            meshNameToHandle_[name] = handle;
        }
        return handle;
    }

    // Get mesh by handle
    Mesh* getMesh(MeshHandle handle) const {
        if (handle == InvalidMesh || handle >= meshes_.size()) {
            return nullptr;
        }
        return meshes_[handle];
    }

    // Find mesh by name
    std::optional<MeshHandle> findMesh(const std::string& name) const {
        auto it = meshNameToHandle_.find(name);
        if (it != meshNameToHandle_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // Get mesh name by handle
    std::string getMeshName(MeshHandle handle) const {
        if (handle >= meshNames_.size()) return "";
        return meshNames_[handle];
    }

    // Get total registered meshes
    size_t getMeshCount() const { return meshes_.size(); }

    // ========================================================================
    // Material Registration (wraps MaterialId from MaterialRegistry)
    // ========================================================================

    // Register a material ID and get a handle
    MaterialHandle registerMaterial(uint32_t materialId, const std::string& name = "") {
        MaterialHandle handle = static_cast<MaterialHandle>(materials_.size());
        materials_.push_back(materialId);
        materialNames_.push_back(name);
        if (!name.empty()) {
            materialNameToHandle_[name] = handle;
        }
        return handle;
    }

    // Get material ID by handle (for use with MaterialRegistry)
    uint32_t getMaterialId(MaterialHandle handle) const {
        if (handle == InvalidMaterial || handle >= materials_.size()) {
            return 0;  // Default material
        }
        return materials_[handle];
    }

    // Find material by name
    std::optional<MaterialHandle> findMaterial(const std::string& name) const {
        auto it = materialNameToHandle_.find(name);
        if (it != materialNameToHandle_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // Get material name by handle
    std::string getMaterialName(MaterialHandle handle) const {
        if (handle >= materialNames_.size()) return "";
        return materialNames_[handle];
    }

    // Get total registered materials
    size_t getMaterialCount() const { return materials_.size(); }

    // ========================================================================
    // Texture Registration
    // ========================================================================

    // Register a texture and get a handle
    TextureHandle registerTexture(Texture* texture, const std::string& name = "") {
        TextureHandle handle = static_cast<TextureHandle>(textures_.size());
        textures_.push_back(texture);
        textureNames_.push_back(name);
        if (!name.empty()) {
            textureNameToHandle_[name] = handle;
        }
        return handle;
    }

    // Get texture by handle
    Texture* getTexture(TextureHandle handle) const {
        if (handle == InvalidTexture || handle >= textures_.size()) {
            return nullptr;
        }
        return textures_[handle];
    }

    // Find texture by name
    std::optional<TextureHandle> findTexture(const std::string& name) const {
        auto it = textureNameToHandle_.find(name);
        if (it != textureNameToHandle_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // Get total registered textures
    size_t getTextureCount() const { return textures_.size(); }

    // ========================================================================
    // Utility
    // ========================================================================

    // Clear all registrations (use with caution - invalidates all handles)
    void clear() {
        meshes_.clear();
        meshNames_.clear();
        meshNameToHandle_.clear();
        materials_.clear();
        materialNames_.clear();
        materialNameToHandle_.clear();
        textures_.clear();
        textureNames_.clear();
        textureNameToHandle_.clear();
    }

    // Debug info
    struct Stats {
        size_t meshCount;
        size_t materialCount;
        size_t textureCount;
    };

    Stats getStats() const {
        return Stats{
            meshes_.size(),
            materials_.size(),
            textures_.size()
        };
    }

private:
    // Mesh storage
    std::vector<Mesh*> meshes_;
    std::vector<std::string> meshNames_;
    std::unordered_map<std::string, MeshHandle> meshNameToHandle_;

    // Material storage (stores MaterialId values)
    std::vector<uint32_t> materials_;
    std::vector<std::string> materialNames_;
    std::unordered_map<std::string, MaterialHandle> materialNameToHandle_;

    // Texture storage
    std::vector<Texture*> textures_;
    std::vector<std::string> textureNames_;
    std::unordered_map<std::string, TextureHandle> textureNameToHandle_;
};
