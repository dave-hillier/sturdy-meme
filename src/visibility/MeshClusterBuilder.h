#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>

#include "Mesh.h"
#include "VmaBuffer.h"

/**
 * MeshCluster - A contiguous group of triangles from a mesh
 *
 * Each cluster contains 64-128 triangles with local bounding data
 * for efficient GPU culling at the cluster granularity.
 *
 * The cluster stores indices into the global vertex/index buffers.
 */
struct MeshCluster {
    // Bounding data for culling
    glm::vec4 boundingSphere;   // xyz = center, w = radius (object space)
    glm::vec3 aabbMin;          // Object-space AABB min
    float _pad0;
    glm::vec3 aabbMax;          // Object-space AABB max
    float _pad1;

    // Cone data for backface cluster culling (Nanite-style)
    glm::vec3 coneAxis;         // Averaged normal direction
    float coneAngle;            // Half-angle of normal cone (cos)

    // Index range in the global index buffer
    uint32_t firstIndex;        // Offset into global index buffer
    uint32_t indexCount;         // Number of indices (triangles * 3)
    uint32_t firstVertex;       // Base vertex offset
    uint32_t meshId;            // Which mesh this cluster belongs to

    // LOD information
    float parentError;          // Object-space error of parent cluster
    float error;                // Object-space error of this cluster
    uint32_t lodLevel;          // LOD level (0 = highest detail)
    uint32_t parentIndex;       // Index of parent in cluster array (UINT32_MAX for root)

    // DAG connectivity
    uint32_t firstChildIndex;   // Index of first child in cluster array
    uint32_t childCount;        // Number of children (0 = leaf)
    uint32_t _pad2;
    uint32_t _pad3;
};

/**
 * ClusteredMesh - Result of clustering a single mesh
 *
 * Contains the clusters and their associated vertex/index data
 * ready for upload to the GPU.
 */
struct ClusteredMesh {
    std::vector<MeshCluster> clusters;

    // Vertex and index data (may be reordered for cluster locality)
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Per-cluster groups for LOD hierarchy
    uint32_t totalTriangles = 0;
    uint32_t totalClusters = 0;

    // DAG metadata
    uint32_t leafClusterCount = 0;  // Number of LOD 0 clusters
    uint32_t dagLevels = 0;         // Total hierarchy depth
    uint32_t rootClusterIndex = 0;  // Index of the root cluster (coarsest LOD)
};

/**
 * MeshClusterBuilder - Splits meshes into GPU-friendly clusters
 *
 * Takes an arbitrary triangle mesh and produces:
 * - 64-128 triangle clusters with bounding data
 * - Spatially coherent triangle ordering within clusters
 * - Normal cones for backface cluster culling
 *
 * This is a CPU preprocessing step done once per mesh.
 *
 * Usage:
 *   MeshClusterBuilder builder;
 *   builder.setTargetClusterSize(64);
 *   auto result = builder.build(mesh.getVertices(), mesh.getIndices());
 */
class MeshClusterBuilder {
public:
    static constexpr uint32_t DEFAULT_CLUSTER_SIZE = 64;  // triangles per cluster
    static constexpr uint32_t MIN_CLUSTER_SIZE = 32;
    static constexpr uint32_t MAX_CLUSTER_SIZE = 128;

    void setTargetClusterSize(uint32_t trianglesPerCluster);

    /**
     * Build clusters from mesh vertex/index data.
     * @param vertices Mesh vertices
     * @param indices  Mesh indices (triangle list)
     * @param meshId   ID to tag clusters with
     * @return ClusteredMesh with cluster data
     */
    ClusteredMesh build(const std::vector<Vertex>& vertices,
                        const std::vector<uint32_t>& indices,
                        uint32_t meshId = 0);

