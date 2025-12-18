#pragma once

#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "SceneBuilder.h"
#include "Light.h"
#include "PhysicsSystem.h"

// Centralized scene management - handles visual objects, physics bodies, and lighting
class SceneManager {
public:
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
    std::vector<Renderable>& getRenderables() { return sceneBuilder.getRenderables(); }
    const std::vector<Renderable>& getRenderables() const { return sceneBuilder.getRenderables(); }
    size_t getPlayerObjectIndex() const { return sceneBuilder.getPlayerObjectIndex(); }

    // SceneBuilder access for texture descriptor sets
    SceneBuilder& getSceneBuilder() { return sceneBuilder; }
    const SceneBuilder& getSceneBuilder() const { return sceneBuilder; }

    // Light management
    LightManager& getLightManager() { return lightManager; }
    const LightManager& getLightManager() const { return lightManager; }

    // Orb light position (updated by physics)
    void setOrbLightPosition(const glm::vec3& position) { orbLightPosition = position; }
    const glm::vec3& getOrbLightPosition() const { return orbLightPosition; }

private:
    SceneManager() = default;  // Private: use factory

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
    SceneBuilder sceneBuilder;
    SceneBuilder::HeightQueryFunc terrainHeightFunc;
    LightManager lightManager;

    // Physics body tracking (mapped to scene object indices)
    std::vector<PhysicsBodyID> scenePhysicsBodies;

    // Orb light position (follows emissive orb physics object)
    glm::vec3 orbLightPosition = glm::vec3(2.0f, 1.3f, 0.0f);
};
