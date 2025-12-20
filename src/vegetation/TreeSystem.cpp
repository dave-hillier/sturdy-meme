#include "TreeSystem.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <filesystem>

std::unique_ptr<TreeSystem> TreeSystem::create(const InitInfo& info) {
    std::unique_ptr<TreeSystem> system(new TreeSystem());
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

    // Load trees from JSON presets
    std::string presetsPath = info.resourcePath + "/assets/trees/presets/";

    // Try to load presets from files, fallback to hardcoded if not found
    std::vector<std::pair<std::string, glm::vec3>> treesToCreate;

    // Check if preset directory exists
    if (std::filesystem::exists(presetsPath)) {
        // Load oak_large.json at origin
        std::string oakPath = presetsPath + "oak_large.json";
        if (std::filesystem::exists(oakPath)) {
            defaultOptions_ = TreeOptions::loadFromJson(oakPath);
            treesToCreate.push_back({"oak_large.json", glm::vec3(0.0f, 0.0f, 0.0f)});
        } else {
            defaultOptions_ = TreeOptions::defaultOak();
            treesToCreate.push_back({"default_oak", glm::vec3(0.0f, 0.0f, 0.0f)});
        }

        // Additional trees at different positions
        treesToCreate.push_back({"pine_large.json", glm::vec3(30.0f, 0.0f, 0.0f)});
        treesToCreate.push_back({"ash_large.json", glm::vec3(-30.0f, 0.0f, 0.0f)});
        treesToCreate.push_back({"aspen_large.json", glm::vec3(0.0f, 0.0f, 30.0f)});
    } else {
        SDL_Log("TreeSystem: Preset directory not found, using defaults");
        defaultOptions_ = TreeOptions::defaultOak();
        treesToCreate.push_back({"default_oak", glm::vec3(0.0f, 0.0f, 0.0f)});
    }

    // Create trees
    for (size_t i = 0; i < treesToCreate.size(); ++i) {
        const auto& [presetName, position] = treesToCreate[i];

        TreeOptions treeOpts;
        std::string presetPath = presetsPath + presetName;
        if (presetName.find(".json") != std::string::npos && std::filesystem::exists(presetPath)) {
            treeOpts = TreeOptions::loadFromJson(presetPath);
        } else {
            // Fallback to default with different seed
            treeOpts = defaultOptions_;
            treeOpts.seed = 12345 + static_cast<uint32_t>(i) * 1000;
        }

        Mesh branchMesh, leafMesh;
        if (!generateTreeMesh(treeOpts, branchMesh, leafMesh)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSystem: Failed to generate tree mesh for %s", presetName.c_str());
            continue;
        }

        uint32_t meshIndex = static_cast<uint32_t>(branchMeshes_.size());
        branchMeshes_.push_back(std::move(branchMesh));
        leafMeshes_.push_back(std::move(leafMesh));
        treeOptions_.push_back(treeOpts);

        TreeInstanceData instance;
        instance.position = position;
        if (info.getTerrainHeight) {
            instance.position.y = info.getTerrainHeight(position.x, position.z);
        }
        instance.rotation = 0.0f;
        instance.scale = 1.0f;
        instance.meshIndex = meshIndex;
        instance.isSelected = (i == 0);  // Select first tree
        treeInstances_.push_back(instance);
    }

    if (!treeInstances_.empty()) {
        selectedTreeIndex_ = 0;
    }

    // Create scene objects for rendering
    createSceneObjects();

    SDL_Log("TreeSystem::init() complete - %zu trees created", treeInstances_.size());
    return true;
}

void TreeSystem::cleanup() {
    // Clean up textures
    for (auto& [name, tex] : barkTextures_) {
        if (tex) {
            tex->destroy(storedAllocator_, storedDevice_);
        }
    }
    barkTextures_.clear();

    for (auto& [name, tex] : barkNormalMaps_) {
        if (tex) {
            tex->destroy(storedAllocator_, storedDevice_);
        }
    }
    barkNormalMaps_.clear();

    for (auto& [name, tex] : leafTextures_) {
        if (tex) {
            tex->destroy(storedAllocator_, storedDevice_);
        }
    }
    leafTextures_.clear();

    // Clean up meshes
    for (auto& mesh : branchMeshes_) {
        mesh.destroy(storedAllocator_);
    }
    branchMeshes_.clear();

    for (auto& mesh : leafMeshes_) {
        mesh.destroy(storedAllocator_);
    }
    leafMeshes_.clear();

    branchRenderables_.clear();
    leafRenderables_.clear();
    treeInstances_.clear();
    treeOptions_.clear();
}

