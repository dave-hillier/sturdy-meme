#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

// Collision layers
namespace PhysicsLayers {
    constexpr uint8_t NON_MOVING = 0;
    constexpr uint8_t MOVING = 1;
    constexpr uint8_t CHARACTER = 2;
    constexpr uint8_t RAGDOLL = 3;      // Ragdoll bones: collide with NON_MOVING/MOVING, NOT with CHARACTER or self
    constexpr uint8_t NUM_LAYERS = 4;
}

// Broad phase layers
namespace BroadPhaseLayers {
    constexpr uint8_t NON_MOVING = 0;
    constexpr uint8_t MOVING = 1;
    constexpr uint8_t NUM_LAYERS = 2;
}

// Layer definitions for object vs broadphase layers
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        objectToBroadPhase_[PhysicsLayers::NON_MOVING] = JPH::BroadPhaseLayer(BroadPhaseLayers::NON_MOVING);
        objectToBroadPhase_[PhysicsLayers::MOVING] = JPH::BroadPhaseLayer(BroadPhaseLayers::MOVING);
        objectToBroadPhase_[PhysicsLayers::CHARACTER] = JPH::BroadPhaseLayer(BroadPhaseLayers::MOVING);
        objectToBroadPhase_[PhysicsLayers::RAGDOLL] = JPH::BroadPhaseLayer(BroadPhaseLayers::MOVING);
    }

    uint32_t GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        JPH_ASSERT(inLayer < PhysicsLayers::NUM_LAYERS);
        return objectToBroadPhase_[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case BroadPhaseLayers::MOVING: return "MOVING";
            default: JPH_ASSERT(false); return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer objectToBroadPhase_[PhysicsLayers::NUM_LAYERS];
};

// Determines which object layers can collide
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
            case PhysicsLayers::NON_MOVING:
                return inObject2 == PhysicsLayers::MOVING ||
                       inObject2 == PhysicsLayers::CHARACTER ||
                       inObject2 == PhysicsLayers::RAGDOLL;
            case PhysicsLayers::MOVING:
                return true; // Moving objects collide with everything
            case PhysicsLayers::CHARACTER:
                // Character collides with NON_MOVING and MOVING, but NOT ragdoll
                // (ragdoll bones are inside the character capsule)
                return inObject2 != PhysicsLayers::RAGDOLL;
            case PhysicsLayers::RAGDOLL:
                // Ragdoll collides with NON_MOVING and MOVING only
                // No self-collision, no character collision
                return inObject2 == PhysicsLayers::NON_MOVING ||
                       inObject2 == PhysicsLayers::MOVING;
            default:
                JPH_ASSERT(false);
                return false;
        }
    }
};

// Determines if an object and broadphase layer can collide
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case PhysicsLayers::NON_MOVING:
                return inLayer2 == JPH::BroadPhaseLayer(BroadPhaseLayers::MOVING);
            case PhysicsLayers::MOVING:
            case PhysicsLayers::CHARACTER:
            case PhysicsLayers::RAGDOLL:
                return true;
            default:
                JPH_ASSERT(false);
                return false;
        }
    }
};

// Contact listener for character
class CharacterContactListener : public JPH::CharacterContactListener {
public:
    void OnContactAdded(const JPH::CharacterVirtual* inCharacter,
                        const JPH::BodyID& inBodyID2,
                        const JPH::SubShapeID& inSubShapeID2,
                        JPH::RVec3Arg inContactPosition,
                        JPH::Vec3Arg inContactNormal,
                        JPH::CharacterContactSettings& ioSettings) override {
        // Allow character to be pushed and to push objects
        ioSettings.mCanPushCharacter = true;
        ioSettings.mCanReceiveImpulses = true;
    }
};

// Global instances (defined in .cpp file)
extern BPLayerInterfaceImpl g_broadPhaseLayerInterface;
extern ObjectLayerPairFilterImpl g_objectLayerPairFilter;
extern ObjectVsBroadPhaseLayerFilterImpl g_objectVsBroadPhaseLayerFilter;
extern CharacterContactListener g_characterContactListener;
