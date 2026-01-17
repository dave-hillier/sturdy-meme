#include "PhysicsSystem.h"
#include "JoltLayerConfig.h"
#include "JoltRuntime.h"
#include "PhysicsConversions.h"
#include "TerrainHeight.h"

// Jolt Physics includes
#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>

#include <SDL3/SDL_log.h>
#include <thread>
#include <cmath>
#include <algorithm>

JPH_SUPPRESS_WARNINGS

using namespace PhysicsConversions;

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld() {
    // RAII cleanup: reset unique_ptrs in reverse order of creation
    physicsSystem_.reset();
    jobSystem_.reset();
    tempAllocator_.reset();

    // Release our reference to the Jolt runtime
    // When the last PhysicsWorld is destroyed, this will trigger JoltRuntime cleanup
    joltRuntime_.reset();

    SDL_Log("Physics system shutdown");
}

PhysicsWorld::PhysicsWorld(PhysicsWorld&& other) noexcept
    : joltRuntime_(std::move(other.joltRuntime_))
    , tempAllocator_(std::move(other.tempAllocator_))
    , jobSystem_(std::move(other.jobSystem_))
    , physicsSystem_(std::move(other.physicsSystem_))
    , character_(std::move(other.character_))
    , accumulatedTime_(other.accumulatedTime_) {
}

PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&& other) noexcept {
    if (this != &other) {
        // Clean up existing resources
        physicsSystem_.reset();
        jobSystem_.reset();
        tempAllocator_.reset();
        joltRuntime_.reset();

        // Move resources from other
        joltRuntime_ = std::move(other.joltRuntime_);
        tempAllocator_ = std::move(other.tempAllocator_);
        jobSystem_ = std::move(other.jobSystem_);
        physicsSystem_ = std::move(other.physicsSystem_);
        character_ = std::move(other.character_);
        accumulatedTime_ = other.accumulatedTime_;
    }
    return *this;
}

std::optional<PhysicsWorld> PhysicsWorld::create() {
    PhysicsWorld world;
    if (!world.initInternal()) {
        return std::nullopt;
    }
    return world;
}

bool PhysicsWorld::initInternal() {
    // Acquire shared Jolt runtime (thread-safe, ref-counted)
    joltRuntime_ = JoltRuntime::acquire();

    // Create temp allocator (10 MB)
    tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

    // Create job system with thread count
    int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        numThreads
    );

    // Create physics system
    const uint32_t maxBodies = 1024;
    const uint32_t numBodyMutexes = 0; // Use default
    const uint32_t maxBodyPairs = 1024;
    const uint32_t maxContactConstraints = 1024;

    physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
    physicsSystem_->Init(
        maxBodies,
        numBodyMutexes,
        maxBodyPairs,
        maxContactConstraints,
        g_broadPhaseLayerInterface,
        g_objectVsBroadPhaseLayerFilter,
        g_objectLayerPairFilter
    );

    // Set gravity
    physicsSystem_->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

    SDL_Log("Physics system initialized with %d worker threads", numThreads);
    return true;
}

void PhysicsWorld::update(float deltaTime) {
    // Fixed timestep physics with accumulator
    accumulatedTime_ += deltaTime;
    int numSteps = 0;

    while (accumulatedTime_ >= FIXED_TIMESTEP && numSteps < MAX_SUBSTEPS) {
        // Update character if exists
        if (character_.isValid()) {
            character_.update(FIXED_TIMESTEP, physicsSystem_.get(), tempAllocator_.get());
        }

        // Step physics
        physicsSystem_->Update(FIXED_TIMESTEP, 1, tempAllocator_.get(), jobSystem_.get());

        accumulatedTime_ -= FIXED_TIMESTEP;
        numSteps++;
    }

    // Prevent spiral of death
    if (accumulatedTime_ > FIXED_TIMESTEP * MAX_SUBSTEPS) {
        accumulatedTime_ = 0.0f;
    }
}

PhysicsBodyID PhysicsWorld::createTerrainDisc(float radius, float heightOffset) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

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

