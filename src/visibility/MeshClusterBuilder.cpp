#include "MeshClusterBuilder.h"
#include <SDL3/SDL_log.h>
#include <meshoptimizer.h>
#include <algorithm>
#include <cstring>
#include <numeric>
#include <unordered_set>

// ============================================================================
// MeshClusterBuilder
// ============================================================================

void MeshClusterBuilder::setTargetClusterSize(uint32_t trianglesPerCluster) {
    targetClusterSize_ = std::clamp(trianglesPerCluster, MIN_CLUSTER_SIZE, MAX_CLUSTER_SIZE);
}

ClusteredMesh MeshClusterBuilder::build(const std::vector<Vertex>& vertices,
                                         const std::vector<uint32_t>& indices,
                                         uint32_t meshId) {
    ClusteredMesh result;
    result.vertices = vertices;
    result.indices = indices;

    uint32_t totalTriangles = static_cast<uint32_t>(indices.size()) / 3;
    uint32_t trianglesPerCluster = targetClusterSize_;

    // Simple linear partitioning of triangles into clusters
    // A more sophisticated approach would use spatial partitioning (e.g., k-d tree)
    // but linear is a good starting point and preserves mesh locality
    uint32_t numClusters = (totalTriangles + trianglesPerCluster - 1) / trianglesPerCluster;

    result.clusters.reserve(numClusters);
    result.totalTriangles = totalTriangles;
    result.totalClusters = numClusters;

    for (uint32_t c = 0; c < numClusters; ++c) {
        uint32_t firstTriangle = c * trianglesPerCluster;
        uint32_t clusterTriangles = std::min(trianglesPerCluster, totalTriangles - firstTriangle);
        uint32_t firstIndex = firstTriangle * 3;
        uint32_t indexCount = clusterTriangles * 3;

        MeshCluster cluster{};
        cluster.firstIndex = firstIndex;
        cluster.indexCount = indexCount;
        cluster.firstVertex = 0;  // All clusters share the same vertex buffer
        cluster.meshId = meshId;
        cluster.lodLevel = 0;
        cluster.parentError = 0.0f;
        cluster.error = 0.0f;
        cluster.parentIndex = UINT32_MAX;
        cluster.firstChildIndex = 0;
        cluster.childCount = 0;

        // Compute bounding data
        cluster.boundingSphere = computeBoundingSphere(vertices, indices, firstIndex, indexCount);
        computeAABB(vertices, indices, firstIndex, indexCount, cluster.aabbMin, cluster.aabbMax);
        computeNormalCone(vertices, indices, firstIndex, indexCount, cluster.coneAxis, cluster.coneAngle);

        result.clusters.push_back(cluster);
    }

    SDL_Log("MeshClusterBuilder: Built %u clusters from %u triangles (target %u tri/cluster)",
            numClusters, totalTriangles, trianglesPerCluster);

    return result;
}

glm::vec4 MeshClusterBuilder::computeBoundingSphere(const std::vector<Vertex>& vertices,
                                                      const std::vector<uint32_t>& indices,
                                                      uint32_t firstIndex, uint32_t indexCount) {
    if (indexCount == 0) return glm::vec4(0.0f);

    // Compute center as centroid of all referenced vertices
    glm::vec3 center(0.0f);
    for (uint32_t i = firstIndex; i < firstIndex + indexCount; ++i) {
        center += vertices[indices[i]].position;
    }
    center /= static_cast<float>(indexCount);

    // Find maximum distance from center
    float maxDist2 = 0.0f;
    for (uint32_t i = firstIndex; i < firstIndex + indexCount; ++i) {
        float dist2 = glm::dot(vertices[indices[i]].position - center,
                                vertices[indices[i]].position - center);
        maxDist2 = std::max(maxDist2, dist2);
    }

    return glm::vec4(center, std::sqrt(maxDist2));
}

