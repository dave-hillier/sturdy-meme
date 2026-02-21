#include "TreeSystem.h"
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <filesystem>
#include <cstring>

namespace {
// Compute full tree bounds including both branches and leaves
AABB computeFullTreeBounds(const Mesh& branchMesh, const std::vector<LeafInstanceGPU>& leafInstances) {
    // Start with branch mesh bounds
    AABB bounds = branchMesh.getBounds();

    // Expand to include all leaf instances (accounting for leaf size)
    for (const auto& leaf : leafInstances) {
        glm::vec3 leafPos = glm::vec3(leaf.positionAndSize);
        float leafSize = leaf.positionAndSize.w;

        // Expand bounds to include leaf quad extents
        bounds.expand(leafPos - glm::vec3(leafSize));
        bounds.expand(leafPos + glm::vec3(leafSize));
    }

    return bounds;
}
} // anonymous namespace

std::unique_ptr<TreeSystem> TreeSystem::create(const InitInfo& info) {
    auto system = std::make_unique<TreeSystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

TreeSystem::~TreeSystem() {
    cleanup();
}

bool TreeSystem::initInternal(const InitInfo& info) {
    SDL_Log("TreeSystem::init() starting");

    storedDevice_ = info.device;
    storedAllocator_ = info.allocator;
    storedCommandPool_ = info.commandPool;
    storedQueue_ = info.graphicsQueue;
    storedPhysicalDevice_ = info.physicalDevice;
    storedResourcePath_ = info.resourcePath;

    // Load textures
    if (!loadTextures(info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSystem: Failed to load textures");
        return false;
    }

    // Create shared leaf quad mesh for instanced rendering
    if (!createSharedLeafQuadMesh()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSystem: Failed to create shared leaf quad mesh");
        return false;
    }

    // Load default options from preset if available
    std::string presetsPath = info.resourcePath + "/assets/trees/presets/";
    std::string oakPath = presetsPath + "oak_large.json";
    if (std::filesystem::exists(oakPath)) {
        defaultOptions_ = TreeOptions::loadFromJson(oakPath);
    } else {
        defaultOptions_ = TreeOptions::defaultOak();
    }

    // Trees are added via addTree() from initialization
    if (!treeInstances_.empty()) {
        selectedTreeIndex_ = 0;
    }

    SDL_Log("TreeSystem::init() complete - %zu trees created", treeInstances_.size());
    return true;
}

ecs::Entity TreeSystem::createTreeEntity(uint32_t treeIdx, const TreeInstanceData& instance,
                                          const TreeOptions& opts, const AABB& bounds) {
    if (!world_) return ecs::NullEntity;

    ecs::Entity e = world_->create();
    world_->add<ecs::TreeTag>(e);
    world_->add<ecs::Transform>(e, instance.getTransformMatrix());
    world_->add<ecs::TreeData>(e, ecs::TreeData{
        static_cast<int>(instance.meshIndex),
        static_cast<int>(treeIdx),
        opts.leaves.tint,
        opts.leaves.autumnHueShift
    });
    world_->add<ecs::BarkType>(e, ecs::BarkType{0, opts.bark.type});
    world_->add<ecs::LeafType>(e, ecs::LeafType{0, opts.leaves.type});
    world_->add<ecs::MeshRef>(e, &branchMeshes_[instance.meshIndex]);
    world_->add<ecs::CastsShadow>(e);

    // Compute bounding sphere from AABB
    glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
    float radius = glm::length(bounds.max - center);
    // Transform to world space
    glm::vec3 worldCenter = glm::vec3(instance.getTransformMatrix() * glm::vec4(center, 1.0f));
    float worldRadius = radius * instance.scale();
    world_->add<ecs::BoundingSphere>(e, worldCenter, worldRadius);

    return e;
}

void TreeSystem::destroyTreeEntity(uint32_t index) {
    if (!world_ || index >= treeEntities_.size()) return;
    if (treeEntities_[index] != ecs::NullEntity) {
        world_->destroy(treeEntities_[index]);
    }
}

void TreeSystem::refreshMeshRefs() {
    if (!world_) return;
    for (size_t i = 0; i < treeEntities_.size() && i < treeInstances_.size(); ++i) {
        ecs::Entity e = treeEntities_[i];
        if (e == ecs::NullEntity) continue;
        uint32_t meshIndex = treeInstances_[i].meshIndex;
        if (meshIndex < branchMeshes_.size()) {
            if (auto* ref = world_->tryGet<ecs::MeshRef>(e)) {
                ref->mesh = &branchMeshes_[meshIndex];
            }
        }
    }
}

void TreeSystem::cleanup() {
    if (storedDevice_ == VK_NULL_HANDLE) return;

    // Destroy all ECS entities
    if (world_) {
        for (auto e : treeEntities_) {
            if (e != ecs::NullEntity) {
                world_->destroy(e);
            }
        }
    }
    treeEntities_.clear();

    // RAII-managed textures - just reset the maps
    barkTextures_.clear();
    barkNormalMaps_.clear();
    leafTextures_.clear();

    // Manually managed mesh vector
    for (auto& mesh : branchMeshes_) {
        mesh.releaseGPUResources();
    }
    branchMeshes_.clear();

    // Shared leaf quad mesh
    sharedLeafQuadMesh_.releaseGPUResources();

    // Leaf instance SSBO
    if (leafInstanceBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(storedAllocator_, leafInstanceBuffer_, leafInstanceAllocation_);
        leafInstanceBuffer_ = VK_NULL_HANDLE;
        leafInstanceAllocation_ = VK_NULL_HANDLE;
        leafInstanceBufferSize_ = 0;
    }
    leafInstancesPerTree_.clear();
    allLeafInstances_.clear();
    leafDrawInfoPerTree_.clear();

    treeInstances_.clear();
    treeOptions_.clear();
    treeMeshData_.clear();
    fullTreeBounds_.clear();
}

bool TreeSystem::loadTextures(const InitInfo& info) {
    std::string texturePath = info.resourcePath + "/textures/";

    // Bark type names (data-driven from JSON presets)
    std::vector<std::string> barkTypeNames = {"birch", "oak", "pine", "willow"};

    // Load all bark textures
    for (const auto& typeName : barkTypeNames) {
        std::string path = texturePath + "bark/" + typeName + "_color_1k.jpg";
        barkTextures_[typeName] = Texture::loadFromFile(path, info.allocator, info.device,
                                                         info.commandPool, info.graphicsQueue, info.physicalDevice);
        if (!barkTextures_[typeName]) {
            SDL_Log("TreeSystem: Using placeholder for %s bark texture", typeName.c_str());
            barkTextures_[typeName] = Texture::createSolidColor(102, 77, 51, 255, info.allocator, info.device,
                                                                  info.commandPool, info.graphicsQueue);
            if (!barkTextures_[typeName]) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create bark texture %s", typeName.c_str());
                return false;
            }
        } else {
            SDL_Log("TreeSystem: Loaded bark texture: %s", path.c_str());
        }
    }

    // Load all bark normal maps
    for (const auto& typeName : barkTypeNames) {
        std::string path = texturePath + "bark/" + typeName + "_normal_1k.jpg";
        barkNormalMaps_[typeName] = Texture::loadFromFile(path, info.allocator, info.device,
                                                           info.commandPool, info.graphicsQueue, info.physicalDevice, false);
        if (!barkNormalMaps_[typeName]) {
            SDL_Log("TreeSystem: Using placeholder for %s bark normal", typeName.c_str());
            barkNormalMaps_[typeName] = Texture::createSolidColor(128, 128, 255, 255, info.allocator, info.device,
                                                                    info.commandPool, info.graphicsQueue);
            if (!barkNormalMaps_[typeName]) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create bark normal %s", typeName.c_str());
                return false;
            }
        } else {
            SDL_Log("TreeSystem: Loaded bark normal: %s", path.c_str());
        }
    }

    // Leaf type names (data-driven from JSON presets)
    std::vector<std::string> leafTypeNames = {"ash", "aspen", "pine", "oak"};

    // Load all leaf textures with mipmaps for proper filtering at distance
    for (const auto& typeName : leafTypeNames) {
        std::string path = texturePath + "leaves/" + typeName + "_color.png";
        leafTextures_[typeName] = Texture::loadFromFileWithMipmaps(path, info.allocator, info.device,
                                                                     info.commandPool, info.graphicsQueue, info.physicalDevice);
        if (!leafTextures_[typeName]) {
            SDL_Log("TreeSystem: Using placeholder for %s leaf texture", typeName.c_str());
            leafTextures_[typeName] = Texture::createSolidColor(51, 102, 51, 200, info.allocator, info.device,
                                                                  info.commandPool, info.graphicsQueue);
            if (!leafTextures_[typeName]) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create leaf texture %s", typeName.c_str());
                return false;
            }
        } else {
            SDL_Log("TreeSystem: Loaded leaf texture with mipmaps: %s", path.c_str());
        }
    }

    return true;
}

