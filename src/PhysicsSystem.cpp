#include "PhysicsSystem.h"

// Jolt Physics includes
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include <SDL3/SDL_log.h>
#include <cstdarg>
#include <thread>
#include <cmath>

// Memory allocation hooks for Jolt
JPH_SUPPRESS_WARNINGS

// Callback for traces
static void TraceImpl(const char* inFMT, ...) {
    va_list args;
    va_start(args, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, args);
    va_end(args);
    SDL_Log("Jolt: %s", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
// Callback for asserts
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint32_t inLine) {
    SDL_Log("Jolt Assert: %s:%u: (%s) %s", inFile, inLine, inExpression, inMessage ? inMessage : "");
    return true; // Break into debugger
}
#endif

// Layer definitions for object vs broadphase layers
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        objectToBroadPhase[PhysicsLayers::NON_MOVING] = JPH::BroadPhaseLayer(BroadPhaseLayers::NON_MOVING);
        objectToBroadPhase[PhysicsLayers::MOVING] = JPH::BroadPhaseLayer(BroadPhaseLayers::MOVING);
        objectToBroadPhase[PhysicsLayers::CHARACTER] = JPH::BroadPhaseLayer(BroadPhaseLayers::MOVING);
    }

    virtual uint32_t GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        JPH_ASSERT(inLayer < PhysicsLayers::NUM_LAYERS);
        return objectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
            case BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
            case BroadPhaseLayers::MOVING: return "MOVING";
            default: JPH_ASSERT(false); return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer objectToBroadPhase[PhysicsLayers::NUM_LAYERS];
};

// Determines which object layers can collide
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
            case PhysicsLayers::NON_MOVING:
                return inObject2 == PhysicsLayers::MOVING || inObject2 == PhysicsLayers::CHARACTER;
            case PhysicsLayers::MOVING:
                return true; // Moving objects collide with everything
            case PhysicsLayers::CHARACTER:
                return true; // Character collides with everything
            default:
                JPH_ASSERT(false);
                return false;
        }
    }
};

// Determines if an object and broadphase layer can collide
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case PhysicsLayers::NON_MOVING:
                return inLayer2 == JPH::BroadPhaseLayer(BroadPhaseLayers::MOVING);
            case PhysicsLayers::MOVING:
            case PhysicsLayers::CHARACTER:
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
    virtual void OnContactAdded(const JPH::CharacterVirtual* inCharacter,
                                 const JPH::BodyID& inBodyID2,
                                 const JPH::SubShapeID& inSubShapeID2,
                                 JPH::RVec3Arg inContactPosition,
                                 JPH::Vec3Arg inContactNormal,
                                 JPH::CharacterContactSettings& ioSettings) override {
        // Allow sliding on all surfaces
        ioSettings.mCanPushCharacter = true;
        ioSettings.mCanReceiveImpulses = true;
    }
};

// Static instances
static BPLayerInterfaceImpl broadPhaseLayerInterface;
static ObjectLayerPairFilterImpl objectLayerPairFilter;
static ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhaseLayerFilter;
static CharacterContactListener characterContactListener;

// Helper conversions
static inline JPH::Vec3 toJolt(const glm::vec3& v) {
    return JPH::Vec3(v.x, v.y, v.z);
}

