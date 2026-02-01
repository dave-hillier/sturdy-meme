#pragma once

#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "SceneBuilder.h"
#include "Light.h"
#include "PhysicsSystem.h"
#include "ecs/World.h"

// Centralized scene management - handles visual objects, physics bodies, and lighting
class SceneManager {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit SceneManager(ConstructToken) {}

    /**
     * Factory: Create and initialize SceneManager.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<SceneManager> create(SceneBuilder::InitInfo& builderInfo);


    ~SceneManager();

    // Non-copyable, non-movable
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&) = delete;
    SceneManager& operator=(SceneManager&&) = delete;

    // Initialize physics bodies for scene objects (called separately by Application)
    void initPhysics(PhysicsWorld& physics);

    // Initialize terrain physics using heightfield data from TerrainSystem
    void initTerrainPhysics(PhysicsWorld& physics, const float* heightSamples,
                            uint32_t sampleCount, float worldSize, float heightScale);

    // Initialize terrain physics with hole mask (for caves/wells)
    void initTerrainPhysics(PhysicsWorld& physics, const float* heightSamples,
                            const uint8_t* holeMask, uint32_t sampleCount,
                            float worldSize, float heightScale);

    // Update scene state (sync physics to visuals)
    void update(PhysicsWorld& physics);

    // Player transform updates
    void updatePlayerTransform(const glm::mat4& transform);

    // Scene object access for rendering
    std::vector<Renderable>& getRenderables() { return sceneBuilder->getRenderables(); }
    const std::vector<Renderable>& getRenderables() const { return sceneBuilder->getRenderables(); }
    size_t getPlayerObjectIndex() const { return sceneBuilder->getPlayerObjectIndex(); }

    // SceneBuilder access for texture descriptor sets
    SceneBuilder& getSceneBuilder() { return *sceneBuilder; }
    const SceneBuilder& getSceneBuilder() const { return *sceneBuilder; }

    // Light management (deprecated - use ECS lights)
    LightManager& getLightManager() { return lightManager; }
    const LightManager& getLightManager() const { return lightManager; }

    // ECS light management
    void setECSWorld(ecs::World* world) { ecsWorld_ = world; }
    ecs::Entity getOrbLightEntity() const { return orbLightEntity_; }

    // Reinitialize lights with ECS (call after setECSWorld)
    void initializeECSLights();

    // Orb light position (updated by physics)
    void setOrbLightPosition(const glm::vec3& position) { orbLightPosition = position; }
    const glm::vec3& getOrbLightPosition() const { return orbLightPosition; }

    // Physics body access for ECS integration
    const std::vector<PhysicsBodyID>& getPhysicsBodies() const { return scenePhysicsBodies; }
    PhysicsBodyID getPhysicsBody(size_t index) const {
        return index < scenePhysicsBodies.size() ? scenePhysicsBodies[index] : INVALID_BODY_ID;
    }

private:
    bool initInternal(SceneBuilder::InitInfo& builderInfo);
    void cleanup();
    void initializeScenePhysics(PhysicsWorld& physics);
    void initializeSceneLights();
    void updatePhysicsToScene(PhysicsWorld& physics);

    // Stored for cleanup
    VmaAllocator storedAllocator = VK_NULL_HANDLE;
    VkDevice storedDevice = VK_NULL_HANDLE;

    // Get terrain height at (x, z), returns 0 if no terrain function available
    float getTerrainHeight(float x, float z) const;

    // Scene resources
    std::unique_ptr<SceneBuilder> sceneBuilder;
    SceneBuilder::HeightQueryFunc terrainHeightFunc;
    LightManager lightManager;
    glm::vec2 sceneOrigin = glm::vec2(0.0f);  // World XZ offset for scene

    // Physics body tracking (mapped to scene object indices)
    std::vector<PhysicsBodyID> scenePhysicsBodies;

    // Stored physics world pointer for deferred initialization callback
    PhysicsWorld* storedPhysics_ = nullptr;

    // Orb light position (follows emissive orb physics object)
    glm::vec3 orbLightPosition = glm::vec3(2.0f, 1.3f, 0.0f);

    // ECS light integration
    ecs::World* ecsWorld_ = nullptr;
    ecs::Entity orbLightEntity_ = ecs::NullEntity;
    ecs::Entity blueLightEntity_ = ecs::NullEntity;
    ecs::Entity greenLightEntity_ = ecs::NullEntity;
};
