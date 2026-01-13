#include "AssetRegistry.h"
#include <SDL_log.h>
#include <algorithm>

AssetRegistry::~AssetRegistry() {
    cleanup();
}

void AssetRegistry::init(VkDevice device, VkPhysicalDevice physicalDevice,
                         VmaAllocator allocator, VkCommandPool commandPool,
                         VkQueue queue) {
    std::lock_guard<std::mutex> lock(mutex_);

    device_ = device;
    physicalDevice_ = physicalDevice;
    allocator_ = allocator;
    commandPool_ = commandPool;
    queue_ = queue;

    SDL_Log("AssetRegistry initialized");
}

void AssetRegistry::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Destroy shaders (they're not reference counted, registry owns them)
    for (auto& [path, module] : shaderCache_) {
        if (module) {
            vkDestroyShaderModule(device_, module, nullptr);
        }
    }
    shaderCache_.clear();

    // Clear caches - textures and meshes are freed when their shared_ptrs expire
    textureCache_.clear();
    meshCache_.clear();

    SDL_Log("AssetRegistry cleaned up");
}

// ============================================================================
// Texture Management
// ============================================================================

std::shared_ptr<Texture> AssetRegistry::loadTexture(const std::string& path,
                                                    const TextureLoadConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check cache first
    auto it = textureCache_.find(path);
    if (it != textureCache_.end()) {
        if (auto texture = it->second.lock()) {
            textureCacheHits_++;
            return texture;
        }
        // Weak ptr expired, remove stale entry
        textureCache_.erase(it);
    }

    // Load the texture
    std::unique_ptr<Texture> texPtr;
    if (config.generateMipmaps) {
        texPtr = Texture::loadFromFileWithMipmaps(
            path, allocator_, device_, commandPool_, queue_,
            physicalDevice_, config.useSRGB, config.enableAnisotropy);
    } else {
        texPtr = Texture::loadFromFile(
            path, allocator_, device_, commandPool_, queue_,
            physicalDevice_, config.useSRGB);
    }

    if (!texPtr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to load texture: %s", path.c_str());
        return nullptr;
    }

    // Convert to shared_ptr and cache
    auto texture = std::shared_ptr<Texture>(texPtr.release());
    textureCache_[path] = texture;

    SDL_Log("AssetRegistry: Loaded texture '%s'", path.c_str());
    return texture;
}

std::shared_ptr<Texture> AssetRegistry::createSolidColorTexture(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check cache if name provided
    if (!name.empty()) {
        auto it = textureCache_.find(name);
        if (it != textureCache_.end()) {
            if (auto texture = it->second.lock()) {
                textureCacheHits_++;
                return texture;
            }
            textureCache_.erase(it);
        }
    }

    auto texPtr = Texture::createSolidColor(
        r, g, b, a, allocator_, device_, commandPool_, queue_);

    if (!texPtr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to create solid color texture");
        return nullptr;
    }

    auto texture = std::shared_ptr<Texture>(texPtr.release());

    // Cache with name if provided
    if (!name.empty()) {
        textureCache_[name] = texture;
    }

    return texture;
}