void MeshClusterBuilder::computeAABB(const std::vector<Vertex>& vertices,
                                       const std::vector<uint32_t>& indices,
                                       uint32_t firstIndex, uint32_t indexCount,
                                       glm::vec3& outMin, glm::vec3& outMax) {
    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (uint32_t i = firstIndex; i < firstIndex + indexCount; ++i) {
        const auto& pos = vertices[indices[i]].position;
        outMin = glm::min(outMin, pos);
        outMax = glm::max(outMax, pos);
    }
}

void MeshClusterBuilder::computeNormalCone(const std::vector<Vertex>& vertices,
                                             const std::vector<uint32_t>& indices,
                                             uint32_t firstIndex, uint32_t indexCount,
                                             glm::vec3& outAxis, float& outAngle) {
    // Compute average normal direction
    glm::vec3 avgNormal(0.0f);
    uint32_t triCount = indexCount / 3;
    for (uint32_t t = 0; t < triCount; ++t) {
        uint32_t base = firstIndex + t * 3;
        const auto& v0 = vertices[indices[base + 0]].position;
        const auto& v1 = vertices[indices[base + 1]].position;
        const auto& v2 = vertices[indices[base + 2]].position;

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 faceNormal = glm::cross(edge1, edge2);
        float area = glm::length(faceNormal);
        if (area > 1e-8f) {
            avgNormal += faceNormal;  // Area-weighted
        }
    }

    float len = glm::length(avgNormal);
    if (len < 1e-8f) {
        outAxis = glm::vec3(0.0f, 1.0f, 0.0f);
        outAngle = -1.0f;  // Degenerate - don't cull
        return;
    }
    outAxis = avgNormal / len;

    // Find max deviation from average normal
    float minCos = 1.0f;
    for (uint32_t t = 0; t < triCount; ++t) {
        uint32_t base = firstIndex + t * 3;
        const auto& v0 = vertices[indices[base + 0]].position;
        const auto& v1 = vertices[indices[base + 1]].position;
        const auto& v2 = vertices[indices[base + 2]].position;

        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));
        float cosAngle = glm::dot(faceNormal, outAxis);
        minCos = std::min(minCos, cosAngle);
    }

    outAngle = minCos;  // cos(half-angle) - higher = tighter cone
}

// ============================================================================
// DAG Builder
// ============================================================================

ClusteredMesh MeshClusterBuilder::buildWithDAG(const std::vector<Vertex>& vertices,
                                                const std::vector<uint32_t>& indices,
                                                uint32_t meshId) {
    // Step 1: Build leaf clusters (LOD 0)
    ClusteredMesh result = build(vertices, indices, meshId);

    uint32_t leafCount = result.totalClusters;
    result.leafClusterCount = leafCount;

    // Need at least 2 clusters to build a hierarchy
    if (leafCount < 2) {
        result.dagLevels = 1;
        result.rootClusterIndex = 0;
        if (!result.clusters.empty()) {
            result.clusters[0].parentIndex = UINT32_MAX;
        }
        SDL_Log("MeshClusterBuilder: DAG trivial (1 cluster, no hierarchy needed)");
        return result;
    }

    // Step 2: Iteratively build DAG levels
    // currentLevel holds indices into result.clusters for the clusters at this level
    std::vector<uint32_t> currentLevel(leafCount);
    std::iota(currentLevel.begin(), currentLevel.end(), 0);

    uint32_t lodLevel = 0;
    uint32_t targetTrisPerParent = targetClusterSize_;

    while (currentLevel.size() > 1) {
        lodLevel++;

        // Group spatially adjacent clusters
        auto groups = groupClustersSpatially(result.clusters, currentLevel);

        std::vector<uint32_t> nextLevel;
        nextLevel.reserve(groups.size());

        for (const auto& group : groups) {
            if (group.size() == 1) {
                // Single cluster can't be grouped further — promote as-is
                // Mark it as its own "parent" at the next level
                nextLevel.push_back(group[0]);
                continue;
            }

            // Simplify the group into a parent cluster
            float simplifyError = 0.0f;
            MeshCluster parent = simplifyClusterGroup(
                group, result.clusters,
                result.vertices, result.indices,
                meshId, lodLevel, targetTrisPerParent,
                simplifyError);

            uint32_t parentIdx = static_cast<uint32_t>(result.clusters.size());
            parent.parentIndex = UINT32_MAX;  // Will be set by next level
            parent.firstChildIndex = group[0]; // Record first child for reference
            parent.childCount = static_cast<uint32_t>(group.size());
            parent.error = simplifyError;

            // Wire up parent-child relationships
            for (uint32_t childIdx : group) {
                result.clusters[childIdx].parentIndex = parentIdx;
                result.clusters[childIdx].parentError = simplifyError;
            }

            result.clusters.push_back(parent);
            nextLevel.push_back(parentIdx);
        }

        // If we didn't reduce the count, force-merge remaining into one
        if (nextLevel.size() >= currentLevel.size()) {
            SDL_Log("MeshClusterBuilder: DAG stopped at level %u (no further reduction from %zu clusters)",
                    lodLevel, currentLevel.size());
            break;
        }

        currentLevel = std::move(nextLevel);
    }

    // The last remaining cluster is the root
    result.rootClusterIndex = currentLevel[0];
    result.dagLevels = lodLevel + 1;
    result.totalClusters = static_cast<uint32_t>(result.clusters.size());

    SDL_Log("MeshClusterBuilder: DAG built with %u levels, %u total clusters (%u leaf + %u internal), root=%u",
            result.dagLevels, result.totalClusters, result.leafClusterCount,
            result.totalClusters - result.leafClusterCount, result.rootClusterIndex);

    return result;
}