static inline JPH::Quat toJolt(const glm::quat& q) {
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

static inline glm::vec3 toGLM(const JPH::Vec3& v) {
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

// Only define RVec3 overload if it's a different type (double precision mode)
#ifdef JPH_DOUBLE_PRECISION
static inline glm::vec3 toGLM(const JPH::RVec3& v) {
    return glm::vec3(static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ()));
}
#endif

static inline glm::quat toGLM(const JPH::Quat& q) {
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld() {
    shutdown();
}

bool PhysicsWorld::init() {
    if (initialized) return true;

    // Register allocation hook
    JPH::RegisterDefaultAllocator();

    // Install callbacks
    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    // Create factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt physics types
    JPH::RegisterTypes();

    // Create temp allocator (10 MB)
    tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

    // Create job system with thread count
    int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        numThreads
    );

    // Create physics system
    const uint32_t maxBodies = 1024;
    const uint32_t numBodyMutexes = 0; // Use default
    const uint32_t maxBodyPairs = 1024;
    const uint32_t maxContactConstraints = 1024;

    physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    physicsSystem->Init(
        maxBodies,
        numBodyMutexes,
        maxBodyPairs,
        maxContactConstraints,
        broadPhaseLayerInterface,
        objectVsBroadPhaseLayerFilter,
        objectLayerPairFilter
    );

    // Set gravity
    physicsSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    initialized = true;
    SDL_Log("Physics system initialized with %d worker threads", numThreads);
    return true;
}

void PhysicsWorld::shutdown() {
    if (!initialized) return;

    character.reset();
    physicsSystem.reset();
    jobSystem.reset();
    tempAllocator.reset();

    // Unregisters all types with the factory and cleans up the default material
    JPH::UnregisterTypes();

    // Destroy factory
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    initialized = false;
    SDL_Log("Physics system shutdown");
}

void PhysicsWorld::update(float deltaTime) {
    if (!initialized) return;

    // Fixed timestep physics with accumulator
    accumulatedTime += deltaTime;
    int numSteps = 0;

    while (accumulatedTime >= FIXED_TIMESTEP && numSteps < MAX_SUBSTEPS) {
        // Update character if exists
        if (character) {
            // Apply character input
            JPH::Vec3 currentVelocity = character->GetLinearVelocity();
            JPH::CharacterVirtual::EGroundState groundState = character->GetGroundState();
            bool onGround = groundState == JPH::CharacterVirtual::EGroundState::OnGround;

            JPH::Vec3 newVelocity;
            newVelocity.SetX(characterDesiredVelocity.x);
            newVelocity.SetZ(characterDesiredVelocity.z);

            // Handle vertical velocity
            if (characterWantsJump && onGround) {
                // Apply jump impulse
                newVelocity.SetY(5.0f);
                characterWantsJump = false;  // Consume jump request
            } else {
                // Preserve current vertical velocity
                newVelocity.SetY(currentVelocity.GetY());
            }

            // Apply gravity
            newVelocity += physicsSystem->GetGravity() * FIXED_TIMESTEP;

            character->SetLinearVelocity(newVelocity);

            // Update character physics
            JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
            JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(objectVsBroadPhaseLayerFilter, PhysicsLayers::CHARACTER);
            JPH::DefaultObjectLayerFilter objectLayerFilter(objectLayerPairFilter, PhysicsLayers::CHARACTER);
            JPH::BodyFilter bodyFilter;
            JPH::ShapeFilter shapeFilter;

            character->ExtendedUpdate(
                FIXED_TIMESTEP,
                physicsSystem->GetGravity(),
                updateSettings,
                broadPhaseFilter,
                objectLayerFilter,
                bodyFilter,
                shapeFilter,
                *tempAllocator
            );
        }

        // Step physics
        physicsSystem->Update(FIXED_TIMESTEP, 1, tempAllocator.get(), jobSystem.get());

        accumulatedTime -= FIXED_TIMESTEP;
        numSteps++;
    }

    // Prevent spiral of death
    if (accumulatedTime > FIXED_TIMESTEP * MAX_SUBSTEPS) {
        accumulatedTime = 0.0f;
    }
}

PhysicsBodyID PhysicsWorld::createTerrainDisc(float radius, float heightOffset) {
    if (!initialized) return INVALID_BODY_ID;

    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

    // Create a large flat box as the ground plane
    // Box is centered at Y = heightOffset - 0.5 so the top surface is at heightOffset
    const float groundThickness = 1.0f;
    JPH::BoxShapeSettings boxSettings(JPH::Vec3(radius, groundThickness * 0.5f, radius));

    JPH::ShapeSettings::ShapeResult shapeResult = boxSettings.Create();
    if (!shapeResult.IsValid()) {
        SDL_Log("Failed to create terrain box shape: %s", shapeResult.GetError().c_str());
        return INVALID_BODY_ID;
    }

    // Position so the top of the box is at heightOffset
    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(0.0, heightOffset - groundThickness * 0.5f, 0.0),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        PhysicsLayers::NON_MOVING
    );
    bodySettings.mFriction = 0.8f;
    bodySettings.mRestitution = 0.0f;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        SDL_Log("Failed to create terrain body");
        return INVALID_BODY_ID;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);

    SDL_Log("Created terrain ground plane with radius %.1f at Y=%.1f", radius, heightOffset);
    return body->GetID().GetIndexAndSequenceNumber();
}

