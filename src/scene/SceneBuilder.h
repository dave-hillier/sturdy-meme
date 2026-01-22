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
#include <optional>
#include <memory>

class AssetRegistry;

// Holds all scene resources (meshes, textures) and provides scene objects
class SceneBuilder {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit SceneBuilder(ConstructToken) {}

    // Function type for querying terrain height at world position (x, z)
    using HeightQueryFunc = std::function<float(float, float)>;

    // Callback fired when renderables are created (for deferred creation mode)
    using OnRenderablesCreatedCallback = std::function<void()>;

    struct InitInfo {
        VmaAllocator allocator;
        VkDevice device;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        VkPhysicalDevice physicalDevice;
        std::string resourcePath;
        AssetRegistry* assetRegistry = nullptr;  // Centralized asset management
        HeightQueryFunc getTerrainHeight;  // Optional: query terrain height for object placement
        glm::vec2 sceneOrigin = glm::vec2(0.0f);  // World XZ offset for scene objects
        bool deferRenderables = false;  // If true, don't create renderables during init
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

    // Check if renderables have been created (false if deferred and not yet triggered)
    bool hasRenderables() const { return renderablesCreated_; }

    // Create renderables now (for deferred creation mode)
    // Call this when terrain is ready if deferRenderables was true during init
    void createRenderablesDeferred();

    // Register callback to be notified when renderables are created
    // Used by SceneManager to initialize physics after deferred creation
    void setOnRenderablesCreated(OnRenderablesCreatedCallback callback) {
        onRenderablesCreated_ = std::move(callback);
    }

    // Get indices of objects that need physics bodies
    // SceneManager uses this instead of hardcoded indices
    const std::vector<size_t>& getPhysicsEnabledIndices() const { return physicsEnabledIndices; }

    // Material registry - call registerMaterials() after init(), before Renderer creates descriptor sets
    MaterialRegistry& getMaterialRegistry() { return materialRegistry; }
    const MaterialRegistry& getMaterialRegistry() const { return materialRegistry; }

    // Access to textures for descriptor set creation (via AssetRegistry)
    const Texture* getGroundTexture() const;
    const Texture* getGroundNormalMap() const;
    const Texture* getCrateTexture() const;
    const Texture* getCrateNormalMap() const;
    const Texture* getMetalTexture() const;
    const Texture* getMetalNormalMap() const;
    const Texture* getDefaultEmissiveMap() const;
    const Texture* getWhiteTexture() const;

    // Access to meshes for dynamic updates (e.g., cloth)
    Mesh& getFlagClothMesh() { return flagClothMesh; }
    Mesh& getFlagPoleMesh() { return *flagPoleMesh; }
    size_t getFlagClothIndex() const { return flagClothIndex; }
    size_t getFlagPoleIndex() const { return flagPoleIndex; }

    // Cape access
    PlayerCape& getPlayerCape() { return playerCape; }
    Mesh& getCapeMesh() { return capeMesh; }
    size_t getCapeIndex() const { return capeIndex; }
    bool hasCape() const { return hasCapeEnabled; }
    void setCapeEnabled(bool enabled) { hasCapeEnabled = enabled; }

    // Weapon visibility
    void setShowSword(bool show) { showSword_ = show; }
    bool getShowSword() const { return showSword_; }
    void setShowShield(bool show) { showShield_ = show; }
    bool getShowShield() const { return showShield_; }

    // Weapon debug axes
    void setShowWeaponAxes(bool show) { showWeaponAxes_ = show; }
    bool getShowWeaponAxes() const { return showWeaponAxes_; }

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

    // Player weapons access
    bool hasWeapons() const { return rightHandBoneIndex >= 0 && leftHandBoneIndex >= 0; }
    size_t getSwordIndex() const { return swordIndex; }
    size_t getShieldIndex() const { return shieldIndex; }

    // Update weapon transforms based on character bone positions
    // Call after updating animated character each frame
    void updateWeaponTransforms(const glm::mat4& characterWorldTransform);

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