// Internal helper that consolidates all heightfield creation logic
PhysicsBodyID PhysicsWorld::createHeightfieldInternal(const float* samples, const uint8_t* holeMask,
                                                       uint32_t sampleCount, float worldSize,
                                                       float heightScale, const glm::vec3& worldPosition,
                                                       bool useHalfTexelOffset) {
    if (!samples || sampleCount < 2) {
        SDL_Log("Invalid heightfield parameters");
        return INVALID_BODY_ID;
    }

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    // Convert to world-space heights and apply hole mask
    std::vector<float> joltSamples(sampleCount * sampleCount);
    for (uint32_t i = 0; i < sampleCount * sampleCount; i++) {
        bool isHole = holeMask && holeMask[i] > 127;
        if (isHole) {
            joltSamples[i] = JPH::HeightFieldShapeConstants::cNoCollisionValue;
        } else {
            joltSamples[i] = TerrainHeight::toWorld(samples[i], heightScale);
        }
    }

    // XZ spacing: sampleCount samples span (sampleCount-1) intervals
    float xzScale = worldSize / (sampleCount - 1);

    // Calculate offset
    float offsetX = -worldSize * 0.5f;
    float offsetZ = -worldSize * 0.5f;

    // Half-texel offset for tiled terrain to align with GPU texture sampling
    if (useHalfTexelOffset) {
        float halfTexel = (worldSize / sampleCount) * 0.5f;
        offsetX -= halfTexel;
        offsetZ -= halfTexel;
    }

    JPH::HeightFieldShapeSettings heightFieldSettings(
        joltSamples.data(),
        JPH::Vec3(offsetX, 0.0f, offsetZ),
        JPH::Vec3(xzScale, 1.0f, xzScale),
        sampleCount
    );

    // Set material properties
    heightFieldSettings.mMaterials.push_back(new JPH::PhysicsMaterial());

    JPH::ShapeSettings::ShapeResult shapeResult = heightFieldSettings.Create();
    if (!shapeResult.IsValid()) {
        SDL_Log("Failed to create heightfield shape: %s", shapeResult.GetError().c_str());
        return INVALID_BODY_ID;
    }

    // Create the body at the specified world position
    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(worldPosition.x, worldPosition.y, worldPosition.z),
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

    if (worldPosition == glm::vec3(0.0f)) {
        SDL_Log("Created terrain heightfield %ux%u, world size %.1f, height scale %.1f",
                sampleCount, sampleCount, worldSize, heightScale);
    }

    return body->GetID().GetIndexAndSequenceNumber();
}

PhysicsBodyID PhysicsWorld::createTerrainHeightfield(const float* samples, uint32_t sampleCount,
                                                      float worldSize, float heightScale) {
    return createHeightfieldInternal(samples, nullptr, sampleCount, worldSize, heightScale,
                                     glm::vec3(0.0f), false);
}

PhysicsBodyID PhysicsWorld::createTerrainHeightfield(const float* samples, const uint8_t* holeMask,
                                                      uint32_t sampleCount, float worldSize, float heightScale) {
    return createHeightfieldInternal(samples, holeMask, sampleCount, worldSize, heightScale,
                                     glm::vec3(0.0f), false);
}

PhysicsBodyID PhysicsWorld::createTerrainHeightfieldAtPosition(const float* samples, uint32_t sampleCount,
                                                                 float tileWorldSize, float heightScale,
                                                                 const glm::vec3& worldPosition) {
    return createHeightfieldInternal(samples, nullptr, sampleCount, tileWorldSize, heightScale,
                                     worldPosition, true);
}

PhysicsBodyID PhysicsWorld::createTerrainHeightfieldAtPosition(const float* samples, const uint8_t* holeMask,
                                                                 uint32_t sampleCount, float tileWorldSize,
                                                                 float heightScale, const glm::vec3& worldPosition) {
    return createHeightfieldInternal(samples, holeMask, sampleCount, tileWorldSize, heightScale,
                                     worldPosition, true);
}

PhysicsBodyID PhysicsWorld::createBox(const glm::vec3& position, const glm::vec3& halfExtents,
                                       float mass, float friction, float restitution) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

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
    bodySettings.mLinearDamping = 0.05f;
    bodySettings.mAngularDamping = 0.05f;

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
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

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
    bodySettings.mLinearDamping = 0.05f;
    bodySettings.mAngularDamping = 0.05f;

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
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

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