bool TreeSystem::loadTextures(const InitInfo& info) {
    std::string texturePath = info.resourcePath + "/textures/";

    // Bark type names (data-driven from JSON presets)
    std::vector<std::string> barkTypeNames = {"birch", "oak", "pine", "willow"};

    // Load all bark textures
    for (const auto& typeName : barkTypeNames) {
        auto tex = std::make_unique<Texture>();
        std::string path = texturePath + "bark/" + typeName + "_color_1k.jpg";
        if (!tex->load(path, info.allocator, info.device,
                       info.commandPool, info.graphicsQueue, info.physicalDevice)) {
            SDL_Log("TreeSystem: Using placeholder for %s bark texture", typeName.c_str());
            if (!tex->createSolidColor(102, 77, 51, 255, info.allocator, info.device,
                                        info.commandPool, info.graphicsQueue)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create bark texture %s", typeName.c_str());
                return false;
            }
        } else {
            SDL_Log("TreeSystem: Loaded bark texture: %s", path.c_str());
        }
        barkTextures_[typeName] = std::move(tex);
    }

    // Load all bark normal maps
    for (const auto& typeName : barkTypeNames) {
        auto tex = std::make_unique<Texture>();
        std::string path = texturePath + "bark/" + typeName + "_normal_1k.jpg";
        if (!tex->load(path, info.allocator, info.device,
                       info.commandPool, info.graphicsQueue, info.physicalDevice, false)) {
            SDL_Log("TreeSystem: Using placeholder for %s bark normal", typeName.c_str());
            if (!tex->createSolidColor(128, 128, 255, 255, info.allocator, info.device,
                                        info.commandPool, info.graphicsQueue)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create bark normal %s", typeName.c_str());
                return false;
            }
        } else {
            SDL_Log("TreeSystem: Loaded bark normal: %s", path.c_str());
        }
        barkNormalMaps_[typeName] = std::move(tex);
    }

    // Leaf type names (data-driven from JSON presets)
    std::vector<std::string> leafTypeNames = {"ash", "aspen", "pine", "oak"};

    // Load all leaf textures
    for (const auto& typeName : leafTypeNames) {
        auto tex = std::make_unique<Texture>();
        std::string path = texturePath + "leaves/" + typeName + "_color.png";
        if (!tex->load(path, info.allocator, info.device,
                       info.commandPool, info.graphicsQueue, info.physicalDevice)) {
            SDL_Log("TreeSystem: Using placeholder for %s leaf texture", typeName.c_str());
            if (!tex->createSolidColor(51, 102, 51, 200, info.allocator, info.device,
                                        info.commandPool, info.graphicsQueue)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create leaf texture %s", typeName.c_str());
                return false;
            }
        } else {
            SDL_Log("TreeSystem: Loaded leaf texture: %s", path.c_str());
        }
        leafTextures_[typeName] = std::move(tex);
    }

    return true;
}

Texture* TreeSystem::getBarkTexture(const std::string& type) const {
    auto it = barkTextures_.find(type);
    if (it != barkTextures_.end()) {
        return it->second.get();
    }
    // Fallback to oak if type not found
    it = barkTextures_.find("oak");
    return it != barkTextures_.end() ? it->second.get() : nullptr;
}

Texture* TreeSystem::getBarkNormalMap(const std::string& type) const {
    auto it = barkNormalMaps_.find(type);
    if (it != barkNormalMaps_.end()) {
        return it->second.get();
    }
    // Fallback to oak if type not found
    it = barkNormalMaps_.find("oak");
    return it != barkNormalMaps_.end() ? it->second.get() : nullptr;
}