PhysicsBodyID PhysicsWorld::createTerrainHeightfield(const float* samples, uint32_t sampleCount,
                                                      float worldSize, float heightScale) {
    if (!initialized) return INVALID_BODY_ID;
    if (!samples || sampleCount < 2) {
        SDL_Log("Invalid heightfield parameters");
        return INVALID_BODY_ID;
    }

    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

    // Jolt HeightFieldShape expects power-of-2 + 1 sample counts
    // The terrain uses normalized [0,1] heights, we need to convert
    // Jolt expects samples in row-major order with Y up

    // Create height samples for Jolt
    // The terrain shader centers heights: (h - 0.5) * heightScale
    // So heights range from -0.5*heightScale to +0.5*heightScale
    // We need to match this in physics
    std::vector<float> joltSamples(sampleCount * sampleCount);
    for (uint32_t i = 0; i < sampleCount * sampleCount; i++) {
        // Match shader: (sample - 0.5) * heightScale
        joltSamples[i] = (samples[i] - 0.5f) * heightScale;
    }

    // HeightFieldShapeSettings
    // sampleCount must be a power of 2 + 1 (e.g., 65, 129, 257, 513)
    // For a 512x512 terrain, we need to use 513 samples or downsample to 257
    // Let's use the samples as-is and let Jolt handle it

    // The scale parameter determines the XZ spacing between samples
    // worldSize covers sampleCount-1 intervals
    float xzScale = worldSize / (sampleCount - 1);

    JPH::HeightFieldShapeSettings heightFieldSettings(
        joltSamples.data(),
        JPH::Vec3(-worldSize * 0.5f, 0.0f, -worldSize * 0.5f),  // Offset: center terrain at origin
        JPH::Vec3(xzScale, 1.0f, xzScale),                       // Scale: XZ spacing, Y is direct
        sampleCount
    );

    // Set material properties
    heightFieldSettings.mMaterials.push_back(new JPH::PhysicsMaterial());

    JPH::ShapeSettings::ShapeResult shapeResult = heightFieldSettings.Create();
    if (!shapeResult.IsValid()) {
        SDL_Log("Failed to create heightfield shape: %s", shapeResult.GetError().c_str());
        return INVALID_BODY_ID;
    }

    // Create the body at the origin (the shape's offset handles positioning)
    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(0.0, 0.0, 0.0),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        PhysicsLayers::NON_MOVING
    );
    bodySettings.mFriction = 0.8f;
    bodySettings.mRestitution = 0.0f;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        SDL_Log("Failed to create heightfield body");
        return INVALID_BODY_ID;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);

    SDL_Log("Created terrain heightfield %ux%u, world size %.1f, height scale %.1f",
            sampleCount, sampleCount, worldSize, heightScale);
    return body->GetID().GetIndexAndSequenceNumber();
}