void AssetRegistry::registerTexture(std::shared_ptr<Texture> texture, const std::string& name) {
    if (!texture || name.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    textureCache_[name] = texture;
}

std::shared_ptr<Texture> AssetRegistry::getTexture(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = textureCache_.find(path);
    if (it != textureCache_.end()) {
        if (auto texture = it->second.lock()) {
            return texture;
        }
        // Expired, clean up
        textureCache_.erase(it);
    }
    return nullptr;
}

// ============================================================================
// Mesh Management
// ============================================================================

std::shared_ptr<Mesh> AssetRegistry::createMesh(const MeshConfig& config,
                                                const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check cache if name provided
    if (!name.empty()) {
        auto it = meshCache_.find(name);
        if (it != meshCache_.end()) {
            if (auto mesh = it->second.lock()) {
                return mesh;
            }
            meshCache_.erase(it);
        }
    }

    auto mesh = std::make_shared<Mesh>();

    switch (config.type) {
        case MeshConfig::Type::Cube:
            mesh->createCube();
            break;
        case MeshConfig::Type::Plane:
            mesh->createPlane(config.width, config.depth);
            break;
        case MeshConfig::Type::Sphere:
            mesh->createSphere(config.radius, config.stacks, config.slices);
            break;
        case MeshConfig::Type::Cylinder:
            mesh->createCylinder(config.radius, config.height, config.segments);
            break;
        case MeshConfig::Type::Capsule:
            mesh->createCapsule(config.radius, config.height, config.stacks, config.slices);
            break;
        case MeshConfig::Type::Disc:
            mesh->createDisc(config.radius, config.segments, config.uvScale);
            break;
        case MeshConfig::Type::Rock:
            mesh->createRock(config.radius, config.subdivisions, config.seed,
                            config.roughness, config.asymmetry);
            break;
        case MeshConfig::Type::Custom:
            // Empty mesh for custom - caller should use createCustomMesh instead
            break;
    }

    if (!mesh->upload(allocator_, device_, commandPool_, queue_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to upload mesh");
        return nullptr;
    }

    // Cache with name if provided
    if (!name.empty()) {
        meshCache_[name] = mesh;
    }

    SDL_Log("AssetRegistry: Created mesh '%s'", name.empty() ? "unnamed" : name.c_str());
    return mesh;
}

std::shared_ptr<Mesh> AssetRegistry::createCustomMesh(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto mesh = std::make_shared<Mesh>();
    mesh->setCustomGeometry(vertices, indices);

    if (!mesh->upload(allocator_, device_, commandPool_, queue_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to upload custom mesh");
        return nullptr;
    }

    if (!name.empty()) {
        meshCache_[name] = mesh;
    }

    return mesh;
}

void AssetRegistry::registerMesh(std::shared_ptr<Mesh> mesh, const std::string& name) {
    if (!mesh || name.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    meshCache_[name] = mesh;
}

std::shared_ptr<Mesh> AssetRegistry::getMesh(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = meshCache_.find(name);
    if (it != meshCache_.end()) {
        if (auto mesh = it->second.lock()) {
            return mesh;
        }
        meshCache_.erase(it);
    }
    return nullptr;
}

// ============================================================================
// Shader Management
// ============================================================================

vk::ShaderModule AssetRegistry::loadShader(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check cache first
    auto it = shaderCache_.find(path);
    if (it != shaderCache_.end()) {
        shaderCacheHits_++;
        return it->second;
    }

    // Load shader
    auto module = ShaderLoader::loadShaderModule(device_, path);
    if (!module) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to load shader: %s", path.c_str());
        return VK_NULL_HANDLE;
    }

    shaderCache_[path] = *module;

    SDL_Log("AssetRegistry: Loaded shader '%s'", path.c_str());
    return *module;
}

vk::ShaderModule AssetRegistry::getShader(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = shaderCache_.find(path);
    if (it != shaderCache_.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

// ============================================================================
// Statistics and Maintenance
// ============================================================================

AssetRegistry::Stats AssetRegistry::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    Stats stats;
    // Count non-expired entries
    for (const auto& [path, wp] : textureCache_) {
        if (!wp.expired()) stats.textureCount++;
    }
    for (const auto& [name, wp] : meshCache_) {
        if (!wp.expired()) stats.meshCount++;
    }
    stats.shaderCount = shaderCache_.size();
    stats.textureCacheHits = textureCacheHits_;
    stats.shaderCacheHits = shaderCacheHits_;
    return stats;
}

void AssetRegistry::pruneExpiredEntries() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Remove expired texture entries
    for (auto it = textureCache_.begin(); it != textureCache_.end();) {
        if (it->second.expired()) {
            SDL_Log("AssetRegistry: Pruned expired texture '%s'", it->first.c_str());
            it = textureCache_.erase(it);
        } else {
            ++it;
        }
    }

    // Remove expired mesh entries
    for (auto it = meshCache_.begin(); it != meshCache_.end();) {
        if (it->second.expired()) {
            SDL_Log("AssetRegistry: Pruned expired mesh '%s'", it->first.c_str());
            it = meshCache_.erase(it);
        } else {
            ++it;
        }
    }
}