Texture* TreeSystem::getLeafTexture(const std::string& type) const {
    auto it = leafTextures_.find(type);
    if (it != leafTextures_.end()) {
        return it->second.get();
    }
    // Fallback to oak if type not found
    it = leafTextures_.find("oak");
    return it != leafTextures_.end() ? it->second.get() : nullptr;
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

bool TreeSystem::generateTreeMesh(const TreeOptions& options, Mesh& branchMesh, Mesh& leafMesh) {
    // Generate tree data
    TreeMeshData meshData = generator_.generate(options);

    SDL_Log("TreeSystem: Generated tree with %zu branches, %zu leaves",
            meshData.branches.size(), meshData.leaves.size());

    // Build branch mesh vertices
    std::vector<Vertex> branchVertices;
    std::vector<uint32_t> branchIndices;

    // Get texture scale from options (applied to UVs)
    glm::vec2 textureScale = options.bark.textureScale;

    uint32_t indexOffset = 0;
    for (const auto& branch : meshData.branches) {
        int sectionCount = branch.sectionCount;
        int segmentCount = branch.segmentCount;

        for (size_t sectionIdx = 0; sectionIdx < branch.sections.size(); ++sectionIdx) {
            const SectionData& section = branch.sections[sectionIdx];

            // Match ez-tree: V coordinate alternates 0/1 to tile the bark texture along the branch
            // This creates a repeating pattern rather than stretching the texture
            float vCoord = (sectionIdx % 2 == 0) ? 0.0f : textureScale.y;

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

    // Build leaf mesh vertices
    std::vector<Vertex> leafVertices;
    std::vector<uint32_t> leafIndices;

    uint32_t leafVertexOffset = 0;
    for (const auto& leaf : meshData.leaves) {
        float halfW = leaf.size * 0.5f;
        float leafHeight = leaf.size;

        // Calculate billboard mode based on options (for now, always double)
        int quadsPerLeaf = (options.leaves.billboard == BillboardMode::Double) ? 2 : 1;

        for (int quad = 0; quad < quadsPerLeaf; ++quad) {
            float yRotation = (quad == 1) ? glm::half_pi<float>() : 0.0f;
            glm::quat yQuat = glm::angleAxis(yRotation, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat finalQuat = leaf.orientation * yQuat;

            // Quad vertices (bottom to top)
            glm::vec3 localVerts[4] = {
                glm::vec3(-halfW, leafHeight, 0.0f),  // Top-left
                glm::vec3(-halfW, 0.0f, 0.0f),         // Bottom-left
                glm::vec3(halfW, 0.0f, 0.0f),          // Bottom-right
                glm::vec3(halfW, leafHeight, 0.0f)    // Top-right
            };

            // UVs flipped vertically so leaf base (branch) is at bottom of quad
            glm::vec2 uvs[4] = {
                glm::vec2(0.0f, 0.0f),  // Top-left vertex gets bottom of texture
                glm::vec2(0.0f, 1.0f),  // Bottom-left vertex gets top of texture
                glm::vec2(1.0f, 1.0f),  // Bottom-right
                glm::vec2(1.0f, 0.0f)   // Top-right
            };

            glm::vec3 normal = finalQuat * glm::vec3(0.0f, 0.0f, 1.0f);

            for (int i = 0; i < 4; ++i) {
                Vertex v{};
                v.position = leaf.position + finalQuat * localVerts[i];
                v.normal = normal;
                v.texCoord = uvs[i];
                v.tangent = glm::vec4(finalQuat * glm::vec3(1.0f, 0.0f, 0.0f), 1.0f);
                // Wind animation data in vertex color:
                // RGB = pivot point (leaf attachment) for skeletal rotation
                // A = 0.98 (leaves are highest level, maximum sway - not 1.0 to avoid default color detection)
                v.color = glm::vec4(leaf.position, 0.98f);

                leafVertices.push_back(v);
            }

            // Indices - CCW winding when viewed from positive Z (where normal points)
            uint32_t base = leafVertexOffset + quad * 4;
            leafIndices.push_back(base + 0);
            leafIndices.push_back(base + 2);
            leafIndices.push_back(base + 1);
            leafIndices.push_back(base + 0);
            leafIndices.push_back(base + 3);
            leafIndices.push_back(base + 2);
        }

        leafVertexOffset += quadsPerLeaf * 4;
    }

    // Create GPU meshes
    if (!branchVertices.empty()) {
        branchMesh.setCustomGeometry(branchVertices, branchIndices);
        if (!branchMesh.upload(storedAllocator_, storedDevice_, storedCommandPool_, storedQueue_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload branch mesh");
            return false;
        }
    }

    if (!leafVertices.empty()) {
        leafMesh.setCustomGeometry(leafVertices, leafIndices);
        if (!leafMesh.upload(storedAllocator_, storedDevice_, storedCommandPool_, storedQueue_)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload leaf mesh");
            return false;
        }
    }

    SDL_Log("TreeSystem: Created meshes - branches: %zu verts, %zu indices; leaves: %zu verts, %zu indices",
            branchVertices.size(), branchIndices.size(),
            leafVertices.size(), leafIndices.size());

    return true;
}

void TreeSystem::createSceneObjects() {
    branchRenderables_.clear();
    leafRenderables_.clear();

    for (const auto& instance : treeInstances_) {
        if (instance.meshIndex >= branchMeshes_.size()) continue;
        if (instance.meshIndex >= treeOptions_.size()) continue;

        const TreeOptions& opts = treeOptions_[instance.meshIndex];

        // Build transform
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), instance.position);
        transform = glm::rotate(transform, instance.rotation, glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::scale(transform, glm::vec3(instance.scale));

        // Get textures based on tree options (string-based lookup)
        Texture* barkTex = getBarkTexture(opts.bark.type);
        Texture* leafTex = getLeafTexture(opts.leaves.type);

        // Branch renderable
        Mesh* branchMesh = &branchMeshes_[instance.meshIndex];
        if (branchMesh->getIndexCount() > 0) {
            Renderable branchRenderable = RenderableBuilder()
                .withMesh(branchMesh)
                .withTexture(barkTex)
                .withTransform(transform)
                .withRoughness(0.7f)
                .withMetallic(0.0f)
                .build();

            branchRenderables_.push_back(branchRenderable);
        }

        // Leaf renderable
        if (instance.meshIndex < leafMeshes_.size()) {
            Mesh* leafMesh = &leafMeshes_[instance.meshIndex];
            if (leafMesh->getIndexCount() > 0) {
                Renderable leafRenderable = RenderableBuilder()
                    .withMesh(leafMesh)
                    .withTexture(leafTex)
                    .withTransform(transform)
                    .withRoughness(0.8f)
                    .withMetallic(0.0f)
                    .withAlphaTest(opts.leaves.alphaTest)
                    .build();

                leafRenderables_.push_back(leafRenderable);
            }
        }
    }
}

void TreeSystem::rebuildSceneObjects() {
    createSceneObjects();
}

uint32_t TreeSystem::addTree(const glm::vec3& position, float rotation, float scale, const TreeOptions& options) {
    // Generate mesh for this tree
    Mesh branchMesh, leafMesh;
    if (!generateTreeMesh(options, branchMesh, leafMesh)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TreeSystem: Failed to generate tree mesh");
        return UINT32_MAX;
    }

    uint32_t meshIndex = static_cast<uint32_t>(branchMeshes_.size());
    branchMeshes_.push_back(std::move(branchMesh));
    leafMeshes_.push_back(std::move(leafMesh));
    treeOptions_.push_back(options);

    TreeInstanceData instance;
    instance.position = position;
    instance.rotation = rotation;
    instance.scale = scale;
    instance.meshIndex = meshIndex;
    instance.isSelected = false;

    uint32_t treeIndex = static_cast<uint32_t>(treeInstances_.size());
    treeInstances_.push_back(instance);

    rebuildSceneObjects();

    return treeIndex;
}

void TreeSystem::removeTree(uint32_t index) {
    if (index >= treeInstances_.size()) return;

    // Remove instance
    treeInstances_.erase(treeInstances_.begin() + index);

    // Update selected index
    if (selectedTreeIndex_ == static_cast<int>(index)) {
        selectedTreeIndex_ = -1;
    } else if (selectedTreeIndex_ > static_cast<int>(index)) {
        selectedTreeIndex_--;
    }

    rebuildSceneObjects();
}

void TreeSystem::selectTree(int index) {
    // Deselect previous
    if (selectedTreeIndex_ >= 0 && selectedTreeIndex_ < static_cast<int>(treeInstances_.size())) {
        treeInstances_[selectedTreeIndex_].isSelected = false;
    }

    // Select new
    if (index >= 0 && index < static_cast<int>(treeInstances_.size())) {
        selectedTreeIndex_ = index;
        treeInstances_[index].isSelected = true;
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

    // Destroy old meshes
    if (meshIndex < branchMeshes_.size()) {
        branchMeshes_[meshIndex].destroy(storedAllocator_);
    }
    if (meshIndex < leafMeshes_.size()) {
        leafMeshes_[meshIndex].destroy(storedAllocator_);
    }

    // Generate new meshes
    Mesh branchMesh, leafMesh;
    if (generateTreeMesh(treeOptions_[meshIndex], branchMesh, leafMesh)) {
        if (meshIndex < branchMeshes_.size()) {
            branchMeshes_[meshIndex] = std::move(branchMesh);
        }
        if (meshIndex < leafMeshes_.size()) {
            leafMeshes_[meshIndex] = std::move(leafMesh);
        }
    }

    rebuildSceneObjects();
}
