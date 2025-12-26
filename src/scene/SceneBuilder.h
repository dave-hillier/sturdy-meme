#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <functional>

#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"
#include "GLTFLoader.h"
#include "AnimatedCharacter.h"
#include "MaterialRegistry.h"
#include "PlayerCape.h"
#include "RAIIAdapter.h"
#include <optional>
#include <memory>

// Holds all scene resources (meshes, textures) and provides scene objects
class SceneBuilder {
public:
    // Function type for querying terrain height at world position (x, z)
    using HeightQueryFunc = std::function<float(float, float)>;

    struct InitInfo {
        VmaAllocator allocator;
        VkDevice device;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        VkPhysicalDevice physicalDevice;
        std::string resourcePath;
        HeightQueryFunc getTerrainHeight;  // Optional: query terrain height for object placement
    };

    /**
     * Factory: Create and initialize SceneBuilder.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<SceneBuilder> create(const InitInfo& info);

    ~SceneBuilder();

    // Non-copyable, non-movable
    SceneBuilder(const SceneBuilder&) = delete;
    SceneBuilder& operator=(const SceneBuilder&) = delete;
    SceneBuilder(SceneBuilder&&) = delete;
    SceneBuilder& operator=(SceneBuilder&&) = delete;

    // Access to built scene
    const std::vector<Renderable>& getRenderables() const { return sceneObjects; }
    std::vector<Renderable>& getRenderables() { return sceneObjects; }
    size_t getPlayerObjectIndex() const { return playerObjectIndex; }
    size_t getEmissiveOrbIndex() const { return emissiveOrbIndex; }

    // Get indices of objects that need physics bodies
    // SceneManager uses this instead of hardcoded indices
    const std::vector<size_t>& getPhysicsEnabledIndices() const { return physicsEnabledIndices; }

    // Material registry - call registerMaterials() after init(), before Renderer creates descriptor sets
    MaterialRegistry& getMaterialRegistry() { return materialRegistry; }
    const MaterialRegistry& getMaterialRegistry() const { return materialRegistry; }

    // Access to textures for descriptor set creation
    Texture& getGroundTexture() { return **groundTexture; }
    Texture& getGroundNormalMap() { return **groundNormalMap; }
    Texture& getCrateTexture() { return **crateTexture; }
    Texture& getCrateNormalMap() { return **crateNormalMap; }
    Texture& getMetalTexture() { return **metalTexture; }
    Texture& getMetalNormalMap() { return **metalNormalMap; }
    Texture& getDefaultEmissiveMap() { return **defaultEmissiveMap; }
    Texture& getWhiteTexture() { return **whiteTexture; }

    // Access to meshes for dynamic updates (e.g., cloth)
    Mesh& getFlagClothMesh() { return flagClothMesh; }
    Mesh& getFlagPoleMesh() { return **flagPoleMesh; }
    size_t getFlagClothIndex() const { return flagClothIndex; }
    size_t getFlagPoleIndex() const { return flagPoleIndex; }

    // Cape access
    PlayerCape& getPlayerCape() { return playerCape; }
    Mesh& getCapeMesh() { return capeMesh; }
    size_t getCapeIndex() const { return capeIndex; }
    bool hasCape() const { return hasCapeEnabled; }
    void setCapeEnabled(bool enabled) { hasCapeEnabled = enabled; }

    // Well entrance position (for creating terrain hole)
    float getWellEntranceX() const { return wellEntranceX; }
    float getWellEntranceZ() const { return wellEntranceZ; }
    static constexpr float WELL_HOLE_RADIUS = 5.0f;  // Radius in meters (hole mask now 2048 res = ~8m/texel)

    // Update player transform
    void updatePlayerTransform(const glm::mat4& transform);

    // Upload flag cloth mesh (for dynamic updates)
    void uploadFlagClothMesh(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue);

    // Animated character access
    AnimatedCharacter& getAnimatedCharacter() { return *animatedCharacter; }
    const AnimatedCharacter& getAnimatedCharacter() const { return *animatedCharacter; }
    bool hasCharacter() const { return hasAnimatedCharacter; }

    // Update animated character (call each frame)
    // movementSpeed: horizontal speed for animation state selection
    // isGrounded: whether on the ground
    // isJumping: whether just started jumping
    void updateAnimatedCharacter(float deltaTime, VmaAllocator allocator, VkDevice device,
                                  VkCommandPool commandPool, VkQueue queue,
                                  float movementSpeed = 0.0f, bool isGrounded = true, bool isJumping = false);

    // Start a jump with trajectory prediction
    void startCharacterJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const class PhysicsWorld* physics);

private:
    SceneBuilder() = default;  // Private: use factory

    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createMeshes(const InitInfo& info);
    bool loadTextures(const InitInfo& info);
    void registerMaterials();
    void createRenderables();

    // Get terrain height at (x, z), returns 0 if no terrain function available
    float getTerrainHeight(float x, float z) const;

    // Build character model transform from world position and rotation
    glm::mat4 buildCharacterTransform(const glm::vec3& position, float yRotation) const;

    // Terrain height query function
    HeightQueryFunc terrainHeightFunc;

    // Stored for RAII cleanup
    VmaAllocator storedAllocator = VK_NULL_HANDLE;
    VkDevice storedDevice = VK_NULL_HANDLE;

    // Meshes (static RAII-managed)
    std::optional<RAIIAdapter<Mesh>> cubeMesh;
    std::optional<RAIIAdapter<Mesh>> sphereMesh;
    std::optional<RAIIAdapter<Mesh>> capsuleMesh;
    std::optional<RAIIAdapter<Mesh>> flagPoleMesh;

    // Meshes (dynamic - manually managed, re-uploaded during runtime)
    Mesh flagClothMesh;
    Mesh capeMesh;

    // Animated character (RAII-managed via unique_ptr - mesh uploaded once, bone matrices updated by Renderer)
    std::unique_ptr<AnimatedCharacter> animatedCharacter;
    bool hasAnimatedCharacter = false;  // True if animated character was loaded successfully

    // Player cape (cloth simulation attached to character)
    PlayerCape playerCape;
    bool hasCapeEnabled = false;

    // Textures (RAII-managed)
    std::optional<RAIIAdapter<Texture>> crateTexture;
    std::optional<RAIIAdapter<Texture>> crateNormalMap;
    std::optional<RAIIAdapter<Texture>> groundTexture;
    std::optional<RAIIAdapter<Texture>> groundNormalMap;
    std::optional<RAIIAdapter<Texture>> metalTexture;
    std::optional<RAIIAdapter<Texture>> metalNormalMap;
    std::optional<RAIIAdapter<Texture>> defaultEmissiveMap;  // Black texture for objects without emissive
    std::optional<RAIIAdapter<Texture>> whiteTexture;        // White texture for vertex-colored objects

    // Scene objects
    std::vector<Renderable> sceneObjects;
    size_t playerObjectIndex = 0;
    size_t flagPoleIndex = 0;
    size_t flagClothIndex = 0;
    size_t wellEntranceIndex = 0;
    size_t capeIndex = 0;
    size_t emissiveOrbIndex = 0;  // Glowing orb that has a corresponding light

    // Indices of objects that should have physics bodies (dynamic objects)
    // Objects NOT in this list are either static (lights, flags) or handled separately (player)
    std::vector<size_t> physicsEnabledIndices;

    // Well entrance position (for terrain hole creation)
    float wellEntranceX = 0.0f;
    float wellEntranceZ = 0.0f;

    // Material registry for data-driven material management
    MaterialRegistry materialRegistry;

    // Material IDs cached for use in createRenderables
    MaterialId crateMaterialId = INVALID_MATERIAL_ID;
    MaterialId groundMaterialId = INVALID_MATERIAL_ID;
    MaterialId metalMaterialId = INVALID_MATERIAL_ID;
    MaterialId whiteMaterialId = INVALID_MATERIAL_ID;
    MaterialId capeMaterialId = INVALID_MATERIAL_ID;
};