PhysicsBodyID PhysicsWorld::createBox(const glm::vec3& position, const glm::vec3& halfExtents,
                                       float mass, float friction, float restitution) {
    if (!initialized) return INVALID_BODY_ID;

    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

    JPH::BoxShapeSettings boxSettings(toJolt(halfExtents));
    JPH::ShapeSettings::ShapeResult shapeResult = boxSettings.Create();
    if (!shapeResult.IsValid()) {
        SDL_Log("Failed to create box shape");
        return INVALID_BODY_ID;
    }

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        PhysicsLayers::MOVING
    );
    bodySettings.mFriction = friction;
    bodySettings.mRestitution = restitution;
    bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    bodySettings.mMassPropertiesOverride.mMass = mass;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        SDL_Log("Failed to create box body");
        return INVALID_BODY_ID;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID().GetIndexAndSequenceNumber();
}

PhysicsBodyID PhysicsWorld::createSphere(const glm::vec3& position, float radius,
                                          float mass, float friction, float restitution) {
    if (!initialized) return INVALID_BODY_ID;

    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

    JPH::SphereShapeSettings sphereSettings(radius);
    JPH::ShapeSettings::ShapeResult shapeResult = sphereSettings.Create();
    if (!shapeResult.IsValid()) {
        SDL_Log("Failed to create sphere shape");
        return INVALID_BODY_ID;
    }

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(position.x, position.y, position.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        PhysicsLayers::MOVING
    );
    bodySettings.mFriction = friction;
    bodySettings.mRestitution = restitution;
    bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    bodySettings.mMassPropertiesOverride.mMass = mass;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        SDL_Log("Failed to create sphere body");
        return INVALID_BODY_ID;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
    return body->GetID().GetIndexAndSequenceNumber();
}

PhysicsBodyID PhysicsWorld::createStaticBox(const glm::vec3& position, const glm::vec3& halfExtents,
                                             const glm::quat& rotation) {
    if (!initialized) return INVALID_BODY_ID;

    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();

    JPH::BoxShapeSettings boxSettings(toJolt(halfExtents));
    JPH::ShapeSettings::ShapeResult shapeResult = boxSettings.Create();
    if (!shapeResult.IsValid()) {
        SDL_Log("Failed to create static box shape");
        return INVALID_BODY_ID;
    }

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(position.x, position.y, position.z),
        toJolt(rotation),
        JPH::EMotionType::Static,
        PhysicsLayers::NON_MOVING
    );
    bodySettings.mFriction = 0.5f;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        SDL_Log("Failed to create static box body");
        return INVALID_BODY_ID;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return body->GetID().GetIndexAndSequenceNumber();
}

bool PhysicsWorld::createCharacter(const glm::vec3& position, float height, float radius) {
    if (!initialized) return false;

    characterHeight = height;
    characterRadius = radius;

    // Create a capsule shape for the character
    // Capsule height is the cylinder height (excluding hemispheres)
    float cylinderHeight = height - 2.0f * radius;
    if (cylinderHeight < 0.0f) cylinderHeight = 0.01f;

    JPH::RefConst<JPH::Shape> standingShape = new JPH::CapsuleShape(cylinderHeight * 0.5f, radius);

    JPH::CharacterVirtualSettings settings;
    settings.mShape = standingShape;
    settings.mMaxSlopeAngle = JPH::DegreesToRadians(45.0f);
    settings.mMaxStrength = 100.0f;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mCharacterPadding = 0.02f;
    settings.mPenetrationRecoverySpeed = 1.0f;
    settings.mPredictiveContactDistance = 0.1f;
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);

    // Position the character so feet are at the given Y
    JPH::RVec3 characterPos(position.x, position.y + height * 0.5f, position.z);

    character = std::make_unique<JPH::CharacterVirtual>(
        &settings,
        characterPos,
        JPH::Quat::sIdentity(),
        0,  // User data
        physicsSystem.get()
    );
    character->SetListener(&characterContactListener);

    SDL_Log("Created character controller at (%.1f, %.1f, %.1f)", position.x, position.y, position.z);
    return true;
}