std::vector<std::vector<uint32_t>> MeshClusterBuilder::groupClustersSpatially(
    const std::vector<MeshCluster>& clusters,
    const std::vector<uint32_t>& clusterIndices) {

    std::vector<std::vector<uint32_t>> groups;
    if (clusterIndices.empty()) return groups;

    // Compute centroids for each cluster
    struct CentroidEntry {
        uint32_t clusterIdx;
        glm::vec3 centroid;
    };
    std::vector<CentroidEntry> entries;
    entries.reserve(clusterIndices.size());
    for (uint32_t idx : clusterIndices) {
        entries.push_back({idx, glm::vec3(clusters[idx].boundingSphere)});
    }

    // Greedy nearest-neighbor grouping: pick an ungrouped cluster,
    // find its nearest 1-3 ungrouped neighbors, form a group
    std::vector<bool> used(entries.size(), false);
    constexpr uint32_t MAX_GROUP_SIZE = 4;
    constexpr uint32_t TARGET_GROUP_SIZE = 2;

    for (size_t i = 0; i < entries.size(); ++i) {
        if (used[i]) continue;

        std::vector<uint32_t> group;
        group.push_back(entries[i].clusterIdx);
        used[i] = true;

        glm::vec3 groupCenter = entries[i].centroid;

        // Find nearest neighbors
        for (uint32_t g = 1; g < MAX_GROUP_SIZE; ++g) {
            float bestDist2 = std::numeric_limits<float>::max();
            size_t bestJ = SIZE_MAX;

            for (size_t j = 0; j < entries.size(); ++j) {
                if (used[j]) continue;
                glm::vec3 diff = entries[j].centroid - groupCenter;
                float dist2 = glm::dot(diff, diff);
                if (dist2 < bestDist2) {
                    bestDist2 = dist2;
                    bestJ = j;
                }
            }

            if (bestJ == SIZE_MAX) break;

            // Only add if reasonably close (within 3x the current group radius)
            // This prevents merging very distant clusters
            if (g >= TARGET_GROUP_SIZE) {
                float groupRadius = clusters[group[0]].boundingSphere.w;
                for (size_t k = 1; k < group.size(); ++k) {
                    groupRadius = std::max(groupRadius, clusters[group[k]].boundingSphere.w);
                }
                if (bestDist2 > 9.0f * groupRadius * groupRadius && g >= TARGET_GROUP_SIZE) {
                    break;
                }
            }

            group.push_back(entries[bestJ].clusterIdx);
            used[bestJ] = true;

            // Update group center
            groupCenter = glm::vec3(0.0f);
            for (uint32_t idx : group) {
                groupCenter += glm::vec3(clusters[idx].boundingSphere);
            }
            groupCenter /= static_cast<float>(group.size());
        }

        groups.push_back(std::move(group));
    }

    return groups;
}

