#pragma once

#include <vector>
#include <glm/glm.hpp>
#include "SceneBuilder.h"
#include "Light.h"
#include "PhysicsSystem.h"

// Centralized scene management - handles visual objects, physics bodies, and lighting
class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager() = default;

    // Initialize scene with visual assets and lights
    bool init(SceneBuilder::InitInfo& builderInfo);

    // Initialize physics bodies for scene objects (called separately by Application)
    void initPhysics(PhysicsWorld& physics);

    // Initialize terrain physics using heightfield data from TerrainSystem
    void initTerrainPhysics(PhysicsWorld& physics, const float* heightSamples,
                            uint32_t sampleCount, float worldSize, float heightScale);

    // Initialize terrain physics with hole mask (for caves/wells)
    void initTerrainPhysics(PhysicsWorld& physics, const float* heightSamples,
                            const uint8_t* holeMask, uint32_t sampleCount,
                            float worldSize, float heightScale);

    void destroy(VmaAllocator allocator, VkDevice device);

    // Update scene state (sync physics to visuals)
    void update(PhysicsWorld& physics);

    // Player transform updates
    void updatePlayerTransform(const glm::mat4& transform);

    // Scene object access for rendering
    std::vector<SceneObject>& getSceneObjects() { return sceneBuilder.getSceneObjects(); }
    const std::vector<SceneObject>& getSceneObjects() const { return sceneBuilder.getSceneObjects(); }
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
    void initializeScenePhysics(PhysicsWorld& physics);
    void initializeSceneLights();
    void updatePhysicsToScene(PhysicsWorld& physics);

    // Get terrain height at (x, z), returns 0 if no terrain function available
    float getTerrainHeight(float x, float z) const;

    // Scene resources
    SceneBuilder sceneBuilder;
    SceneBuilder::HeightQueryFunc terrainHeightFunc;
    LightManager lightManager;

    // Physics body tracking (mapped to scene object indices)
    std::vector<PhysicsBodyID> scenePhysicsBodies;

    // Orb light position (follows physics object 6)
    glm::vec3 orbLightPosition = glm::vec3(2.0f, 1.3f, 0.0f);

    // Scene object indices (for clarity)
    static constexpr size_t ORB_LIGHT_OBJECT_INDEX = 6;
};
