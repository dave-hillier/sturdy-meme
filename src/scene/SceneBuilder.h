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
#include "ecs/World.h"
#include "ecs/Components.h"
#include <array>
#include <optional>
#include <memory>
#include <unordered_map>

class AssetRegistry;
class NPCSimulation;

// Holds all scene resources (meshes, textures) and provides scene objects
class SceneBuilder {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit SceneBuilder(ConstructToken);

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
        ecs::World* ecsWorld = nullptr;  // Optional: ECS world for NPC entity creation
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

    // Access to built scene (legacy - for backwards compatibility)
    const std::vector<Renderable>& getRenderables() const { return sceneObjects; }
    std::vector<Renderable>& getRenderables() { return sceneObjects; }

    // ECS entity accessors (Phase 6: direct entity access)
    ecs::Entity getPlayerEntity() const { return playerEntity_; }
    ecs::Entity getEmissiveOrbEntity() const { return emissiveOrbEntity_; }
    ecs::Entity getFlagPoleEntity() const { return flagPoleEntity_; }
    ecs::Entity getFlagClothEntity() const { return flagClothEntity_; }
    ecs::Entity getCapeEntity() const { return capeEntity_; }
    ecs::Entity getSwordEntity() const { return swordEntity_; }
    ecs::Entity getShieldEntity() const { return shieldEntity_; }
    ecs::Entity getWellEntranceEntity() const { return wellEntranceEntity_; }

    // Get all scene entities (for ECS-based iteration)
    const std::vector<ecs::Entity>& getSceneEntities() const { return sceneEntities_; }

    // Find the renderable for a given entity (returns nullptr if not found)
    Renderable* getRenderableForEntity(ecs::Entity entity) {
        auto it = entityToRenderableIndex_.find(entity);
        if (it != entityToRenderableIndex_.end() && it->second < sceneObjects.size()) {
            return &sceneObjects[it->second];
        }
        return nullptr;
    }
    const Renderable* getRenderableForEntity(ecs::Entity entity) const {
        auto it = entityToRenderableIndex_.find(entity);
        if (it != entityToRenderableIndex_.end() && it->second < sceneObjects.size()) {
            return &sceneObjects[it->second];
        }
        return nullptr;
    }

    // Get NPC entity by index
    ecs::Entity getNPCEntity(size_t npcIndex) const {
        if (npcIndex < npcEntities_.size()) {
            return npcEntities_[npcIndex];
        }
        return ecs::NullEntity;
    }

    // Set the ECS world (must be called before createRenderables if ECS is used)
    void setECSWorld(ecs::World* world) { ecsWorld_ = world; }
    ecs::World* getECSWorld() const { return ecsWorld_; }

    // Create entities from renderables (for legacy compatibility during transition)
    void createEntitiesFromRenderables();

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


    // Material registry - call registerMaterials() after init(), before Renderer creates descriptor sets
    MaterialRegistry& getMaterialRegistry() { return materialRegistry; }
    const MaterialRegistry& getMaterialRegistry() const { return materialRegistry; }

    // Mesh accessors for demo/testing purposes
    Mesh* getCubeMesh() const { return cubeMesh.get(); }
    Mesh* getSphereMesh() const { return sphereMesh.get(); }

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

    // Cape access
    PlayerCape& getPlayerCape() { return playerCape; }
    Mesh& getCapeMesh() { return capeMesh; }
    bool hasCape() const { return hasCapeEnabled; }
    void setCapeEnabled(bool enabled) { hasCapeEnabled = enabled; }

    // Weapon visibility (controlled via WeaponTag.visible on entities)
    void setShowSword(bool show);
    void setShowShield(bool show);
    void setShowWeaponAxes(bool show);

    // Well entrance position (for creating terrain hole)
    float getWellEntranceX() const { return wellEntranceX; }
    float getWellEntranceZ() const { return wellEntranceZ; }
    static constexpr float WELL_HOLE_RADIUS = 1.0f;  // Radius in meters (hole mask 8192 res = ~2m/texel)

    // Update player transform
    void updatePlayerTransform(const glm::mat4& transform);

    // Upload flag cloth mesh (for dynamic updates)
    void uploadFlagClothMesh(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue);

    // Animated character access
    AnimatedCharacter& getAnimatedCharacter() { return *animatedCharacter; }
    const AnimatedCharacter& getAnimatedCharacter() const { return *animatedCharacter; }
    bool hasCharacter() const { return hasAnimatedCharacter; }

    // NPC access via NPCSimulation
    NPCSimulation* getNPCSimulation() { return npcSimulation_.get(); }
    const NPCSimulation* getNPCSimulation() const { return npcSimulation_.get(); }
    size_t getNPCCount() const;
    bool hasNPCs() const;


    // Player weapons access
    bool hasWeapons() const { return rightHandBoneIndex >= 0 && leftHandBoneIndex >= 0; }
    int32_t getRightHandBoneIndex() const { return rightHandBoneIndex; }
    int32_t getLeftHandBoneIndex() const { return leftHandBoneIndex; }

