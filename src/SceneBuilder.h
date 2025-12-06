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

    SceneBuilder() = default;
    ~SceneBuilder() = default;

    bool init(const InitInfo& info);
    void destroy(VmaAllocator allocator, VkDevice device);

    // Access to built scene
    const std::vector<Renderable>& getRenderables() const { return sceneObjects; }
    std::vector<Renderable>& getRenderables() { return sceneObjects; }
    size_t getPlayerObjectIndex() const { return playerObjectIndex; }

    // Material registry - call registerMaterials() after init(), before Renderer creates descriptor sets
    MaterialRegistry& getMaterialRegistry() { return materialRegistry; }
    const MaterialRegistry& getMaterialRegistry() const { return materialRegistry; }

    // Access to textures for descriptor set creation
    Texture& getGroundTexture() { return groundTexture; }
    Texture& getGroundNormalMap() { return groundNormalMap; }
    Texture& getCrateTexture() { return crateTexture; }
    Texture& getCrateNormalMap() { return crateNormalMap; }
    Texture& getMetalTexture() { return metalTexture; }
    Texture& getMetalNormalMap() { return metalNormalMap; }
    Texture& getDefaultEmissiveMap() { return defaultEmissiveMap; }
    Texture& getWhiteTexture() { return whiteTexture; }

    // Access to meshes for dynamic updates (e.g., cloth)
    Mesh& getFlagClothMesh() { return flagClothMesh; }
    Mesh& getFlagPoleMesh() { return flagPoleMesh; }
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
    AnimatedCharacter& getAnimatedCharacter() { return animatedCharacter; }
    const AnimatedCharacter& getAnimatedCharacter() const { return animatedCharacter; }
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

    // Meshes
    Mesh groundMesh;
    Mesh cubeMesh;
    Mesh sphereMesh;
    Mesh capsuleMesh;
    Mesh flagPoleMesh;
    Mesh flagClothMesh;
    Mesh capeMesh;
    AnimatedCharacter animatedCharacter;  // Player character (animated from glTF)
    bool hasAnimatedCharacter = false;  // True if animated character was loaded successfully

    // Player cape (cloth simulation attached to character)
    PlayerCape playerCape;
    bool hasCapeEnabled = false;

    // Textures
    Texture crateTexture;
    Texture crateNormalMap;
    Texture groundTexture;
    Texture groundNormalMap;
    Texture metalTexture;
    Texture metalNormalMap;
    Texture defaultEmissiveMap;  // Black texture for objects without emissive
    Texture whiteTexture;        // White texture for vertex-colored objects
    Texture capeTexture;         // Cape diffuse texture
    Texture capeNormalMap;       // Cape normal map

    // Scene objects
    std::vector<Renderable> sceneObjects;
    size_t playerObjectIndex = 0;
    size_t flagPoleIndex = 0;
    size_t flagClothIndex = 0;
    size_t wellEntranceIndex = 0;
    size_t capeIndex = 0;

    // Well entrance position (for terrain hole creation)
    float wellEntranceX = 0.0f;
    float wellEntranceZ = 0.0f;

    // Material registry for data-driven material management
    MaterialRegistry materialRegistry;

    // Material IDs cached for use in createRenderables
    MaterialId crateMaterialId = INVALID_MATERIAL_ID;
    MaterialId metalMaterialId = INVALID_MATERIAL_ID;
    MaterialId whiteMaterialId = INVALID_MATERIAL_ID;
    MaterialId capeMaterialId = INVALID_MATERIAL_ID;
};
