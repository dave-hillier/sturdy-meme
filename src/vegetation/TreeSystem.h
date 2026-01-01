#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <optional>

#include "TreeOptions.h"
#include "TreeGenerator.h"
#include "TreeCollision.h"
#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"
#include "BufferUtils.h"

// GPU leaf instance data - matches shaders/tree_leaf_instance.glsl
// std430 layout: 32 bytes per instance
struct LeafInstanceGPU {
    glm::vec4 positionAndSize;  // xyz = world position, w = size
    glm::vec4 orientation;       // quaternion (x, y, z, w)
};
static_assert(sizeof(LeafInstanceGPU) == 32, "LeafInstanceGPU must be 32 bytes for std430 layout");

// Per-tree leaf instance offsets and counts for instanced drawing
struct LeafDrawInfo {
    uint32_t firstInstance;  // Starting instance in the SSBO
    uint32_t instanceCount;  // Number of leaf instances for this tree
};

// A single tree instance in the scene
struct TreeInstanceData {
    glm::vec3 position;
    float rotation;         // Y-axis rotation
    float scale;            // Uniform scale factor
    uint32_t meshIndex;     // Which tree mesh to use
    uint32_t archetypeIndex; // Which impostor archetype to use (0=oak, 1=pine, 2=ash, 3=aspen)
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

    /**
     * Add a tree from pre-generated staged data (for threaded loading)
     * This uploads pre-generated mesh data to GPU without regenerating
     *
     * @param position World position
     * @param rotation Y-axis rotation in radians
     * @param scale Uniform scale factor
     * @param options Tree options for texture selection
     * @param branchVertexData Raw vertex data (Vertex structs)
     * @param branchVertexCount Number of vertices
     * @param branchIndices Index buffer data
     * @param leafInstanceData Raw leaf instance data (LeafInstanceGPU structs)
     * @param leafInstanceCount Number of leaf instances
     * @param archetypeIndex Impostor archetype index
     * @return Tree index, or UINT32_MAX on failure
     */
    uint32_t addTreeFromStagedData(
        const glm::vec3& position, float rotation, float scale,
        const TreeOptions& options,
        const std::vector<uint8_t>& branchVertexData,
        uint32_t branchVertexCount,
        const std::vector<uint32_t>& branchIndices,
        const std::vector<uint8_t>& leafInstanceData,
        uint32_t leafInstanceCount,
        uint32_t archetypeIndex);

    /**
     * Batch upload leaf instance buffer after adding multiple trees
     * Call this after adding all trees to avoid re-uploading for each tree
     */
    bool finalizeLeafInstanceBuffer();

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

    // Generate collision capsule data for a tree instance
    // Returns capsules in world space (tree position + rotation + scale applied)
    std::vector<PhysicsWorld::CapsuleData> getTreeCollisionCapsules(
        uint32_t treeIndex,
        const TreeCollision::Config& config = TreeCollision::Config{}) const;

    // Get raw mesh data for a tree (for external collision generation)
    const TreeMeshData* getTreeMeshData(uint32_t meshIndex) const;

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

    // Leaf instancing accessors (for TreeRenderer)
    VkBuffer getLeafInstanceBuffer() const { return leafInstanceBuffer_; }
    VkDeviceSize getLeafInstanceBufferSize() const { return leafInstanceBufferSize_; }
    const Mesh& getSharedLeafQuadMesh() const { return sharedLeafQuadMesh_; }
    const std::vector<LeafDrawInfo>& getLeafDrawInfo() const { return leafDrawInfoPerTree_; }

    // Accessors for impostor generation
    const Mesh& getBranchMesh(uint32_t meshIndex) const { return branchMeshes_[meshIndex]; }
    const std::vector<LeafInstanceGPU>& getLeafInstances(uint32_t meshIndex) const { return leafInstancesPerTree_[meshIndex]; }
    const TreeOptions& getTreeOptions(uint32_t meshIndex) const { return treeOptions_[meshIndex]; }

    // Get full tree bounds (branches + leaves) for accurate imposter sizing
    const AABB& getFullTreeBounds(uint32_t meshIndex) const { return fullTreeBounds_[meshIndex]; }

private:
    TreeSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();
    bool loadTextures(const InitInfo& info);
    bool generateTreeMesh(const TreeOptions& options, Mesh& branchMesh, std::vector<LeafInstanceGPU>& leafInstances,
                          TreeMeshData* meshDataOut = nullptr);
    bool createSharedLeafQuadMesh();
    bool uploadLeafInstanceBuffer();
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

    // Tree meshes (branches only - leaves use instanced quad)
    std::vector<Mesh> branchMeshes_;

    // Shared leaf quad mesh (4 vertices, 6 indices) used for all leaf instances
    Mesh sharedLeafQuadMesh_;

    // Leaf instance data per tree (CPU-side, uploaded to GPU SSBO)
    // Each tree's leaves are stored contiguously: tree 0 leaves, tree 1 leaves, etc.
    std::vector<std::vector<LeafInstanceGPU>> leafInstancesPerTree_;

    // All leaf instances combined for GPU upload (flattened from leafInstancesPerTree_)
    std::vector<LeafInstanceGPU> allLeafInstances_;

    // Per-tree leaf instance offsets and counts for instanced drawing
    std::vector<LeafDrawInfo> leafDrawInfoPerTree_;

    // Leaf instance SSBO (storage buffer for GPU)
    VkBuffer leafInstanceBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafInstanceAllocation_ = VK_NULL_HANDLE;
    VkDeviceSize leafInstanceBufferSize_ = 0;

    // Raw mesh data (stored for collision generation)
    std::vector<TreeMeshData> treeMeshData_;

    // Full tree bounds (branches + leaves) per mesh - for accurate imposter sizing
    std::vector<AABB> fullTreeBounds_;

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
