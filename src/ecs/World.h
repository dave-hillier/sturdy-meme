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
        registry_.emplace<Transform>(entity, Transform{position, yaw});
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

        registry_.emplace<Transform>(entity, Transform{position, 0.0f});
        PointLight light;
        light.color = color;
        light.intensity = intensity;
        light.radius = radius;
        registry_.emplace<PointLight>(entity, light);
        registry_.emplace<LightEnabled>(entity);

        return entity;
    }

    // Create a spot light entity
    entt::entity createSpotLight(const glm::vec3& position,
                                  const glm::vec3& direction,
                                  const glm::vec3& color = glm::vec3{1.0f},
                                  float intensity = 1.0f,
                                  float innerAngle = 30.0f,
                                  float outerAngle = 45.0f,
                                  float radius = 15.0f) {
        auto entity = registry_.create();

        registry_.emplace<Transform>(entity, Transform{position, 0.0f});
        SpotLight light;
        light.color = color;
        light.intensity = intensity;
        light.direction = glm::normalize(direction);
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

        registry_.emplace<Transform>(entity, Transform{position, yaw});
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
    // Camera Entity Creation
    // ========================================================================

    // Create a camera entity
    entt::entity createCamera(const glm::vec3& position,
                               float yaw = 0.0f,
                               float fov = 60.0f,
                               bool isMain = false) {
        auto entity = registry_.create();

        registry_.emplace<Transform>(entity, Transform{position, yaw});
        registry_.emplace<CameraComponent>(entity, CameraComponent{fov, 0.1f, 1000.0f, isMain ? 100 : 0});

        if (isMain) {
            // Remove MainCamera from any other entity first
            auto view = registry_.view<MainCamera>();
            for (auto other : view) {
                registry_.remove<MainCamera>(other);
            }
            registry_.emplace<MainCamera>(entity);
        }

        return entity;
    }

    // Find the main camera entity
    entt::entity findMainCamera() const {
        auto view = registry_.view<MainCamera>();
        return view.empty() ? entt::null : view.front();
    }

    // Set an entity as the main camera
    void setMainCamera(entt::entity camera) {
        if (!registry_.valid(camera)) return;

        // Remove MainCamera from all entities first
        auto view = registry_.view<MainCamera>();
        for (auto other : view) {
            registry_.remove<MainCamera>(other);
        }

        // Add to specified entity
        if (!registry_.all_of<MainCamera>(camera)) {
            registry_.emplace<MainCamera>(camera);
        }
    }

    // ========================================================================
    // Mesh Renderable Entity Creation
    // ========================================================================

    // Create a mesh renderable entity
    entt::entity createMeshEntity(const std::string& name,
                                   const glm::vec3& position,
                                   MeshHandle mesh = InvalidMesh,
                                   MaterialHandle material = InvalidMaterial) {
        auto entity = registry_.create();

        registry_.emplace<EntityInfo>(entity, EntityInfo{name, "M", true, false, 0});
        registry_.emplace<Transform>(entity, Transform{position, 0.0f});
        registry_.emplace<Hierarchy>(entity);
        registry_.emplace<WorldTransform>(entity);
        registry_.emplace<MeshRenderer>(entity, MeshRenderer{mesh, material});
        registry_.emplace<AABBBounds>(entity);

        return entity;
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

private:
    entt::registry registry_;
};
