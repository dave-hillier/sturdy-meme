#pragma once

#include "Components.h"
#include "Systems.h"
#include <entt/entt.hpp>

// Thin wrapper around entt::registry providing convenient entity creation
// and typed queries. Keeps ECS usage simple and focused.
class World {
public:
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }

    // Create player entity with all required components
    entt::entity createPlayer(const glm::vec3& position, float yaw = 0.0f) {
        auto entity = registry_.create();
        registry_.emplace<Transform>(entity, Transform::withYaw(position, yaw));
        registry_.emplace<Velocity>(entity);
        registry_.emplace<PlayerTag>(entity);
        registry_.emplace<PlayerMovement>(entity);
        registry_.emplace<Grounded>(entity);
        return entity;
    }

    // Create a dynamic physics object
    entt::entity createDynamicObject(size_t sceneIndex, PhysicsBodyID bodyId) {
        auto entity = registry_.create();
        registry_.emplace<RenderableRef>(entity, RenderableRef{sceneIndex});
        registry_.emplace<PhysicsBody>(entity, PhysicsBody{bodyId});
        registry_.emplace<DynamicObject>(entity);
        return entity;
    }

    // Create emissive object with light
    entt::entity createEmissiveObject(size_t sceneIndex, PhysicsBodyID bodyId,
                                       const glm::vec3& color, float intensity) {
        auto entity = createDynamicObject(sceneIndex, bodyId);
        registry_.emplace<EmissiveLight>(entity, EmissiveLight{color, intensity});
        return entity;
    }

    // Find the player entity (returns null entity if not found)
    entt::entity findPlayer() const {
        auto view = registry_.view<PlayerTag>();
        return view.empty() ? entt::null : view.front();
    }

    // Get player transform (assumes player exists)
    Transform& getPlayerTransform() {
        return registry_.get<Transform>(findPlayer());
    }

    const Transform& getPlayerTransform() const {
        return registry_.get<Transform>(findPlayer());
    }

    // Get player movement component
    PlayerMovement& getPlayerMovement() {
        return registry_.get<PlayerMovement>(findPlayer());
    }

    // Check if player is grounded
    bool isPlayerGrounded() const {
        auto player = findPlayer();
        return player != entt::null && registry_.all_of<Grounded>(player);
    }

    void setPlayerGrounded(bool grounded) {
        auto player = findPlayer();
        if (player == entt::null) return;

        if (grounded && !registry_.all_of<Grounded>(player)) {
            registry_.emplace<Grounded>(player);
        } else if (!grounded && registry_.all_of<Grounded>(player)) {
            registry_.remove<Grounded>(player);
        }
    }

    // ========================================================================
    // Light Entity Creation
    // ========================================================================

    // Create a point light entity
    entt::entity createPointLight(const glm::vec3& position,
                                   const glm::vec3& color = glm::vec3{1.0f},
                                   float intensity = 1.0f,
                                   float radius = 10.0f) {
        auto entity = registry_.create();

        registry_.emplace<Transform>(entity, Transform::withPosition(position));
        PointLight light;
        light.color = color;
        light.intensity = intensity;
        light.radius = radius;
        registry_.emplace<PointLight>(entity, light);
        registry_.emplace<LightEnabled>(entity);

        return entity;
    }

    // Create a spot light entity
    // Direction is stored as rotation in the Transform component
    entt::entity createSpotLight(const glm::vec3& position,
                                  const glm::vec3& direction,
                                  const glm::vec3& color = glm::vec3{1.0f},
                                  float intensity = 1.0f,
                                  float innerAngle = 30.0f,
                                  float outerAngle = 45.0f,
                                  float radius = 15.0f) {
        auto entity = registry_.create();

        // Store direction as rotation in transform
        glm::quat rotation = SpotLight::rotationFromDirection(direction);
        registry_.emplace<Transform>(entity, Transform::withRotation(position, rotation));

        SpotLight light;
        light.color = color;
        light.intensity = intensity;
        light.innerConeAngle = innerAngle;
        light.outerConeAngle = outerAngle;
        light.radius = radius;
        registry_.emplace<SpotLight>(entity, light);
        registry_.emplace<LightEnabled>(entity);

        return entity;
    }

    // Create a light attached to another entity
    entt::entity createAttachedLight(entt::entity parent,
                                      const glm::vec3& offset,
                                      const glm::vec3& color = glm::vec3{1.0f},
                                      float intensity = 1.0f,
                                      float radius = 5.0f) {
        auto entity = createPointLight(glm::vec3{0.0f}, color, intensity, radius);
        registry_.emplace<LightAttachment>(entity, LightAttachment{parent, offset});
        return entity;
    }

    // Enable/disable a light
    void setLightEnabled(entt::entity light, bool enabled) {
        if (!registry_.valid(light)) return;

        if (enabled && !registry_.all_of<LightEnabled>(light)) {
            registry_.emplace<LightEnabled>(light);
        } else if (!enabled && registry_.all_of<LightEnabled>(light)) {
            registry_.remove<LightEnabled>(light);
        }
    }

    // Get all enabled point lights for rendering
    auto getEnabledPointLights() {
        return registry_.view<Transform, PointLight, LightEnabled>();
    }

    // Get all enabled spot lights for rendering
    auto getEnabledSpotLights() {
        return registry_.view<Transform, SpotLight, LightEnabled>();
    }

    // ========================================================================
    // NPC Entity Creation
    // ========================================================================

    // Create an NPC entity with basic AI
    entt::entity createNPC(const glm::vec3& position,
                            const std::string& name = "NPC",
                            float yaw = 0.0f) {
        auto entity = registry_.create();

        registry_.emplace<Transform>(entity, Transform::withYaw(position, yaw));
        registry_.emplace<Velocity>(entity);
        registry_.emplace<NPCTag>(entity);
        registry_.emplace<AIState>(entity);
        registry_.emplace<MovementSettings>(entity);
        registry_.emplace<NameTag>(entity, NameTag{name});
        registry_.emplace<Health>(entity);
        registry_.emplace<ModelMatrix>(entity);

        return entity;
    }

    // Create an NPC with a patrol path
    entt::entity createPatrolNPC(const glm::vec3& startPosition,
                                  const std::vector<glm::vec3>& waypoints,
                                  const std::string& name = "Guard") {
        auto entity = createNPC(startPosition, name);

        PatrolPath patrol;
        patrol.waypoints = waypoints;
        patrol.currentWaypoint = 0;
        patrol.loop = true;
        registry_.emplace<PatrolPath>(entity, patrol);

        // Start in patrol state
        registry_.get<AIState>(entity).current = AIState::State::Patrol;

        return entity;
    }

    // Find all NPCs
    auto findAllNPCs() {
        return registry_.view<NPCTag, Transform>();
    }

    // Set NPC AI state
    void setNPCState(entt::entity npc, AIState::State state) {
        if (!registry_.valid(npc) || !registry_.all_of<AIState>(npc)) return;
        auto& aiState = registry_.get<AIState>(npc);
        aiState.current = state;
        aiState.stateTimer = 0.0f;
    }

    // ========================================================================
    // Extended Update
    // ========================================================================

    // Update all extended systems (lights and AI)
    void updateExtended(float deltaTime) {
        // Light systems
        lightAttachmentSystem(registry_);

        // AI systems
        aiStateTimerSystem(registry_, deltaTime);
        patrolSystem(registry_, deltaTime);
    }

    // ========================================================================
    // Scene Object Management (Unified)
    // ========================================================================

    // Create a scene object entity from a Renderable pointer
    // The Renderable is owned externally (by SceneBuilder or SceneObjectCollection)
    entt::entity createSceneObject(Renderable* renderable,
                                    RenderableSource::Type sourceType,
                                    size_t sourceIndex = 0) {
        auto entity = registry_.create();
        registry_.emplace<SceneObjectTag>(entity);
        registry_.emplace<RenderablePtr>(entity, RenderablePtr{renderable});
        registry_.emplace<RenderableSource>(entity, RenderableSource{sourceType, sourceIndex});
        registry_.emplace<FrustumCullable>(entity);
        return entity;
    }

    // Create scene object with transform component (for physics-driven objects)
    entt::entity createSceneObjectWithTransform(Renderable* renderable,
                                                 const glm::vec3& position,
                                                 RenderableSource::Type sourceType,
                                                 size_t sourceIndex = 0) {
        auto entity = createSceneObject(renderable, sourceType, sourceIndex);
        registry_.emplace<Transform>(entity, Transform::withPosition(position));
        return entity;
    }

    // Batch create scene objects from a vector of Renderables
    // Returns vector of created entity IDs
    std::vector<entt::entity> createSceneObjects(std::vector<Renderable>& renderables,
                                                  RenderableSource::Type sourceType) {
        std::vector<entt::entity> entities;
        entities.reserve(renderables.size());
        for (size_t i = 0; i < renderables.size(); ++i) {
            entities.push_back(createSceneObject(&renderables[i], sourceType, i));
        }
        return entities;
    }

    // Remove all scene objects of a specific source type
    // Call before rebuilding a collection to avoid stale entities
    void removeSceneObjectsBySource(RenderableSource::Type sourceType) {
        auto view = registry_.view<SceneObjectTag, RenderableSource>();
        std::vector<entt::entity> toRemove;
        for (auto entity : view) {
            if (view.get<RenderableSource>(entity).type == sourceType) {
                toRemove.push_back(entity);
            }
        }
        for (auto entity : toRemove) {
            registry_.destroy(entity);
        }
    }

    // Get all scene objects for rendering
    auto getAllSceneObjects() {
        return registry_.view<SceneObjectTag, RenderablePtr>();
    }

    // Get scene objects with specific source type
    auto getSceneObjectsBySource(RenderableSource::Type sourceType) {
        return registry_.view<SceneObjectTag, RenderablePtr, RenderableSource>();
    }

    // Get count of scene objects
    size_t getSceneObjectCount() const {
        return registry_.view<SceneObjectTag>().size();
    }

    // Register a SceneObjectCollection's renderables as ECS entities
    // Call after rebuildSceneObjects() to sync ECS with the collection
    // Template allows any type with getSceneObjects() method
    template<typename CollectionT>
    std::vector<entt::entity> registerCollection(CollectionT& collection,
                                                  RenderableSource::Type sourceType) {
        // Remove existing entities from this source first
        removeSceneObjectsBySource(sourceType);
        // Create new entities for all renderables
        return createSceneObjects(collection.getSceneObjects(), sourceType);
    }

    // Collect all renderables from registered scene objects into a vector
    // Useful for rendering pipeline that expects a flat vector
    void collectRenderables(std::vector<Renderable*>& out) {
        auto view = registry_.view<SceneObjectTag, RenderablePtr>();
        out.reserve(out.size() + view.size_hint());
        for (auto entity : view) {
            auto* ptr = view.get<RenderablePtr>(entity).renderable;
            if (ptr) {
                out.push_back(ptr);
            }
        }
    }

private:
    entt::registry registry_;
};
