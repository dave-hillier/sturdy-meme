#pragma once

#include <glm/glm.hpp>
#include <memory>

// Forward declarations for Jolt types
namespace JPH {
    class CharacterVirtual;
    class PhysicsSystem;
    class TempAllocatorImpl;
}

// Character controller wrapping Jolt's CharacterVirtual
// Handles character physics separately from the main physics world
class CharacterController {
public:
    CharacterController();
    ~CharacterController();

    // Move-only (CharacterVirtual is not copyable)
    CharacterController(CharacterController&&) noexcept;
    CharacterController& operator=(CharacterController&&) noexcept;
    CharacterController(const CharacterController&) = delete;
    CharacterController& operator=(const CharacterController&) = delete;

    // Create the character at the given position
    // Returns true on success
    bool create(JPH::PhysicsSystem* physicsSystem, const glm::vec3& position,
                float height, float radius);

    // Update the character physics (called during fixed timestep)
    void update(float deltaTime, JPH::PhysicsSystem* physicsSystem,
                JPH::TempAllocatorImpl* tempAllocator);

    // Set desired movement input (called from game logic)
    void setInput(const glm::vec3& desiredVelocity, bool jump);

    // Position management
    void setPosition(const glm::vec3& position);
    glm::vec3 getPosition() const;
    glm::vec3 getVelocity() const;

    // Ground state
    bool isOnGround() const;

    // Ground surface information (Jolt already knows these; expose so callers avoid re-raycasting)
    glm::vec3 getGroundNormal() const;
    glm::vec3 getGroundVelocity() const;  // Full XYZ including horizontal platform velocity

    // Jump configuration
    void setJumpImpulse(float impulse) { jumpImpulse_ = impulse; }
    float getJumpImpulse() const { return jumpImpulse_; }

    // Check if character is created
    bool isValid() const { return character_ != nullptr; }

private:
    std::unique_ptr<JPH::CharacterVirtual> character_;
    float height_ = 1.8f;
    float radius_ = 0.3f;
    glm::vec3 desiredVelocity_{0.0f};
    bool wantsJump_ = false;
    float jumpImpulse_ = 5.0f;
};