MeshCluster MeshClusterBuilder::simplifyClusterGroup(
    const std::vector<uint32_t>& groupIndices,
    const std::vector<MeshCluster>& clusters,
    std::vector<Vertex>& vertices,
    std::vector<uint32_t>& indices,
    uint32_t meshId,
    uint32_t lodLevel,
    uint32_t targetTriangles,
    float& outError) {

    // Collect all vertices referenced by the group's clusters
    // We need to remap indices to a local vertex set for meshoptimizer
    std::unordered_set<uint32_t> usedVertexSet;
    uint32_t totalGroupIndices = 0;
    for (uint32_t ci : groupIndices) {
        const auto& c = clusters[ci];
        for (uint32_t i = c.firstIndex; i < c.firstIndex + c.indexCount; ++i) {
            usedVertexSet.insert(indices[i]);
        }
        totalGroupIndices += c.indexCount;
    }

    // Build local vertex buffer and index remapping
    std::vector<uint32_t> globalToLocal(vertices.size(), UINT32_MAX);
    std::vector<Vertex> localVertices;
    localVertices.reserve(usedVertexSet.size());

    for (uint32_t vi : usedVertexSet) {
        globalToLocal[vi] = static_cast<uint32_t>(localVertices.size());
        localVertices.push_back(vertices[vi]);
    }

    // Build local index buffer
    std::vector<uint32_t> localIndices;
    localIndices.reserve(totalGroupIndices);
    for (uint32_t ci : groupIndices) {
        const auto& c = clusters[ci];
        for (uint32_t i = c.firstIndex; i < c.firstIndex + c.indexCount; ++i) {
            localIndices.push_back(globalToLocal[indices[i]]);
        }
    }

    // Simplify using meshoptimizer
    uint32_t targetIndexCount = targetTriangles * 3;
    // Don't go below 12 indices (4 triangles) - need at least something visible
    targetIndexCount = std::max(targetIndexCount, 12u);
    // But also cap at the number of input indices
    targetIndexCount = std::min(targetIndexCount, static_cast<uint32_t>(localIndices.size()));

    float targetError = 0.05f;  // Allow up to 5% error
    float resultError = 0.0f;

    std::vector<uint32_t> simplifiedIndices(localIndices.size());
    size_t simplifiedCount = meshopt_simplify(
        simplifiedIndices.data(),
        localIndices.data(),
        localIndices.size(),
        &localVertices[0].position.x,
        localVertices.size(),
        sizeof(Vertex),
        targetIndexCount,
        targetError,
        0,  // no options
        &resultError);

    simplifiedIndices.resize(simplifiedCount);

    // If meshopt_simplify didn't reduce enough, try sloppy mode
    if (simplifiedCount > targetIndexCount * 2) {
        size_t sloppyCount = meshopt_simplifySloppy(
            simplifiedIndices.data(),
            localIndices.data(),
            localIndices.size(),
            &localVertices[0].position.x,
            localVertices.size(),
            sizeof(Vertex),
            targetIndexCount,
            targetError,
            &resultError);
        if (sloppyCount > 0 && sloppyCount < simplifiedCount) {
            simplifiedIndices.resize(sloppyCount);
            simplifiedCount = sloppyCount;
        }
    }

    outError = resultError;

    // Optimize vertex cache for the simplified mesh
    meshopt_optimizeVertexCache(
        simplifiedIndices.data(),
        simplifiedIndices.data(),
        simplifiedIndices.size(),
        localVertices.size());

    // Append the simplified geometry to the global buffers
    uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
    uint32_t baseIndex = static_cast<uint32_t>(indices.size());

    // We only need to append vertices that are actually used by the simplified mesh
    std::vector<uint32_t> localToGlobal(localVertices.size(), UINT32_MAX);
    uint32_t newVertexCount = 0;
    for (uint32_t si : simplifiedIndices) {
        if (localToGlobal[si] == UINT32_MAX) {
            localToGlobal[si] = baseVertex + newVertexCount;
            vertices.push_back(localVertices[si]);
            newVertexCount++;
        }
    }

    // Remap and append indices
    for (uint32_t si : simplifiedIndices) {
        indices.push_back(localToGlobal[si]);
    }

    // Build the parent cluster
    uint32_t parentFirstIndex = baseIndex;
    uint32_t parentIndexCount = static_cast<uint32_t>(simplifiedIndices.size());

    MeshCluster parent{};
    parent.firstIndex = parentFirstIndex;
    parent.indexCount = parentIndexCount;
    parent.firstVertex = 0;  // Global vertex buffer
    parent.meshId = meshId;
    parent.lodLevel = lodLevel;
    parent.error = resultError;
    parent.parentError = 0.0f;  // Set by next level
    parent.parentIndex = UINT32_MAX;
    parent.firstChildIndex = 0;
    parent.childCount = 0;

    // Compute bounding data from the simplified geometry
    parent.boundingSphere = computeBoundingSphere(vertices, indices, parentFirstIndex, parentIndexCount);
    computeAABB(vertices, indices, parentFirstIndex, parentIndexCount, parent.aabbMin, parent.aabbMax);
    computeNormalCone(vertices, indices, parentFirstIndex, parentIndexCount, parent.coneAxis, parent.coneAngle);

    return parent;
}