    /**
     * Build clusters AND a DAG hierarchy for LOD selection.
     * First builds leaf clusters, then iteratively groups and simplifies
     * them into coarser parent clusters using meshoptimizer.
     *
     * @param vertices Mesh vertices
     * @param indices  Mesh indices (triangle list)
     * @param meshId   ID to tag clusters with
     * @return ClusteredMesh with full DAG hierarchy
     */
    ClusteredMesh buildWithDAG(const std::vector<Vertex>& vertices,
                               const std::vector<uint32_t>& indices,
                               uint32_t meshId = 0);

private:
    // Compute bounding sphere from a set of points
    static glm::vec4 computeBoundingSphere(const std::vector<Vertex>& vertices,
                                            const std::vector<uint32_t>& indices,
                                            uint32_t firstIndex, uint32_t indexCount);

    // Compute AABB from a set of triangles
    static void computeAABB(const std::vector<Vertex>& vertices,
                             const std::vector<uint32_t>& indices,
                             uint32_t firstIndex, uint32_t indexCount,
                             glm::vec3& outMin, glm::vec3& outMax);

    // Compute normal cone for backface cluster culling
    static void computeNormalCone(const std::vector<Vertex>& vertices,
                                   const std::vector<uint32_t>& indices,
                                   uint32_t firstIndex, uint32_t indexCount,
                                   glm::vec3& outAxis, float& outAngle);

    // Group clusters spatially for DAG level building.
    // Returns groups of cluster indices (2-4 per group).
    static std::vector<std::vector<uint32_t>> groupClustersSpatially(
        const std::vector<MeshCluster>& clusters,
        const std::vector<uint32_t>& clusterIndices);

    // Merge a group of clusters' geometry and simplify into a single parent cluster.
    // Appends new vertices/indices to the mesh and returns the parent cluster.
    static MeshCluster simplifyClusterGroup(
        const std::vector<uint32_t>& groupIndices,
        const std::vector<MeshCluster>& clusters,
        std::vector<Vertex>& vertices,
        std::vector<uint32_t>& indices,
        uint32_t meshId,
        uint32_t lodLevel,
        uint32_t targetTriangles,
        float& outError);

    uint32_t targetClusterSize_ = DEFAULT_CLUSTER_SIZE;
};

/**
 * GPUClusterBuffer - Manages GPU-side cluster data
 *
 * Holds the global vertex buffer, index buffer, and cluster metadata buffer
 * for all clustered meshes in the scene.
 */
class GPUClusterBuffer {
public:
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit GPUClusterBuffer(ConstructToken) {}

    struct InitInfo {
        VmaAllocator allocator;
        VkDevice device;
        VkCommandPool commandPool;
        VkQueue queue;
        uint32_t maxClusters;       // Max total clusters across all meshes
        uint32_t maxVertices;       // Max total vertices
        uint32_t maxIndices;        // Max total indices
    };

    static std::unique_ptr<GPUClusterBuffer> create(const InitInfo& info);

    ~GPUClusterBuffer() = default;

    // Non-copyable, non-movable
    GPUClusterBuffer(const GPUClusterBuffer&) = delete;
    GPUClusterBuffer& operator=(const GPUClusterBuffer&) = delete;

    /**
     * Upload a clustered mesh to the GPU buffers.
     * Returns the base cluster index for this mesh.
     * Returns UINT32_MAX on failure.
     */
    uint32_t uploadMesh(const ClusteredMesh& mesh);

    // Buffer accessors for binding
    VkBuffer getVertexBuffer() const { return vertexBuffer_.get(); }
    VkBuffer getIndexBuffer() const { return indexBuffer_.get(); }
    VkBuffer getClusterBuffer() const { return clusterBuffer_.get(); }

    uint32_t getTotalClusters() const { return totalClusters_; }
    uint32_t getTotalVertices() const { return totalVertices_; }
    uint32_t getTotalIndices() const { return totalIndices_; }

private:
    bool initInternal(const InitInfo& info);

    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;

    // Global buffers
    ManagedBuffer vertexBuffer_;    // All vertices from all clustered meshes
    ManagedBuffer indexBuffer_;     // All indices
    ManagedBuffer clusterBuffer_;   // MeshCluster array (SSBO)

    uint32_t maxClusters_ = 0;
    uint32_t maxVertices_ = 0;
    uint32_t maxIndices_ = 0;
    uint32_t totalClusters_ = 0;
    uint32_t totalVertices_ = 0;
    uint32_t totalIndices_ = 0;
};