    // Get weapon offset matrices (for ECS bone attachment)
    glm::mat4 getSwordOffset() const {
        glm::mat4 offset = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        return glm::translate(offset, glm::vec3(0.0f, 0.4f, 0.0f));
    }
    glm::mat4 getShieldOffset() const {
        return glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    // Update weapon transforms based on character bone positions
    // Call after updating animated character each frame
    void updateWeaponTransforms(const glm::mat4& characterWorldTransform);

    // Update animated character (call each frame)
    // movementSpeed: horizontal speed for animation state selection
    // isGrounded: whether on the ground
    // isJumping: whether just started jumping
    // position: world position (for motion matching)
    // facing: facing direction (for motion matching)
    // inputDirection: desired movement direction (for motion matching)
    // strafeMode: whether orientation is locked (for strafe animation matching)
    // cameraDirection: camera facing direction (for strafe mode - character faces camera)
    void updateAnimatedCharacter(float deltaTime, VmaAllocator allocator, VkDevice device,
                                  VkCommandPool commandPool, VkQueue queue,
                                  float movementSpeed = 0.0f, bool isGrounded = true, bool isJumping = false,
                                  const glm::vec3& position = glm::vec3(0.0f),
                                  const glm::vec3& facing = glm::vec3(0.0f, 0.0f, 1.0f),
                                  const glm::vec3& inputDirection = glm::vec3(0.0f),
                                  bool strafeMode = false,
                                  const glm::vec3& cameraDirection = glm::vec3(0.0f, 0.0f, 1.0f));

    // Update all NPCs (call each frame) - updates animation states
    // cameraPos: used for LOD level calculation
    void updateNPCs(float deltaTime, const glm::vec3& cameraPos);

    // Start a jump with trajectory prediction
    void startCharacterJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const class PhysicsWorld* physics);

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createMeshes(const InitInfo& info);
    bool loadTextures(const InitInfo& info);
    void registerMaterials();
    void createRenderables();
    void createNPCs(const InitInfo& info);  // Create NPC characters

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

    // NPCs (managed by NPCSimulation for update/LOD logic separation)
    std::unique_ptr<NPCSimulation> npcSimulation_;

    // Player cape (cloth simulation attached to character)
    PlayerCape playerCape;
    bool hasCapeEnabled = false;

    // (Weapon visibility now stored in WeaponTag.visible and DebugAxisTag.visible on entities)

    // Textures (managed via AssetRegistry with shared_ptr)
    std::shared_ptr<Texture> crateTexture_;
    std::shared_ptr<Texture> crateNormal_;
    std::shared_ptr<Texture> groundTexture_;
    std::shared_ptr<Texture> groundNormal_;
    std::shared_ptr<Texture> metalTexture_;
    std::shared_ptr<Texture> metalNormal_;
    std::shared_ptr<Texture> defaultEmissive_;  // Black texture for objects without emissive
    std::shared_ptr<Texture> whiteTexture_;     // White texture for vertex-colored objects

    // Scene objects (renderables for rendering pipeline)
    std::vector<Renderable> sceneObjects;

    int32_t rightHandBoneIndex = -1;  // Bone index for sword attachment
    int32_t leftHandBoneIndex = -1;   // Bone index for shield attachment

    // Scene object roles - assigned during createRenderables(), consumed during entity creation
    enum class ObjectRole : uint8_t {
        None, Player, EmissiveOrb, FlagPole, FlagCloth, Cape,
        Sword, Shield, WellEntrance, DebugAxisRightX, DebugAxisRightY,
        DebugAxisRightZ, DebugAxisLeftX, DebugAxisLeftY, DebugAxisLeftZ
    };
    std::vector<ObjectRole> objectRoles_;

    // ECS entity handles - the primary way to identify scene objects
    ecs::World* ecsWorld_ = nullptr;  // Pointer to ECS world (not owned)
    std::vector<ecs::Entity> sceneEntities_;  // All scene entities (parallel to sceneObjects)
    std::vector<ecs::Entity> npcEntities_;    // NPC entities
    std::unordered_map<ecs::Entity, size_t> entityToRenderableIndex_;  // Reverse map
    ecs::Entity playerEntity_ = ecs::NullEntity;
    ecs::Entity emissiveOrbEntity_ = ecs::NullEntity;
    ecs::Entity flagPoleEntity_ = ecs::NullEntity;
    ecs::Entity flagClothEntity_ = ecs::NullEntity;
    ecs::Entity capeEntity_ = ecs::NullEntity;
    ecs::Entity swordEntity_ = ecs::NullEntity;
    ecs::Entity shieldEntity_ = ecs::NullEntity;
    ecs::Entity wellEntranceEntity_ = ecs::NullEntity;
    // Debug axis entities (3 per hand: X=Red, Y=Green, Z=Blue)
    std::array<ecs::Entity, 3> rightHandAxisEntities_ = {ecs::NullEntity, ecs::NullEntity, ecs::NullEntity};
    std::array<ecs::Entity, 3> leftHandAxisEntities_ = {ecs::NullEntity, ecs::NullEntity, ecs::NullEntity};

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