PhysicsBodyID PhysicsWorld::createStaticConvexHull(const glm::vec3& position, const glm::vec3* vertices,
                                                    size_t vertexCount, float scale,
                                                    const glm::quat& rotation) {
    if (vertexCount < 4) return INVALID_BODY_ID;

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    // Convert glm vertices to Jolt Vec3 array with scale applied
    std::vector<JPH::Vec3> joltVertices;
    joltVertices.reserve(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        joltVertices.push_back(JPH::Vec3(vertices[i].x * scale, vertices[i].y * scale, vertices[i].z * scale));
    }

    // Create convex hull from vertices
    JPH::ConvexHullShapeSettings hullSettings(joltVertices.data(), static_cast<int>(joltVertices.size()));
    hullSettings.mMaxConvexRadius = 0.05f;
    JPH::ShapeSettings::ShapeResult shapeResult = hullSettings.Create();
    if (!shapeResult.IsValid()) {
        SDL_Log("Failed to create convex hull shape: %s", shapeResult.GetError().c_str());
        return INVALID_BODY_ID;
    }

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(position.x, position.y, position.z),
        toJolt(rotation),
        JPH::EMotionType::Static,
        PhysicsLayers::NON_MOVING
    );
    bodySettings.mFriction = 0.7f;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        SDL_Log("Failed to create convex hull body");
        return INVALID_BODY_ID;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return body->GetID().GetIndexAndSequenceNumber();
}

PhysicsBodyID PhysicsWorld::createStaticCapsule(const glm::vec3& position, float halfHeight, float radius,
                                                 const glm::quat& rotation) {
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    // Jolt capsules are oriented along the Y axis by default
    JPH::CapsuleShapeSettings capsuleSettings(halfHeight, radius);
    JPH::ShapeSettings::ShapeResult shapeResult = capsuleSettings.Create();
    if (!shapeResult.IsValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create static capsule shape: %s",
                     shapeResult.GetError().c_str());
        return INVALID_BODY_ID;
    }

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(position.x, position.y, position.z),
        toJolt(rotation),
        JPH::EMotionType::Static,
        PhysicsLayers::NON_MOVING
    );
    bodySettings.mFriction = 0.6f;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create static capsule body");
        return INVALID_BODY_ID;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    return body->GetID().GetIndexAndSequenceNumber();
}

PhysicsBodyID PhysicsWorld::createStaticCompoundCapsules(const glm::vec3& position,
                                                          const std::vector<CapsuleData>& capsules,
                                                          const glm::quat& rotation) {
    if (capsules.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "createStaticCompoundCapsules called with empty capsule list");
        return INVALID_BODY_ID;
    }

    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    // Build compound shape from multiple capsules
    JPH::StaticCompoundShapeSettings compoundSettings;
    compoundSettings.mSubShapes.reserve(capsules.size());

    for (const auto& capsule : capsules) {
        // Create capsule shape (Jolt capsules are Y-axis aligned by default)
        auto capsuleShape = new JPH::CapsuleShape(capsule.halfHeight, capsule.radius);

        // Add to compound with local transform
        compoundSettings.AddShape(
            toJolt(capsule.localPosition),
            toJolt(capsule.localRotation),
            capsuleShape
        );
    }

    JPH::ShapeSettings::ShapeResult shapeResult = compoundSettings.Create();
    if (!shapeResult.IsValid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create compound capsule shape: %s",
                     shapeResult.GetError().c_str());
        return INVALID_BODY_ID;
    }

    JPH::BodyCreationSettings bodySettings(
        shapeResult.Get(),
        JPH::RVec3(position.x, position.y, position.z),
        toJolt(rotation),
        JPH::EMotionType::Static,
        PhysicsLayers::NON_MOVING
    );
    bodySettings.mFriction = 0.6f;

    JPH::Body* body = bodyInterface.CreateBody(bodySettings);
    if (!body) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create compound capsule body");
        return INVALID_BODY_ID;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    SDL_Log("Created compound shape with %zu capsules", capsules.size());
    return body->GetID().GetIndexAndSequenceNumber();
}

bool PhysicsWorld::createCharacter(const glm::vec3& position, float height, float radius) {
    return character_.create(physicsSystem_.get(), position, height, radius);
}

void PhysicsWorld::updateCharacter(float deltaTime, const glm::vec3& desiredVelocity, bool jump) {
    character_.setInput(desiredVelocity, jump);
}

void PhysicsWorld::setCharacterPosition(const glm::vec3& position) {
    character_.setPosition(position);
}