void PhysicsWorld::updateCharacter(float deltaTime, const glm::vec3& desiredVelocity, bool jump) {
    if (!character || !initialized) return;

    // Store the desired velocity and jump request for the fixed timestep update
    characterDesiredVelocity = desiredVelocity;
    characterWantsJump = jump;
}

glm::vec3 PhysicsWorld::getCharacterPosition() const {
    if (!character) return glm::vec3(0.0f);

    JPH::RVec3 pos = character->GetPosition();
    // Return foot position (bottom of character)
    return glm::vec3(
        static_cast<float>(pos.GetX()),
        static_cast<float>(pos.GetY()) - characterHeight * 0.5f,
        static_cast<float>(pos.GetZ())
    );
}

glm::vec3 PhysicsWorld::getCharacterVelocity() const {
    if (!character) return glm::vec3(0.0f);
    return toGLM(character->GetLinearVelocity());
}

bool PhysicsWorld::isCharacterOnGround() const {
    if (!character) return false;
    return character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

PhysicsBodyInfo PhysicsWorld::getBodyInfo(PhysicsBodyID bodyID) const {
    PhysicsBodyInfo info;
    if (!initialized || bodyID == INVALID_BODY_ID) return info;

    JPH::BodyID joltID;
    joltID = JPH::BodyID(bodyID);

    const JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
    if (!bodyInterface.IsAdded(joltID)) return info;

    info.bodyID = bodyID;
    info.position = toGLM(bodyInterface.GetPosition(joltID));
    info.rotation = toGLM(bodyInterface.GetRotation(joltID));
    info.linearVelocity = toGLM(bodyInterface.GetLinearVelocity(joltID));
    info.isAwake = bodyInterface.IsActive(joltID);

    return info;
}

void PhysicsWorld::setBodyPosition(PhysicsBodyID bodyID, const glm::vec3& position) {
    if (!initialized || bodyID == INVALID_BODY_ID) return;

    JPH::BodyID joltID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
    if (!bodyInterface.IsAdded(joltID)) return;

    bodyInterface.SetPosition(joltID, JPH::RVec3(position.x, position.y, position.z), JPH::EActivation::Activate);
}

void PhysicsWorld::setBodyVelocity(PhysicsBodyID bodyID, const glm::vec3& velocity) {
    if (!initialized || bodyID == INVALID_BODY_ID) return;

    JPH::BodyID joltID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
    if (!bodyInterface.IsAdded(joltID)) return;

    bodyInterface.SetLinearVelocity(joltID, toJolt(velocity));
}

void PhysicsWorld::applyImpulse(PhysicsBodyID bodyID, const glm::vec3& impulse) {
    if (!initialized || bodyID == INVALID_BODY_ID) return;

    JPH::BodyID joltID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
    if (!bodyInterface.IsAdded(joltID)) return;

    bodyInterface.AddImpulse(joltID, toJolt(impulse));
}

glm::mat4 PhysicsWorld::getBodyTransform(PhysicsBodyID bodyID) const {
    if (!initialized || bodyID == INVALID_BODY_ID) {
        return glm::mat4(1.0f);
    }

    JPH::BodyID joltID(bodyID);
    const JPH::BodyInterface& bodyInterface = physicsSystem->GetBodyInterface();
    if (!bodyInterface.IsAdded(joltID)) {
        return glm::mat4(1.0f);
    }

    JPH::RVec3 position = bodyInterface.GetPosition(joltID);
    JPH::Quat rotation = bodyInterface.GetRotation(joltID);

    glm::vec3 pos = toGLM(position);
    glm::quat rot = toGLM(rotation);

    glm::mat4 transform = glm::mat4(1.0f);
    transform = glm::translate(transform, pos);
    transform *= glm::mat4_cast(rot);

    return transform;
}

int PhysicsWorld::getActiveBodyCount() const {
    if (!initialized) return 0;
    return static_cast<int>(physicsSystem->GetNumActiveBodies(JPH::EBodyType::RigidBody));
}
