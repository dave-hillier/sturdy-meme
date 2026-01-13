#include "AssetRegistry.h"
#include <SDL3/SDL_log.h>

void AssetRegistry::init(VkDevice device, VkPhysicalDevice physicalDevice,
                         VmaAllocator allocator, VkCommandPool commandPool,
                         VkQueue queue) {
    device_ = device;
    physicalDevice_ = physicalDevice;
    allocator_ = allocator;
    commandPool_ = commandPool;
    queue_ = queue;

    SDL_Log("AssetRegistry initialized");
}

// ============================================================================
// Texture Management
// ============================================================================

std::shared_ptr<Texture> AssetRegistry::loadTexture(const std::string& path,
                                                    const TextureLoadConfig& config) {
    // Check cache first
    if (auto cached = textureCache_.get(path)) {
        return cached;
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
    textureCache_.put(path, texture);

    SDL_Log("AssetRegistry: Loaded texture '%s'", path.c_str());
    return texture;
}

std::shared_ptr<Texture> AssetRegistry::createSolidColorTexture(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a, const std::string& name) {
    // Check cache if name provided
    if (!name.empty()) {
        if (auto cached = textureCache_.get(name)) {
            return cached;
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

    if (!name.empty()) {
        textureCache_.put(name, texture);
    }

    return texture;
}

void AssetRegistry::registerTexture(std::shared_ptr<Texture> texture, const std::string& name) {
    if (texture && !name.empty()) {
        textureCache_.put(name, texture);
    }
}

std::shared_ptr<Texture> AssetRegistry::getTexture(const std::string& path) {
    return textureCache_.get(path);
}

// ============================================================================
// Mesh Management
// ============================================================================

std::shared_ptr<Mesh> AssetRegistry::createMesh(const MeshConfig& config,
                                                const std::string& name) {
    // Check cache if name provided
    if (!name.empty()) {
        if (auto cached = meshCache_.get(name)) {
            return cached;
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

    if (!name.empty()) {
        meshCache_.put(name, mesh);
    }

    SDL_Log("AssetRegistry: Created mesh '%s'", name.empty() ? "unnamed" : name.c_str());
    return mesh;
}

std::shared_ptr<Mesh> AssetRegistry::createCustomMesh(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    const std::string& name) {
    auto mesh = std::make_shared<Mesh>();
    mesh->setCustomGeometry(vertices, indices);

    if (!mesh->upload(allocator_, device_, commandPool_, queue_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to upload custom mesh");
        return nullptr;
    }

    if (!name.empty()) {
        meshCache_.put(name, mesh);
    }

    return mesh;
}

void AssetRegistry::registerMesh(std::shared_ptr<Mesh> mesh, const std::string& name) {
    if (mesh && !name.empty()) {
        meshCache_.put(name, mesh);
    }
}

std::shared_ptr<Mesh> AssetRegistry::getMesh(const std::string& name) {
    return meshCache_.get(name);
}

// ============================================================================
// Shader Management
// ============================================================================

std::shared_ptr<AssetRegistry::ShaderModule> AssetRegistry::loadShader(const std::string& path) {
    // Check cache first
    if (auto cached = shaderCache_.get(path)) {
        return cached;
    }

    // Load shader
    auto module = ShaderLoader::loadShaderModule(device_, path);
    if (!module) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to load shader: %s", path.c_str());
        return nullptr;
    }

    // Wrap in RAII container
    auto shader = std::make_shared<ShaderModule>(*module, device_);
    shaderCache_.put(path, shader);

    SDL_Log("AssetRegistry: Loaded shader '%s'", path.c_str());
    return shader;
}

std::shared_ptr<AssetRegistry::ShaderModule> AssetRegistry::getShader(const std::string& path) {
    return shaderCache_.get(path);
}

// ============================================================================
// Statistics and Maintenance
// ============================================================================

AssetRegistry::Stats AssetRegistry::getStats() const {
    Stats stats;
    stats.textureCount = textureCache_.size();
    stats.meshCount = meshCache_.size();
    stats.shaderCount = shaderCache_.size();
    stats.textureCacheHits = textureCache_.hits();
    stats.shaderCacheHits = shaderCache_.hits();
    return stats;
}

void AssetRegistry::pruneExpiredEntries() {
    size_t texturesPruned = textureCache_.prune();
    size_t meshesPruned = meshCache_.prune();
    size_t shadersPruned = shaderCache_.prune();

    if (texturesPruned > 0 || meshesPruned > 0 || shadersPruned > 0) {
        SDL_Log("AssetRegistry: Pruned %zu textures, %zu meshes, %zu shaders",
                texturesPruned, meshesPruned, shadersPruned);
    }
}