// ============================================================================
// GPUClusterBuffer
// ============================================================================

std::unique_ptr<GPUClusterBuffer> GPUClusterBuffer::create(const InitInfo& info) {
    auto buffer = std::make_unique<GPUClusterBuffer>(ConstructToken{});
    if (!buffer->initInternal(info)) {
        return nullptr;
    }
    return buffer;
}

bool GPUClusterBuffer::initInternal(const InitInfo& info) {
    allocator_ = info.allocator;
    device_ = info.device;
    commandPool_ = info.commandPool;
    queue_ = info.queue;
    maxClusters_ = info.maxClusters;
    maxVertices_ = info.maxVertices;
    maxIndices_ = info.maxIndices;

    // Create device-local buffers sized for max capacity
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    // Vertex buffer
    bufInfo.size = maxVertices_ * sizeof(Vertex);
    bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!ManagedBuffer::create(allocator_, bufInfo, allocInfo, vertexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUClusterBuffer: Failed to create vertex buffer");
        return false;
    }

    // Index buffer
    bufInfo.size = maxIndices_ * sizeof(uint32_t);
    bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!ManagedBuffer::create(allocator_, bufInfo, allocInfo, indexBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUClusterBuffer: Failed to create index buffer");
        return false;
    }

    // Cluster metadata buffer
    bufInfo.size = maxClusters_ * sizeof(MeshCluster);
    bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!ManagedBuffer::create(allocator_, bufInfo, allocInfo, clusterBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUClusterBuffer: Failed to create cluster buffer");
        return false;
    }

    SDL_Log("GPUClusterBuffer: Created (maxClusters=%u, maxVertices=%u, maxIndices=%u)",
            maxClusters_, maxVertices_, maxIndices_);
    return true;
}

