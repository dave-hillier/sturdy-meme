#pragma once

#include "Components.h"
#include "Systems.h"
#include "TransformHierarchy.h"
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

    // Create a light attached to another entity (uses Hierarchy system)
    entt::entity createAttachedLight(entt::entity parent,
                                      const glm::vec3& offset,
                                      const glm::vec3& color = glm::vec3{1.0f},
                                      float intensity = 1.0f,
                                      float radius = 5.0f) {
        // Create light as child entity with offset as local position
        auto entity = registry_.create();
        registry_.emplace<Transform>(entity, Transform::withPosition(offset));
        registry_.emplace<PointLight>(entity, PointLight{{color, intensity, radius}});
        registry_.emplace<LightEnabled>(entity);
        registry_.emplace<WorldTransform>(entity);

        // Set up hierarchy - light is child of parent entity
        registry_.emplace<Hierarchy>(entity, Hierarchy::withParent(parent));

        // Ensure parent has Hierarchy component
        if (registry_.valid(parent)) {
            if (!registry_.all_of<Hierarchy>(parent)) {
                registry_.emplace<Hierarchy>(parent, Hierarchy::root());
            }
            registry_.get<Hierarchy>(parent).children.push_back(entity);

            // Ensure parent has WorldTransform for hierarchy system
            if (!registry_.all_of<WorldTransform>(parent)) {
                registry_.emplace<WorldTransform>(parent);
            }
        }

        // Update depth
        TransformHierarchy::updateDepth(registry_, entity);

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

    // Get a light's world position (handles both hierarchy and non-hierarchy lights)
    // Use this instead of reading Transform.position directly for lights!
    glm::vec3 getLightWorldPosition(entt::entity light) const {
        if (registry_.all_of<WorldTransform>(light)) {
            return registry_.get<WorldTransform>(light).getWorldPosition();
        }
        if (registry_.all_of<Transform>(light)) {
            return registry_.get<Transform>(light).position;
        }
        return glm::vec3(0.0f);
    }

    // Get a light's world rotation (handles both hierarchy and non-hierarchy lights)
    glm::quat getLightWorldRotation(entt::entity light) const {
        if (registry_.all_of<WorldTransform>(light)) {
            return registry_.get<WorldTransform>(light).getWorldRotation();
        }
        if (registry_.all_of<Transform>(light)) {
            return registry_.get<Transform>(light).rotation;
        }
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
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
    // Transform Hierarchy
    // ========================================================================

    // Create an entity as a child of another entity
    entt::entity createChildEntity(entt::entity parent,
                                    const glm::vec3& localPosition = glm::vec3(0.0f),
                                    const glm::quat& localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                    const glm::vec3& localScale = glm::vec3(1.0f)) {
        auto entity = registry_.create();
        registry_.emplace<Transform>(entity, Transform::withAll(localPosition, localRotation, localScale));
        registry_.emplace<Hierarchy>(entity, Hierarchy::withParent(parent));
        registry_.emplace<WorldTransform>(entity);

        // Add to parent's children list
        if (registry_.valid(parent)) {
            if (!registry_.all_of<Hierarchy>(parent)) {
                registry_.emplace<Hierarchy>(parent, Hierarchy::root());
            }
            registry_.get<Hierarchy>(parent).children.push_back(entity);
        }

        // Update depth
        TransformHierarchy::updateDepth(registry_, entity);

        return entity;
    }

    // Create a root entity with hierarchy support (can have children)
    entt::entity createHierarchyRoot(const glm::vec3& position = glm::vec3(0.0f),
                                      const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                      const glm::vec3& scale = glm::vec3(1.0f)) {
        auto entity = registry_.create();
        registry_.emplace<Transform>(entity, Transform::withAll(position, rotation, scale));
        registry_.emplace<Hierarchy>(entity, Hierarchy::root());
        registry_.emplace<WorldTransform>(entity);
        return entity;
    }

    // Set parent of an entity
    void setParent(entt::entity child, entt::entity parent) {
        TransformHierarchy::setParent(registry_, child, parent);
    }

    // Remove entity from parent (make it a root)
    void removeFromParent(entt::entity entity) {
        TransformHierarchy::removeFromParent(registry_, entity);
    }

    // Get parent of an entity
    entt::entity getParent(entt::entity entity) const {
        if (!registry_.valid(entity) || !registry_.all_of<Hierarchy>(entity)) {
            return entt::null;
        }
        return registry_.get<Hierarchy>(entity).parent;
    }

    // Get children of an entity
    std::vector<entt::entity> getChildren(entt::entity entity) const {
        if (!registry_.valid(entity) || !registry_.all_of<Hierarchy>(entity)) {
            return {};
        }
        return registry_.get<Hierarchy>(entity).children;
    }

    // Get the world transform matrix for an entity
    const glm::mat4& getWorldMatrix(entt::entity entity) {
        return TransformHierarchy::ensureWorldTransform(registry_, entity);
    }

    // Get world position of an entity
    glm::vec3 getWorldPosition(entt::entity entity) {
        const glm::mat4& world = getWorldMatrix(entity);
        return glm::vec3(world[3]);
    }

    // Set world position of an entity (calculates required local position)
    void setWorldPosition(entt::entity entity, const glm::vec3& worldPos) {
        TransformHierarchy::setWorldPosition(registry_, entity, worldPos);
    }

    // Set local position of an entity (relative to parent)
    void setLocalPosition(entt::entity entity, const glm::vec3& localPos) {
        TransformHierarchy::setLocalPosition(registry_, entity, localPos);
    }

    // Set local rotation of an entity (relative to parent)
    void setLocalRotation(entt::entity entity, const glm::quat& localRot) {
        TransformHierarchy::setLocalRotation(registry_, entity, localRot);
    }

    // Set local scale of an entity (relative to parent)
    void setLocalScale(entt::entity entity, const glm::vec3& localScale) {
        TransformHierarchy::setLocalScale(registry_, entity, localScale);
    }

    // Make entity look at a target in world space
    void lookAt(entt::entity entity, const glm::vec3& target) {
        TransformHierarchy::lookAt(registry_, entity, target);
    }

    // ========================================================================
    // Extended Update
    // ========================================================================

    // Update all extended systems (hierarchy, lights, and AI)
    void updateExtended(float deltaTime) {
        // Transform hierarchy system - must run before light attachments
        TransformHierarchy::transformHierarchySystem(registry_);

        // Light systems
        lightAttachmentSystem(registry_);

        // AI systems
        aiStateTimerSystem(registry_, deltaTime);
        patrolSystem(registry_, deltaTime);
    }

    // Update only the transform hierarchy (for use in specific update loops)
    void updateTransformHierarchy() {
        TransformHierarchy::transformHierarchySystem(registry_);
    }

private:
    entt::registry registry_;
};
