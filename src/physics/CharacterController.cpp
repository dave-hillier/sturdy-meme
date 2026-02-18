#include "CharacterController.h"
#include "PhysicsConversions.h"
#include "JoltLayerConfig.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Core/TempAllocator.h>

#include <SDL3/SDL_log.h>

using namespace PhysicsConversions;

CharacterController::CharacterController() = default;

CharacterController::~CharacterController() = default;

CharacterController::CharacterController(CharacterController&&) noexcept = default;

CharacterController& CharacterController::operator=(CharacterController&&) noexcept = default;

bool CharacterController::create(JPH::PhysicsSystem* physicsSystem, const glm::vec3& position,
                                  float height, float radius) {
    height_ = height;
    radius_ = radius;

    // Create a capsule shape for the character
    // Capsule height is the cylinder height (excluding hemispheres)
    float cylinderHeight = height - 2.0f * radius;
    if (cylinderHeight < 0.0f) cylinderHeight = 0.01f;

    JPH::RefConst<JPH::Shape> standingShape = new JPH::CapsuleShape(cylinderHeight * 0.5f, radius);

    JPH::CharacterVirtualSettings settings;
    settings.mShape = standingShape;
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(45.0f);
    settings.mMaxStrength = 25.0f;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mCharacterPadding = 0.05f;
    settings.mPenetrationRecoverySpeed = 0.4f;
    settings.mPredictiveContactDistance = 0.1f;
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
    settings.mMass = 70.0f;

    // Position the character so feet are at the given Y
    JPH::RVec3 characterPos(position.x, position.y + height * 0.5f, position.z);

    character_ = std::make_unique<JPH::CharacterVirtual>(
        &settings,
        characterPos,
        JPH::Quat::sIdentity(),
        0,  // User data
        physicsSystem
    );
    character_->SetListener(&g_characterContactListener);

    SDL_Log("Created character controller at (%.1f, %.1f, %.1f)", position.x, position.y, position.z);
    return true;
}

void CharacterController::update(float deltaTime, JPH::PhysicsSystem* physicsSystem,
                                  JPH::TempAllocatorImpl* tempAllocator) {
    if (!character_) return;

    // Apply character input following Jolt's CharacterVirtual documentation
    JPH::Vec3 currentVelocity = character_->GetLinearVelocity();
    JPH::CharacterVirtual::EGroundState groundState = character_->GetGroundState();
    bool onGround = groundState == JPH::CharacterVirtual::EGroundState::OnGround;

    JPH::Vec3 newVelocity;

    // Project desired horizontal velocity onto the ground plane when grounded.
    // Without this, near the slope limit (45°) the character oscillates between
    // OnGround and airborne because the input velocity has an uphill component.
    if (onGround) {
        JPH::Vec3 groundNormal = character_->GetGroundNormal();

        // Project desiredVelocity onto the ground plane: v - (v·n)n
        JPH::Vec3 desired(desiredVelocity_.x, desiredVelocity_.y, desiredVelocity_.z);
        float dot = desired.Dot(groundNormal);
        JPH::Vec3 projected = desired - groundNormal * dot;

        newVelocity.SetX(projected.GetX());
        newVelocity.SetZ(projected.GetZ());
    } else {
        newVelocity.SetX(desiredVelocity_.x);
        newVelocity.SetZ(desiredVelocity_.z);
    }

    // Vertical velocity per Jolt docs:
    // OnGround: groundVelocity + horizontal + optional jump + dt*gravity
    // Else: currentVertical + horizontal + dt*gravity
    if (onGround) {
        JPH::Vec3 groundVelocity = character_->GetGroundVelocity();

        // Include horizontal ground velocity for moving platform support.
        // Previously only the Y component was used, discarding platform horizontal motion.
        newVelocity.SetX(newVelocity.GetX() + groundVelocity.GetX());
        newVelocity.SetZ(newVelocity.GetZ() + groundVelocity.GetZ());

        float verticalVelocity = groundVelocity.GetY();

        if (wantsJump_) {
            verticalVelocity += jumpImpulse_;
            wantsJump_ = false;
        }

        newVelocity.SetY(verticalVelocity);
    } else {
        newVelocity.SetY(currentVelocity.GetY());
    }

    // Always apply gravity
    newVelocity += physicsSystem->GetGravity() * deltaTime;

    character_->SetLinearVelocity(newVelocity);

    // ExtendedUpdate - use zero gravity to avoid applying extra downward force
    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mStickToFloorStepDown = JPH::Vec3(0, -0.5f, 0);
    updateSettings.mWalkStairsStepUp = JPH::Vec3(0, 0.4f, 0);

    JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(g_objectVsBroadPhaseLayerFilter, PhysicsLayers::CHARACTER);
    JPH::DefaultObjectLayerFilter objectLayerFilter(g_objectLayerPairFilter, PhysicsLayers::CHARACTER);
    JPH::BodyFilter bodyFilter;
    JPH::ShapeFilter shapeFilter;

    character_->ExtendedUpdate(
        deltaTime,
        JPH::Vec3::sZero(),  // No extra gravity - we already applied it above
        updateSettings,
        broadPhaseFilter,
        objectLayerFilter,
        bodyFilter,
        shapeFilter,
        *tempAllocator
    );
}

void CharacterController::setInput(const glm::vec3& desiredVelocity, bool jump) {
    desiredVelocity_ = desiredVelocity;
    wantsJump_ = jump;
}

void CharacterController::setPosition(const glm::vec3& position) {
    if (!character_) return;

    // Character position is at center, so offset by half height
    JPH::RVec3 centerPos(position.x, position.y + height_ * 0.5f, position.z);
    character_->SetPosition(centerPos);
    // Reset velocity to avoid glitches when teleporting
    character_->SetLinearVelocity(JPH::Vec3::sZero());
}

glm::vec3 CharacterController::getPosition() const {
    if (!character_) return glm::vec3(0.0f);

    JPH::RVec3 pos = character_->GetPosition();
    // Return foot position (bottom of character)
    return glm::vec3(
        static_cast<float>(pos.GetX()),
        static_cast<float>(pos.GetY()) - height_ * 0.5f,
        static_cast<float>(pos.GetZ())
    );
}

glm::vec3 CharacterController::getVelocity() const {
    if (!character_) return glm::vec3(0.0f);
    return toGLM(character_->GetLinearVelocity());
}

bool CharacterController::isOnGround() const {
    if (!character_) return false;
    return character_->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

glm::vec3 CharacterController::getGroundNormal() const {
    if (!character_) return glm::vec3(0.0f, 1.0f, 0.0f);
    JPH::Vec3 n = character_->GetGroundNormal();
    // Normalise defensively; Jolt should return a unit normal, but handle degenerate cases
    float len = n.Length();
    if (len < 1e-6f) return glm::vec3(0.0f, 1.0f, 0.0f);
    return toGLM(n / len);
}

glm::vec3 CharacterController::getGroundVelocity() const {
    if (!character_) return glm::vec3(0.0f);
    return toGLM(character_->GetGroundVelocity());
}