Texture* TreeSystem::getBarkTexture(const std::string& type) const {
    auto it = barkTextures_.find(type);
    if (it != barkTextures_.end() && it->second) {
        return it->second.get();
    }
    // Fallback to oak if type not found
    it = barkTextures_.find("oak");
    return (it != barkTextures_.end() && it->second) ? it->second.get() : nullptr;
}

Texture* TreeSystem::getBarkNormalMap(const std::string& type) const {
    auto it = barkNormalMaps_.find(type);
    if (it != barkNormalMaps_.end() && it->second) {
        return it->second.get();
    }
    // Fallback to oak if type not found
    it = barkNormalMaps_.find("oak");
    return (it != barkNormalMaps_.end() && it->second) ? it->second.get() : nullptr;
}

Texture* TreeSystem::getLeafTexture(const std::string& type) const {
    auto it = leafTextures_.find(type);
    if (it != leafTextures_.end() && it->second) {
        return it->second.get();
    }
    // Fallback to oak if type not found
    it = leafTextures_.find("oak");
    return (it != leafTextures_.end() && it->second) ? it->second.get() : nullptr;
}

std::vector<std::string> TreeSystem::getBarkTextureTypes() const {
    std::vector<std::string> types;
    for (const auto& [name, tex] : barkTextures_) {
        types.push_back(name);
    }
    return types;
}

