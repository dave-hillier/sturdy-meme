#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>

#include "TreeOptions.h"
#include "TreeGenerator.h"
#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"

// A single tree instance in the scene
struct TreeInstanceData {
    glm::vec3 position;
    float rotation;         // Y-axis rotation
    float scale;            // Uniform scale factor
    uint32_t meshIndex;     // Which tree mesh to use
    bool isSelected;        // Is this the currently editable tree
};

class TreeSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        VkPhysicalDevice physicalDevice;
        std::string resourcePath;
        std::function<float(float, float)> getTerrainHeight;  // Terrain height query
        float terrainSize;
    };

    /**
     * Factory: Create and initialize TreeSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<TreeSystem> create(const InitInfo& info);

    ~TreeSystem();

    // Non-copyable, non-movable
    TreeSystem(const TreeSystem&) = delete;
    TreeSystem& operator=(const TreeSystem&) = delete;
    TreeSystem(TreeSystem&&) = delete;
    TreeSystem& operator=(TreeSystem&&) = delete;

    // Get scene objects for rendering (integrated with existing pipeline)
    const std::vector<Renderable>& getBranchRenderables() const { return branchRenderables_; }
    std::vector<Renderable>& getBranchRenderables() { return branchRenderables_; }

    const std::vector<Renderable>& getLeafRenderables() const { return leafRenderables_; }
    std::vector<Renderable>& getLeafRenderables() { return leafRenderables_; }

    // Tree management
    uint32_t addTree(const glm::vec3& position, float rotation, float scale, const TreeOptions& options);
    void removeTree(uint32_t index);
    void selectTree(int index);
    int getSelectedTreeIndex() const { return selectedTreeIndex_; }

    // Update selected tree's options (triggers mesh regeneration)
    void updateSelectedTreeOptions(const TreeOptions& options);
    const TreeOptions* getSelectedTreeOptions() const;

    // Preset management
    void loadPreset(const std::string& name);
    void setPreset(const TreeOptions& preset);

    // Statistics
    size_t getTreeCount() const { return treeInstances_.size(); }
    size_t getMeshCount() const { return branchMeshes_.size(); }

    // Get tree instances for physics/other systems
    const std::vector<TreeInstanceData>& getTreeInstances() const { return treeInstances_; }

    // Access textures for GUI display
    const TreeOptions& getDefaultOptions() const { return defaultOptions_; }
    TreeOptions& getDefaultOptions() { return defaultOptions_; }

    // Access textures for descriptor set binding (uses default texture if type not found)
    Texture* getBarkTexture(const std::string& type) const;
    Texture* getBarkNormalMap(const std::string& type) const;
    Texture* getLeafTexture(const std::string& type) const;

    // Get all texture type names (for iteration in renderer)
    std::vector<std::string> getBarkTextureTypes() const;
    std::vector<std::string> getLeafTextureTypes() const;

    // Regenerate tree at index with new options
    void regenerateTree(uint32_t index);

private:
    TreeSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();
    bool loadTextures(const InitInfo& info);
    bool generateTreeMesh(const TreeOptions& options, Mesh& branchMesh, Mesh& leafMesh);
    void createSceneObjects();
    void rebuildSceneObjects();

    // Stored for RAII cleanup and reload
    VmaAllocator storedAllocator_ = VK_NULL_HANDLE;
    VkDevice storedDevice_ = VK_NULL_HANDLE;
    VkCommandPool storedCommandPool_ = VK_NULL_HANDLE;
    VkQueue storedQueue_ = VK_NULL_HANDLE;
    VkPhysicalDevice storedPhysicalDevice_ = VK_NULL_HANDLE;
    std::string storedResourcePath_;

    // Tree generator
    TreeGenerator generator_;

    // Tree options per mesh
    std::vector<TreeOptions> treeOptions_;
    TreeOptions defaultOptions_;

    // Tree meshes (branches and leaves separate)
    std::vector<Mesh> branchMeshes_;
    std::vector<Mesh> leafMeshes_;

    // Textures indexed by type name (e.g., "oak", "pine", "ash")
    std::unordered_map<std::string, std::unique_ptr<Texture>> barkTextures_;
    std::unordered_map<std::string, std::unique_ptr<Texture>> barkNormalMaps_;
    std::unordered_map<std::string, std::unique_ptr<Texture>> leafTextures_;

    // Tree instances (positions, rotations, etc.)
    std::vector<TreeInstanceData> treeInstances_;
    int selectedTreeIndex_ = -1;

    // Scene objects for rendering
    std::vector<Renderable> branchRenderables_;
    std::vector<Renderable> leafRenderables_;
};
