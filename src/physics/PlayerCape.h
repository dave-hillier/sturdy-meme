#pragma once

#include "ClothSimulation.h"
#include "Mesh.h"
#include "GLTFLoader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

class WindSystem;

// Body collider definition for cape collision
struct BodyCollider {
    std::string boneName1;      // First bone for capsule (or single bone for sphere)
    std::string boneName2;      // Second bone for capsule (empty for sphere)
    float radius;               // Collider radius
    glm::vec3 offset1;          // Offset from bone1 position
    glm::vec3 offset2;          // Offset from bone2 position (for capsules)
    bool isCapsule;             // True for capsule, false for sphere
};

// Cape attachment point (pinned to a bone)
struct CapeAttachment {
    std::string boneName;       // Bone to attach to
    glm::vec3 localOffset;      // Offset from bone position
    int clothX;                 // Cloth grid X coordinate to pin
    int clothY;                 // Cloth grid Y coordinate to pin
};

// Debug visualization data for cape colliders
struct CapeDebugData {
    struct SphereCollider {
        glm::vec3 center;
        float radius;
    };
    struct CapsuleCollider {
        glm::vec3 point1;
        glm::vec3 point2;
        float radius;
    };
    std::vector<SphereCollider> spheres;
    std::vector<CapsuleCollider> capsules;
    std::vector<glm::vec3> attachmentPoints;
};

// Player cape with cloth simulation and body collision
class PlayerCape {
public:
    PlayerCape() = default;
    ~PlayerCape() = default;

    // Initialize cape with dimensions
    // width/height: cloth grid resolution
    // spacing: distance between cloth particles
    void create(int width, int height, float spacing);

    // Add a body collider for cape collision
    void addBodyCollider(const BodyCollider& collider);

    // Add an attachment point (pins cloth to a bone)
    void addAttachment(const CapeAttachment& attachment);

    // Setup default colliders for humanoid character (torso, arms, legs)
    void setupDefaultColliders();

    // Setup default attachments (shoulders/upper back)
    void setupDefaultAttachments();

    // Update cape simulation
    // skeleton: character skeleton with current pose
    // worldTransform: character world transform
    // deltaTime: time since last frame
    // windSystem: optional wind for cloth movement
    void update(const Skeleton& skeleton, const glm::mat4& worldTransform,
                float deltaTime, const WindSystem* windSystem = nullptr);

    // Get/update the mesh for rendering
    void createMesh(Mesh& mesh) const;
    void updateMesh(Mesh& mesh) const;

    // Check if cape is initialized
    bool isInitialized() const { return initialized; }

    // Get cloth simulation for debugging
    const ClothSimulation& getClothSimulation() const { return clothSim; }

    // Get debug visualization data (colliders and attachment points)
    CapeDebugData getDebugData() const;

    // Initialize cloth particle positions from skeleton (call once after skeleton is ready)
    // This prevents the cape from "snapping" from origin to the character
    void initializeFromSkeleton(const Skeleton& skeleton, const glm::mat4& worldTransform);

private:
    // Find bone world position from skeleton
    glm::vec3 getBoneWorldPosition(const Skeleton& skeleton, const glm::mat4& worldTransform,
                                    const std::string& boneName, const glm::vec3& offset) const;

    // Update attachment positions (pins cloth particles to bones)
    void updateAttachments(const Skeleton& skeleton, const glm::mat4& worldTransform);

    // Apply body colliders to cloth simulation
    void applyBodyColliders(const Skeleton& skeleton, const glm::mat4& worldTransform);

    ClothSimulation clothSim;
    std::vector<BodyCollider> bodyColliders;
    std::vector<CapeAttachment> attachments;
    std::vector<glm::mat4> cachedGlobalTransforms;  // Cache bone transforms

    int clothWidth = 0;
    int clothHeight = 0;
    float particleSpacing = 0.05f;
    bool initialized = false;
    bool positionsInitialized = false;  // True after first skeleton-based init
    mutable CapeDebugData lastDebugData;  // Cached debug data

    // Cape parameters
    static constexpr float CAPE_WIDTH = 0.5f;       // Half-width in meters
    static constexpr float CAPE_LENGTH = 0.8f;      // Length in meters
    static constexpr int DEFAULT_WIDTH = 8;         // Cloth grid width
    static constexpr int DEFAULT_HEIGHT = 12;       // Cloth grid height
};