std::vector<std::string> TreeSystem::getLeafTextureTypes() const {
    std::vector<std::string> types;
    for (const auto& [name, tex] : leafTextures_) {
        types.push_back(name);
    }
    return types;
}

bool TreeSystem::generateTreeMesh(const TreeOptions& options, Mesh& branchMesh, std::vector<LeafInstanceGPU>& leafInstances,
                                   TreeMeshData* meshDataOut) {
    // Generate tree data
    TreeMeshData meshData = generator_.generate(options);

    SDL_Log("TreeSystem: Generated tree with %zu branches, %zu leaves",
            meshData.branches.size(), meshData.leaves.size());

    // Output mesh data if requested (for collision generation)
    if (meshDataOut) {
        *meshDataOut = meshData;
    }

    // Build branch mesh vertices
    std::vector<Vertex> branchVertices;
    std::vector<uint32_t> branchIndices;

    // Get texture scale from options (applied to UVs)
    // ez-tree uses: repeat.x = scale.x, repeat.y = 1/scale.y
    // So higher Y values = less tiling (stretched texture)
    glm::vec2 textureScale = options.bark.textureScale;
    float vRepeat = 1.0f / textureScale.y;  // Invert Y to match ez-tree semantics

    uint32_t indexOffset = 0;
    for (const auto& branch : meshData.branches) {
        int sectionCount = branch.sectionCount;
        int segmentCount = branch.segmentCount;

        for (size_t sectionIdx = 0; sectionIdx < branch.sections.size(); ++sectionIdx) {
            const SectionData& section = branch.sections[sectionIdx];

            // Match ez-tree: V coordinate alternates 0/vRepeat to tile the bark texture along the branch
            // This creates a repeating pattern rather than stretching the texture
            float vCoord = (sectionIdx % 2 == 0) ? 0.0f : vRepeat;

            for (int seg = 0; seg <= segmentCount; ++seg) {
                float angle = 2.0f * glm::pi<float>() * static_cast<float>(seg) / static_cast<float>(segmentCount);

                // Local position on unit circle
                glm::vec3 localPos(std::cos(angle), 0.0f, std::sin(angle));
                // Negate normal to point outward (matching front-face winding)
                glm::vec3 localNormal = -localPos;

                // Transform by section orientation
                glm::vec3 worldOffset = section.orientation * (localPos * section.radius);
                glm::vec3 worldNormal = glm::normalize(section.orientation * localNormal);

                // U coordinate wraps around the circumference, scaled by textureScale.x
                float uCoord = static_cast<float>(seg) / static_cast<float>(segmentCount) * textureScale.x;

                // Final vertex
                Vertex v{};
                v.position = section.origin + worldOffset;
                v.normal = worldNormal;
                v.texCoord = glm::vec2(uCoord, vCoord);
                v.tangent = glm::vec4(
                    glm::normalize(section.orientation * glm::vec3(0.0f, 1.0f, 0.0f)),
                    1.0f
                );
                // Wind animation data in vertex color:
                // RGB = pivot point (branch origin) for skeletal rotation
                // A = branch level (0-0.95 for levels 0-3) for wind intensity
                // For trunk (level 0), use white RGB so texture renders correctly
                // Cap alpha at 0.95 to distinguish from default color (1,1,1,1)
                float normalizedLevel = static_cast<float>(branch.level) / 3.0f * 0.95f;
                if (branch.level == 0) {
                    // Trunk: no wind animation, use white for proper texture rendering
                    v.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
                } else {
                    v.color = glm::vec4(branch.origin, normalizedLevel);
                }

                branchVertices.push_back(v);
            }
        }

        // Generate indices for this branch
        uint32_t vertsPerRing = static_cast<uint32_t>(segmentCount + 1);
        for (int section = 0; section < sectionCount; ++section) {
            for (int seg = 0; seg < segmentCount; ++seg) {
                uint32_t v0 = indexOffset + section * vertsPerRing + seg;
                uint32_t v1 = v0 + 1;
                uint32_t v2 = v0 + vertsPerRing;
                uint32_t v3 = v2 + 1;

                branchIndices.push_back(v0);
                branchIndices.push_back(v2);
                branchIndices.push_back(v1);

                branchIndices.push_back(v1);
                branchIndices.push_back(v2);
                branchIndices.push_back(v3);
            }
        }

        indexOffset += static_cast<uint32_t>(branch.sections.size()) * vertsPerRing;
    }

    // Build leaf instance data (for instanced rendering)
    // Each leaf generates 1 or 2 instances (for double billboard mode)
    leafInstances.clear();
    for (const auto& leaf : meshData.leaves) {
        // Calculate billboard mode based on options
        int quadsPerLeaf = (options.leaves.billboard == BillboardMode::Double) ? 2 : 1;

        for (int quad = 0; quad < quadsPerLeaf; ++quad) {
            float yRotation = (quad == 1) ? glm::half_pi<float>() : 0.0f;
            glm::quat yQuat = glm::angleAxis(yRotation, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat finalQuat = leaf.orientation * yQuat;

            LeafInstanceGPU instance;
            instance.positionAndSize = glm::vec4(leaf.position, leaf.size);
            instance.orientation = glm::vec4(finalQuat.x, finalQuat.y, finalQuat.z, finalQuat.w);
            leafInstances.push_back(instance);
        }
    }

    // Create GPU meshes
    if (!branchVertices.empty()) {
        branchMesh.setCustomGeometry(branchVertices, branchIndices);
        if (!branchMesh.upload(storedAllocator_, storedDevice_, storedCommandPool_, storedQueue_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload branch mesh");
            return false;
        }
    }

    SDL_Log("TreeSystem: Created branch mesh - %zu verts, %zu indices; %zu leaf instances",
            branchVertices.size(), branchIndices.size(), leafInstances.size());

    return true;
}

bool TreeSystem::createSharedLeafQuadMesh() {
    // Create a single quad mesh (4 vertices, 6 indices) that will be instanced for all leaves
    // The vertex positions are in local space: [-0.5, 0.5] x [0, 1] x [0, 0]
    // The shader will transform each instance using the SSBO data
    std::vector<Vertex> vertices(4);
    std::vector<uint32_t> indices = {0, 2, 1, 0, 3, 2};  // CCW winding

    // Vertex positions match LEAF_QUAD_OFFSETS in tree_leaf_instance.glsl
    glm::vec3 positions[4] = {
        glm::vec3(-0.5f, 1.0f, 0.0f),   // Top-left
        glm::vec3(-0.5f, 0.0f, 0.0f),   // Bottom-left
        glm::vec3( 0.5f, 0.0f, 0.0f),   // Bottom-right
        glm::vec3( 0.5f, 1.0f, 0.0f)    // Top-right
    };

    // UVs match LEAF_QUAD_UVS in tree_leaf_instance.glsl
    glm::vec2 uvs[4] = {
        glm::vec2(0.0f, 0.0f),  // Top-left gets bottom of texture
        glm::vec2(0.0f, 1.0f),  // Bottom-left gets top of texture
        glm::vec2(1.0f, 1.0f),  // Bottom-right
        glm::vec2(1.0f, 0.0f)   // Top-right
    };

    for (int i = 0; i < 4; ++i) {
        vertices[i].position = positions[i];
        vertices[i].normal = glm::vec3(0.0f, 0.0f, 1.0f);  // Default normal, will be computed in shader
        vertices[i].texCoord = uvs[i];
        vertices[i].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);  // Default tangent
        vertices[i].color = glm::vec4(1.0f);  // Not used for instanced leaves
    }

    sharedLeafQuadMesh_.setCustomGeometry(vertices, indices);
    if (!sharedLeafQuadMesh_.upload(storedAllocator_, storedDevice_, storedCommandPool_, storedQueue_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload shared leaf quad mesh");
        return false;
    }

    SDL_Log("TreeSystem: Created shared leaf quad mesh (4 vertices, 6 indices)");
    return true;
}

bool TreeSystem::uploadLeafInstanceBuffer() {
    // Destroy old buffer if it exists
    if (leafInstanceBuffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(storedAllocator_, leafInstanceBuffer_, leafInstanceAllocation_);
        leafInstanceBuffer_ = VK_NULL_HANDLE;
        leafInstanceAllocation_ = VK_NULL_HANDLE;
    }

    // Flatten all per-tree leaf instances into a single array and compute draw info
    allLeafInstances_.clear();
    leafDrawInfoPerTree_.clear();

    uint32_t currentOffset = 0;
    for (size_t treeIdx = 0; treeIdx < leafInstancesPerTree_.size(); ++treeIdx) {
        const auto& treeLeaves = leafInstancesPerTree_[treeIdx];

        LeafDrawInfo drawInfo;
        drawInfo.firstInstance = currentOffset;
        drawInfo.instanceCount = static_cast<uint32_t>(treeLeaves.size());
        leafDrawInfoPerTree_.push_back(drawInfo);

        allLeafInstances_.insert(allLeafInstances_.end(), treeLeaves.begin(), treeLeaves.end());
        currentOffset += drawInfo.instanceCount;
    }

    if (allLeafInstances_.empty()) {
        leafInstanceBufferSize_ = 0;
        return true;  // No leaves to upload
    }

    // Create storage buffer for leaf instances
    leafInstanceBufferSize_ = sizeof(LeafInstanceGPU) * allLeafInstances_.size();

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(leafInstanceBufferSize_)
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocationInfo{};
    if (vmaCreateBuffer(storedAllocator_, reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo), &allocInfo,
                        &leafInstanceBuffer_, &leafInstanceAllocation_, &allocationInfo) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create leaf instance SSBO");
        return false;
    }

    // Copy data to the mapped buffer
    memcpy(allocationInfo.pMappedData, allLeafInstances_.data(), leafInstanceBufferSize_);

    SDL_Log("TreeSystem: Uploaded %zu leaf instances to SSBO (%zu bytes)",
            allLeafInstances_.size(), static_cast<size_t>(leafInstanceBufferSize_));
    return true;
}

uint32_t TreeSystem::addTree(const glm::vec3& position, float rotation, float scale, const TreeOptions& options) {
    // Generate mesh and leaf instances for this tree
    Mesh branchMesh;
    std::vector<LeafInstanceGPU> leafInstances;
    TreeMeshData meshData;
    if (!generateTreeMesh(options, branchMesh, leafInstances, &meshData)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSystem: Failed to generate tree mesh");
        return UINT32_MAX;
    }

    // Compute full tree bounds (branches + leaves) before moving data
    AABB fullBounds = computeFullTreeBounds(branchMesh, leafInstances);

    uint32_t meshIndex = static_cast<uint32_t>(branchMeshes_.size());
    branchMeshes_.push_back(std::move(branchMesh));
    leafInstancesPerTree_.push_back(std::move(leafInstances));
    treeOptions_.push_back(options);
    treeMeshData_.push_back(std::move(meshData));
    fullTreeBounds_.push_back(fullBounds);

    TreeInstanceData instance;
    instance.transform = Transform(position, Transform::yRotation(rotation), scale);
    instance.meshIndex = meshIndex;
    instance.isSelected = false;

    // Determine archetype index based on leaf type
    // Archetypes: 0=oak, 1=pine, 2=ash, 3=aspen
    const std::string& leafType = options.leaves.type;
    if (leafType == "oak") {
        instance.archetypeIndex = 0;
    } else if (leafType == "pine") {
        instance.archetypeIndex = 1;
    } else if (leafType == "ash") {
        instance.archetypeIndex = 2;
    } else if (leafType == "aspen") {
        instance.archetypeIndex = 3;
    } else {
        instance.archetypeIndex = 0;  // Default to oak
    }

    uint32_t treeIndex = static_cast<uint32_t>(treeInstances_.size());
    treeInstances_.push_back(instance);

    // Create ECS entity for this tree
    ecs::Entity entity = createTreeEntity(treeIndex, instance, options, fullBounds);
    treeEntities_.push_back(entity);

    // Upload leaf instances to GPU SSBO
    if (!uploadLeafInstanceBuffer()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSystem: Failed to upload leaf instance buffer");
    }

    refreshMeshRefs();

    return treeIndex;
}

uint32_t TreeSystem::addTreeFromStagedData(
    const glm::vec3& position, float rotation, float scale,
    const TreeOptions& options,
    const std::vector<uint8_t>& branchVertexData,
    uint32_t branchVertexCount,
    const std::vector<uint32_t>& branchIndices,
    const std::vector<uint8_t>& leafInstanceData,
    uint32_t leafInstanceCount,
    uint32_t archetypeIndex)
{
    // Create branch mesh from staged data
    Mesh branchMesh;

    if (!branchVertexData.empty() && branchVertexCount > 0) {
        // Reconstruct vertex vector from raw bytes
        std::vector<Vertex> vertices(branchVertexCount);
        std::memcpy(vertices.data(), branchVertexData.data(),
                    std::min(branchVertexData.size(), branchVertexCount * sizeof(Vertex)));

        branchMesh.setCustomGeometry(vertices, branchIndices);
        if (!branchMesh.upload(storedAllocator_, storedDevice_, storedCommandPool_, storedQueue_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSystem: Failed to upload staged branch mesh");
            return UINT32_MAX;
        }
    }

    // Reconstruct leaf instances from raw bytes
    std::vector<LeafInstanceGPU> leafInstances(leafInstanceCount);
    if (leafInstanceCount > 0 && !leafInstanceData.empty()) {
        std::memcpy(leafInstances.data(), leafInstanceData.data(),
                    std::min(leafInstanceData.size(), leafInstanceCount * sizeof(LeafInstanceGPU)));
    }

    // Compute bounds from mesh and leaf instances
    AABB fullBounds = computeFullTreeBounds(branchMesh, leafInstances);

    uint32_t meshIndex = static_cast<uint32_t>(branchMeshes_.size());
    branchMeshes_.push_back(std::move(branchMesh));
    leafInstancesPerTree_.push_back(std::move(leafInstances));
    treeOptions_.push_back(options);
    treeMeshData_.push_back(TreeMeshData{});  // Empty mesh data for staged trees
    fullTreeBounds_.push_back(fullBounds);

    TreeInstanceData instance;
    instance.transform = Transform(position, Transform::yRotation(rotation), scale);
    instance.meshIndex = meshIndex;
    instance.isSelected = false;
    instance.archetypeIndex = archetypeIndex;

    uint32_t treeIndex = static_cast<uint32_t>(treeInstances_.size());
    treeInstances_.push_back(instance);

    // Create ECS entity for this tree
    ecs::Entity entity = createTreeEntity(treeIndex, instance, options, fullBounds);
    treeEntities_.push_back(entity);

    // Note: Don't upload leaf buffer or rebuild scene objects here
    // Caller should call finalizeLeafInstanceBuffer() after adding all trees

    return treeIndex;
}

bool TreeSystem::finalizeLeafInstanceBuffer() {
    if (!uploadLeafInstanceBuffer()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSystem: Failed to upload leaf instance buffer");
        return false;
    }
    refreshMeshRefs();
    return true;
}

void TreeSystem::removeTree(uint32_t index) {
    if (index >= treeInstances_.size()) return;

    // Destroy ECS entity
    destroyTreeEntity(index);
    if (index < treeEntities_.size()) {
        treeEntities_.erase(treeEntities_.begin() + index);

        // Update treeInstanceIndex in remaining entities' TreeData
        if (world_) {
            for (uint32_t i = index; i < treeEntities_.size(); ++i) {
                if (treeEntities_[i] != ecs::NullEntity && world_->has<ecs::TreeData>(treeEntities_[i])) {
                    world_->get<ecs::TreeData>(treeEntities_[i]).treeInstanceIndex = static_cast<int>(i);
                }
            }
        }
    }

    // Remove instance
    treeInstances_.erase(treeInstances_.begin() + index);

    // Update selected index
    if (selectedTreeIndex_ == static_cast<int>(index)) {
        selectedTreeIndex_ = -1;
    } else if (selectedTreeIndex_ > static_cast<int>(index)) {
        selectedTreeIndex_--;
    }

    refreshMeshRefs();
}

void TreeSystem::selectTree(int index) {
    // Deselect previous
    if (selectedTreeIndex_ >= 0 && selectedTreeIndex_ < static_cast<int>(treeInstances_.size())) {
        treeInstances_[selectedTreeIndex_].isSelected = false;
        // Remove ECS tag from previous selection
        if (world_ && static_cast<size_t>(selectedTreeIndex_) < treeEntities_.size()) {
            ecs::Entity prev = treeEntities_[selectedTreeIndex_];
            if (prev != ecs::NullEntity && world_->has<ecs::TreeSelected>(prev)) {
                world_->remove<ecs::TreeSelected>(prev);
            }
        }
    }

    // Select new
    if (index >= 0 && index < static_cast<int>(treeInstances_.size())) {
        selectedTreeIndex_ = index;
        treeInstances_[index].isSelected = true;
        // Add ECS tag to new selection
        if (world_ && static_cast<size_t>(index) < treeEntities_.size()) {
            ecs::Entity e = treeEntities_[index];
            if (e != ecs::NullEntity && !world_->has<ecs::TreeSelected>(e)) {
                world_->add<ecs::TreeSelected>(e);
            }
        }
    } else {
        selectedTreeIndex_ = -1;
    }
}

void TreeSystem::updateSelectedTreeOptions(const TreeOptions& options) {
    if (selectedTreeIndex_ < 0 || selectedTreeIndex_ >= static_cast<int>(treeInstances_.size())) {
        return;
    }

    uint32_t meshIndex = treeInstances_[selectedTreeIndex_].meshIndex;
    if (meshIndex >= treeOptions_.size()) return;

    // Update options
    treeOptions_[meshIndex] = options;

    // Regenerate mesh
    regenerateTree(static_cast<uint32_t>(selectedTreeIndex_));
}

const TreeOptions* TreeSystem::getSelectedTreeOptions() const {
    if (selectedTreeIndex_ < 0 || selectedTreeIndex_ >= static_cast<int>(treeInstances_.size())) {
        return nullptr;
    }

    uint32_t meshIndex = treeInstances_[selectedTreeIndex_].meshIndex;
    if (meshIndex >= treeOptions_.size()) return nullptr;

    return &treeOptions_[meshIndex];
}

void TreeSystem::loadPreset(const std::string& name) {
    // Try to load from JSON first, fall back to hardcoded defaults
    std::string presetDir = storedResourcePath_ + "/assets/trees/presets/";
    std::string jsonPath = presetDir + name + "_large.json";

    if (std::filesystem::exists(jsonPath)) {
        setPreset(TreeOptions::loadFromJson(jsonPath));
        SDL_Log("TreeSystem: Loaded preset from %s", jsonPath.c_str());
        return;
    }

    // Fall back to hardcoded defaults
    if (name == "oak") {
        setPreset(TreeOptions::defaultOak());
    } else if (name == "pine") {
        setPreset(TreeOptions::defaultPine());
    } else if (name == "birch") {
        setPreset(TreeOptions::defaultBirch());
    } else if (name == "willow") {
        setPreset(TreeOptions::defaultWillow());
    } else if (name == "aspen") {
        setPreset(TreeOptions::defaultAspen());
    } else if (name == "bush") {
        setPreset(TreeOptions::defaultBush());
    }
}

void TreeSystem::setPreset(const TreeOptions& preset) {
    defaultOptions_ = preset;

    if (selectedTreeIndex_ >= 0) {
        updateSelectedTreeOptions(preset);
    }
}

void TreeSystem::regenerateTree(uint32_t treeIndex) {
    if (treeIndex >= treeInstances_.size()) return;

    uint32_t meshIndex = treeInstances_[treeIndex].meshIndex;
    if (meshIndex >= treeOptions_.size()) return;

    // Destroy old branch mesh
    if (meshIndex < branchMeshes_.size()) {
        branchMeshes_[meshIndex].releaseGPUResources();
    }

    // Generate new branch mesh and leaf instances
    Mesh branchMesh;
    std::vector<LeafInstanceGPU> leafInstances;
    TreeMeshData meshData;
    if (generateTreeMesh(treeOptions_[meshIndex], branchMesh, leafInstances, &meshData)) {
        // Compute full bounds before moving data
        AABB fullBounds = computeFullTreeBounds(branchMesh, leafInstances);

        if (meshIndex < branchMeshes_.size()) {
            branchMeshes_[meshIndex] = std::move(branchMesh);
        }
        if (meshIndex < leafInstancesPerTree_.size()) {
            leafInstancesPerTree_[meshIndex] = std::move(leafInstances);
        }
        if (meshIndex < treeMeshData_.size()) {
            treeMeshData_[meshIndex] = std::move(meshData);
        }
        if (meshIndex < fullTreeBounds_.size()) {
            fullTreeBounds_[meshIndex] = fullBounds;
        }
    }

    // Re-upload leaf instance buffer
    uploadLeafInstanceBuffer();

    refreshMeshRefs();
}

const TreeMeshData* TreeSystem::getTreeMeshData(uint32_t meshIndex) const {
    if (meshIndex >= treeMeshData_.size()) {
        return nullptr;
    }
    return &treeMeshData_[meshIndex];
}

std::vector<PhysicsWorld::CapsuleData> TreeSystem::getTreeCollisionCapsules(
    uint32_t treeIndex,
    const TreeCollision::Config& config) const
{
    if (treeIndex >= treeInstances_.size()) {
        return {};
    }

    const TreeInstanceData& instance = treeInstances_[treeIndex];
    if (instance.meshIndex >= treeMeshData_.size()) {
        return {};
    }

    // Generate capsules in local tree space (relative to tree origin)
    auto localCapsules = TreeCollision::generateCapsules(treeMeshData_[instance.meshIndex], config);

    // Apply instance scale to capsule dimensions and positions
    // Keep positions LOCAL so caller can position the compound body at tree's world position
    std::vector<PhysicsWorld::CapsuleData> scaledCapsules;
    scaledCapsules.reserve(localCapsules.size());

    for (const auto& local : localCapsules) {
        PhysicsWorld::CapsuleData scaled;

        // Scale the local position (still relative to tree origin at 0,0,0)
        scaled.localPosition = local.localPosition * instance.scale();

        // Keep the local rotation unchanged
        scaled.localRotation = local.localRotation;

        // Scale the capsule dimensions
        scaled.halfHeight = local.halfHeight * instance.scale();
        scaled.radius = local.radius * instance.scale();

        scaledCapsules.push_back(scaled);
    }

    return scaledCapsules;
}