    // Asset registry for centralized resource management
    std::optional<std::reference_wrapper<AssetRegistry>> assetRegistry_;

    // Meshes (static RAII-managed)
    std::unique_ptr<Mesh> cubeMesh;
    std::unique_ptr<Mesh> sphereMesh;
    std::unique_ptr<Mesh> capsuleMesh;
    std::unique_ptr<Mesh> flagPoleMesh;
    std::unique_ptr<Mesh> swordMesh;   // Long cylinder for sword
    std::unique_ptr<Mesh> shieldMesh;  // Flat cylinder for shield
    std::unique_ptr<Mesh> axisLineMesh; // Thin cylinder for debug axis visualization

    // Meshes (dynamic - manually managed, re-uploaded during runtime)
    Mesh flagClothMesh;
    Mesh capeMesh;

    // Animated character (RAII-managed via unique_ptr - mesh uploaded once, bone matrices updated by Renderer)
    std::unique_ptr<AnimatedCharacter> animatedCharacter;
    bool hasAnimatedCharacter = false;  // True if animated character was loaded successfully

    // Player cape (cloth simulation attached to character)
    PlayerCape playerCape;
    bool hasCapeEnabled = false;

    // Weapon visibility and debug visualization
    bool showSword_ = true;
    bool showShield_ = true;
    bool showWeaponAxes_ = false;

    // Textures (managed via AssetRegistry with shared_ptr)
    std::shared_ptr<Texture> crateTexture_;
    std::shared_ptr<Texture> crateNormal_;
    std::shared_ptr<Texture> groundTexture_;
    std::shared_ptr<Texture> groundNormal_;
    std::shared_ptr<Texture> metalTexture_;
    std::shared_ptr<Texture> metalNormal_;
    std::shared_ptr<Texture> defaultEmissive_;  // Black texture for objects without emissive
    std::shared_ptr<Texture> whiteTexture_;     // White texture for vertex-colored objects

    // Scene objects
    std::vector<Renderable> sceneObjects;
    size_t playerObjectIndex = 0;
    size_t flagPoleIndex = 0;
    size_t flagClothIndex = 0;
    size_t wellEntranceIndex = 0;
    size_t capeIndex = 0;
    size_t emissiveOrbIndex = 0;  // Glowing orb that has a corresponding light
    size_t swordIndex = 0;        // Player sword renderable index
    size_t shieldIndex = 0;       // Player shield renderable index
    // Debug axis indicators (R=X, G=Y, B=Z) for hands
    size_t rightHandAxisX = 0;
    size_t rightHandAxisY = 0;
    size_t rightHandAxisZ = 0;
    size_t leftHandAxisX = 0;
    size_t leftHandAxisY = 0;
    size_t leftHandAxisZ = 0;
    int32_t rightHandBoneIndex = -1;  // Bone index for sword attachment
    int32_t leftHandBoneIndex = -1;   // Bone index for shield attachment

    // Indices of objects that should have physics bodies (dynamic objects)
    // Objects NOT in this list are either static (lights, flags) or handled separately (player)
    std::vector<size_t> physicsEnabledIndices;

    // Well entrance position (for terrain hole creation)
    float wellEntranceX = 0.0f;
    float wellEntranceZ = 0.0f;

    // Scene origin offset (world XZ position where scene objects are placed)
    glm::vec2 sceneOrigin = glm::vec2(0.0f);

    // Track whether renderables have been created (for deferred mode)
    bool renderablesCreated_ = false;

    // Callback fired when renderables are created (for physics initialization)
    OnRenderablesCreatedCallback onRenderablesCreated_;

    // Material registry for data-driven material management
    MaterialRegistry materialRegistry;

    // Material IDs cached for use in createRenderables
    MaterialId crateMaterialId = INVALID_MATERIAL_ID;
    MaterialId groundMaterialId = INVALID_MATERIAL_ID;
    MaterialId metalMaterialId = INVALID_MATERIAL_ID;
    MaterialId whiteMaterialId = INVALID_MATERIAL_ID;
    MaterialId capeMaterialId = INVALID_MATERIAL_ID;
};