glm::vec3 PhysicsWorld::getCharacterPosition() const {
    return character_.getPosition();
}

glm::vec3 PhysicsWorld::getCharacterVelocity() const {
    return character_.getVelocity();
}

bool PhysicsWorld::isCharacterOnGround() const {
    return character_.isOnGround();
}

PhysicsBodyInfo PhysicsWorld::getBodyInfo(PhysicsBodyID bodyID) const {
    PhysicsBodyInfo info;
    if (bodyID == INVALID_BODY_ID) return info;

    JPH::BodyID joltID;
    joltID = JPH::BodyID(bodyID);

    const JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    if (!bodyInterface.IsAdded(joltID)) return info;

    info.bodyID = bodyID;
    info.position = toGLM(bodyInterface.GetPosition(joltID));
    info.rotation = toGLM(bodyInterface.GetRotation(joltID));
    info.linearVelocity = toGLM(bodyInterface.GetLinearVelocity(joltID));
    info.isAwake = bodyInterface.IsActive(joltID);

    return info;
}

void PhysicsWorld::setBodyPosition(PhysicsBodyID bodyID, const glm::vec3& position) {
    if (bodyID == INVALID_BODY_ID) return;

    JPH::BodyID joltID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    if (!bodyInterface.IsAdded(joltID)) return;

    bodyInterface.SetPosition(joltID, JPH::RVec3(position.x, position.y, position.z), JPH::EActivation::Activate);
}

void PhysicsWorld::setBodyVelocity(PhysicsBodyID bodyID, const glm::vec3& velocity) {
    if (bodyID == INVALID_BODY_ID) return;

    JPH::BodyID joltID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    if (!bodyInterface.IsAdded(joltID)) return;

    bodyInterface.SetLinearVelocity(joltID, toJolt(velocity));
}

void PhysicsWorld::applyImpulse(PhysicsBodyID bodyID, const glm::vec3& impulse) {
    if (bodyID == INVALID_BODY_ID) return;

    JPH::BodyID joltID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
    if (!bodyInterface.IsAdded(joltID)) return;

    bodyInterface.AddImpulse(joltID, toJolt(impulse));
}

glm::mat4 PhysicsWorld::getBodyTransform(PhysicsBodyID bodyID) const {
    if (bodyID == INVALID_BODY_ID) {
        return glm::mat4(1.0f);
    }

    JPH::BodyID joltID(bodyID);
    const JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();
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
    return static_cast<int>(physicsSystem_->GetNumActiveBodies(JPH::EBodyType::RigidBody));
}

std::vector<RaycastHit> PhysicsWorld::castRayAllHits(const glm::vec3& from, const glm::vec3& to) const {
    std::vector<RaycastHit> results;

    // Calculate ray direction and length
    glm::vec3 direction = to - from;
    float rayLength = glm::length(direction);
    if (rayLength < 0.001f) return results;

    direction = glm::normalize(direction);

    // Create Jolt ray
    JPH::RRayCast ray;
    ray.mOrigin = JPH::RVec3(from.x, from.y, from.z);
    ray.mDirection = JPH::Vec3(direction.x * rayLength, direction.y * rayLength, direction.z * rayLength);

    // Use NarrowPhaseQuery for accurate shape-level collision detection
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;

    // Use RayCastSettings with default backface mode
    JPH::RayCastSettings settings;

    // Cast the ray using narrowphase
    physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, settings, collector);

    // Convert results
    for (const auto& hit : collector.mHits) {
        RaycastHit result;
        result.hit = true;
        result.distance = hit.mFraction * rayLength;
        result.bodyId = hit.mBodyID.GetIndexAndSequenceNumber();
        result.position = from + direction * result.distance;
        results.push_back(result);
    }

    // Sort by distance
    std::sort(results.begin(), results.end(), [](const RaycastHit& a, const RaycastHit& b) {
        return a.distance < b.distance;
    });

    return results;
}

void PhysicsWorld::removeBody(PhysicsBodyID bodyID) {
    if (bodyID == INVALID_BODY_ID) return;

    JPH::BodyID joltID(bodyID);
    JPH::BodyInterface& bodyInterface = physicsSystem_->GetBodyInterface();

    if (bodyInterface.IsAdded(joltID)) {
        bodyInterface.RemoveBody(joltID);
        bodyInterface.DestroyBody(joltID);
    }
}