uint32_t GPUClusterBuffer::uploadMesh(const ClusteredMesh& mesh) {
    if (totalClusters_ + mesh.totalClusters > maxClusters_ ||
        totalVertices_ + mesh.vertices.size() > maxVertices_ ||
        totalIndices_ + mesh.indices.size() > maxIndices_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GPUClusterBuffer: Not enough space (clusters: %u+%u/%u, vertices: %u+%zu/%u, indices: %u+%zu/%u)",
            totalClusters_, mesh.totalClusters, maxClusters_,
            totalVertices_, mesh.vertices.size(), maxVertices_,
            totalIndices_, mesh.indices.size(), maxIndices_);
        return UINT32_MAX;
    }

    uint32_t baseCluster = totalClusters_;
    uint32_t baseVertex = totalVertices_;
    uint32_t baseIndex = totalIndices_;

    // Create staging buffers and upload
    VkDeviceSize vertexSize = mesh.vertices.size() * sizeof(Vertex);
    VkDeviceSize indexSize = mesh.indices.size() * sizeof(uint32_t);
    VkDeviceSize clusterSize = mesh.clusters.size() * sizeof(MeshCluster);

    // Adjust cluster offsets for global buffer positioning
    std::vector<MeshCluster> adjustedClusters = mesh.clusters;
    for (auto& cluster : adjustedClusters) {
        cluster.firstIndex += baseIndex;
        cluster.firstVertex += static_cast<int32_t>(baseVertex);

        // Adjust DAG connectivity indices to global cluster buffer positions
        if (cluster.parentIndex != UINT32_MAX) {
            cluster.parentIndex += baseCluster;
        }
        if (cluster.childCount > 0) {
            cluster.firstChildIndex += baseCluster;
        }
    }

    // Create staging buffer for all three uploads
    VkDeviceSize totalStagingSize = vertexSize + indexSize + clusterSize;

    VkBufferCreateInfo stagingBufInfo{};
    stagingBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufInfo.size = totalStagingSize;
    stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                              VMA_ALLOCATION_CREATE_MAPPED_BIT;

    ManagedBuffer stagingBuffer;
    if (!ManagedBuffer::create(allocator_, stagingBufInfo, stagingAllocInfo, stagingBuffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUClusterBuffer: Failed to create staging buffer");
        return UINT32_MAX;
    }

    // Map and copy data
    void* mapped = stagingBuffer.map();
    if (!mapped) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUClusterBuffer: Failed to map staging buffer");
        return UINT32_MAX;
    }

    auto* ptr = static_cast<uint8_t*>(mapped);
    memcpy(ptr, mesh.vertices.data(), vertexSize);
    ptr += vertexSize;
    memcpy(ptr, mesh.indices.data(), indexSize);
    ptr += indexSize;
    memcpy(ptr, adjustedClusters.data(), clusterSize);

    // Record copy commands
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = commandPool_;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &cmdAllocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkDeviceSize stagingOffset = 0;

    // Copy vertices
    VkBufferCopy vertexCopy{};
    vertexCopy.srcOffset = stagingOffset;
    vertexCopy.dstOffset = baseVertex * sizeof(Vertex);
    vertexCopy.size = vertexSize;
    vkCmdCopyBuffer(cmd, stagingBuffer.get(), vertexBuffer_.get(), 1, &vertexCopy);
    stagingOffset += vertexSize;

    // Copy indices
    VkBufferCopy indexCopy{};
    indexCopy.srcOffset = stagingOffset;
    indexCopy.dstOffset = baseIndex * sizeof(uint32_t);
    indexCopy.size = indexSize;
    vkCmdCopyBuffer(cmd, stagingBuffer.get(), indexBuffer_.get(), 1, &indexCopy);
    stagingOffset += indexSize;

    // Copy cluster metadata
    VkBufferCopy clusterCopy{};
    clusterCopy.srcOffset = stagingOffset;
    clusterCopy.dstOffset = baseCluster * sizeof(MeshCluster);
    clusterCopy.size = clusterSize;
    vkCmdCopyBuffer(cmd, stagingBuffer.get(), clusterBuffer_.get(), 1, &clusterCopy);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

    // Update totals
    totalClusters_ += mesh.totalClusters;
    totalVertices_ += static_cast<uint32_t>(mesh.vertices.size());
    totalIndices_ += static_cast<uint32_t>(mesh.indices.size());

    SDL_Log("GPUClusterBuffer: Uploaded mesh (%u clusters, %zu vertices, %zu indices)",
            mesh.totalClusters, mesh.vertices.size(), mesh.indices.size());

    return baseCluster;
}
