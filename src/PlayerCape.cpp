#include "PlayerCape.h"
#include "WindSystem.h"
#include <SDL3/SDL_log.h>

void PlayerCape::create(int width, int height, float spacing) {
    clothWidth = width;
    clothHeight = height;
    particleSpacing = spacing;

    // Create cloth with initial position at origin
    // Will be repositioned when initializeFromSkeleton is called
    glm::vec3 topLeft(0.0f, 0.0f, 0.0f);
    clothSim.create(width, height, spacing, topLeft);

    initialized = true;
    positionsInitialized = false;
    SDL_Log("PlayerCape: Created %dx%d cloth simulation", width, height);
}

void PlayerCape::addBodyCollider(const BodyCollider& collider) {
    bodyColliders.push_back(collider);
}

void PlayerCape::addAttachment(const CapeAttachment& attachment) {
    attachments.push_back(attachment);
    // Pin the cloth particle at this position
    clothSim.pinParticle(attachment.clothX, attachment.clothY);
}

void PlayerCape::setupDefaultColliders() {
    bodyColliders.clear();

    // Helper to find bone name with or without mixamorig prefix
    auto makeBone = [](const std::string& name) -> std::string {
        return "mixamorig:" + name;
    };

    // Note: In Mixamo character local space:
    // +Y is up, +Z is FORWARD (where character faces), -Z is BACK

    // Spine/Torso capsule (from hips to spine2)
    // Offset slightly back (-Z in character local space)
    bodyColliders.push_back({
        makeBone("Hips"), makeBone("Spine2"),
        0.12f,                            // radius
        glm::vec3(0.0f, 0.0f, -0.02f),   // offset1 - slightly back (-Z)
        glm::vec3(0.0f, 0.0f, -0.02f),   // offset2 - slightly back (-Z)
        true                              // isCapsule
    });

    // Upper back sphere (to prevent cape going through back)
    bodyColliders.push_back({
        makeBone("Spine2"), "",
        0.14f,                            // radius
        glm::vec3(0.0f, 0.0f, -0.08f),   // offset: behind spine (-Z is back)
        glm::vec3(0.0f),
        false                             // isSphere
    });

    // Left upper arm
    bodyColliders.push_back({
        makeBone("LeftArm"), makeBone("LeftForeArm"),
        0.05f,
        glm::vec3(0.0f), glm::vec3(0.0f),
        true
    });

    // Right upper arm
    bodyColliders.push_back({
        makeBone("RightArm"), makeBone("RightForeArm"),
        0.05f,
        glm::vec3(0.0f), glm::vec3(0.0f),
        true
    });

    // Left upper leg
    bodyColliders.push_back({
        makeBone("LeftUpLeg"), makeBone("LeftLeg"),
        0.07f,
        glm::vec3(0.0f), glm::vec3(0.0f),
        true
    });

    // Right upper leg
    bodyColliders.push_back({
        makeBone("RightUpLeg"), makeBone("RightLeg"),
        0.07f,
        glm::vec3(0.0f), glm::vec3(0.0f),
        true
    });

    // Head sphere
    bodyColliders.push_back({
        makeBone("Head"), "",
        0.10f,
        glm::vec3(0.0f, 0.05f, 0.0f),    // offset up
        glm::vec3(0.0f),
        false
    });

    SDL_Log("PlayerCape: Setup %zu default body colliders", bodyColliders.size());
}

void PlayerCape::setupDefaultAttachments() {
    attachments.clear();

    auto makeBone = [](const std::string& name) -> std::string {
        return "mixamorig:" + name;
    };

    // Note: In Mixamo character local space:
    // +Y is up, +Z is FORWARD (where character faces), -Z is BACK

    // Attach top-left corner to left shoulder
    attachments.push_back({
        makeBone("LeftShoulder"),
        glm::vec3(0.0f, -0.02f, -0.08f),  // Offset: back of shoulder (-Z)
        0, 0                               // Top-left corner
    });

    // Attach top-right corner to right shoulder
    attachments.push_back({
        makeBone("RightShoulder"),
        glm::vec3(0.0f, -0.02f, -0.08f),  // Offset: back of shoulder (-Z)
        clothWidth - 1, 0                  // Top-right corner
    });

    // Attach top-center to spine2 (upper back)
    int centerX = clothWidth / 2;
    attachments.push_back({
        makeBone("Spine2"),
        glm::vec3(0.0f, 0.0f, -0.10f),    // Offset: back of spine (-Z)
        centerX, 0                         // Top-center
    });

    // Pin the particles
    for (const auto& att : attachments) {
        clothSim.pinParticle(att.clothX, att.clothY);
    }

    SDL_Log("PlayerCape: Setup %zu default attachments", attachments.size());
}

glm::vec3 PlayerCape::getBoneWorldPosition(const Skeleton& skeleton, const glm::mat4& worldTransform,
                                            const std::string& boneName, const glm::vec3& offset) const {
    int32_t boneIndex = skeleton.findJointIndex(boneName);
    if (boneIndex < 0) {
        // Try without mixamorig prefix
        std::string altName = boneName;
        if (boneName.find("mixamorig:") == 0) {
            altName = boneName.substr(10);  // Remove "mixamorig:" prefix
        }
        boneIndex = skeleton.findJointIndex(altName);
    }

    if (boneIndex < 0 || boneIndex >= static_cast<int32_t>(cachedGlobalTransforms.size())) {
        return glm::vec3(0.0f);
    }

    // Get bone world position with local offset transformed by bone orientation
    glm::mat4 boneWorld = worldTransform * cachedGlobalTransforms[boneIndex];
    glm::vec4 worldPos = boneWorld * glm::vec4(offset, 1.0f);
    return glm::vec3(worldPos);
}

void PlayerCape::updateAttachments(const Skeleton& skeleton, const glm::mat4& worldTransform) {
    // Update attachment positions by moving pinned particles
    for (const auto& att : attachments) {
        glm::vec3 worldPos = getBoneWorldPosition(skeleton, worldTransform, att.boneName, att.localOffset);
        clothSim.setParticlePosition(att.clothX, att.clothY, worldPos);
    }
}

void PlayerCape::applyBodyColliders(const Skeleton& skeleton, const glm::mat4& worldTransform) {
    clothSim.clearCollisions();

    // Clear debug data
    lastDebugData.spheres.clear();
    lastDebugData.capsules.clear();
    lastDebugData.attachmentPoints.clear();

    for (const auto& collider : bodyColliders) {
        glm::vec3 pos1 = getBoneWorldPosition(skeleton, worldTransform, collider.boneName1, collider.offset1);

        if (collider.isCapsule && !collider.boneName2.empty()) {
            glm::vec3 pos2 = getBoneWorldPosition(skeleton, worldTransform, collider.boneName2, collider.offset2);
            clothSim.addCapsuleCollision(pos1, pos2, collider.radius);

            // Store for debug visualization
            lastDebugData.capsules.push_back({pos1, pos2, collider.radius});
        } else {
            clothSim.addSphereCollision(pos1, collider.radius);

            // Store for debug visualization
            lastDebugData.spheres.push_back({pos1, collider.radius});
        }
    }

    // Store attachment points for debug
    for (const auto& att : attachments) {
        glm::vec3 worldPos = getBoneWorldPosition(skeleton, worldTransform, att.boneName, att.localOffset);
        lastDebugData.attachmentPoints.push_back(worldPos);
    }
}

void PlayerCape::initializeFromSkeleton(const Skeleton& skeleton, const glm::mat4& worldTransform) {
    if (!initialized || positionsInitialized) return;

    // Cache global transforms
    skeleton.computeGlobalTransforms(cachedGlobalTransforms);

    // Get attachment world positions
    std::vector<glm::vec3> attachWorldPos;
    for (const auto& att : attachments) {
        glm::vec3 worldPos = getBoneWorldPosition(skeleton, worldTransform, att.boneName, att.localOffset);
        attachWorldPos.push_back(worldPos);
    }

    if (attachWorldPos.empty()) {
        SDL_Log("PlayerCape: No attachments, cannot initialize positions");
        return;
    }

    // Calculate cape center and down direction from attachments
    glm::vec3 topCenter(0.0f);
    for (const auto& pos : attachWorldPos) {
        topCenter += pos;
    }
    topCenter /= static_cast<float>(attachWorldPos.size());

    // Cape hangs down from attachments
    glm::vec3 down(0.0f, -1.0f, 0.0f);

    // Calculate cape width from left-right attachment spread
    float capeWidth = clothWidth * particleSpacing;
    if (attachWorldPos.size() >= 2) {
        capeWidth = glm::length(attachWorldPos[1] - attachWorldPos[0]);
    }

    // Initialize all cloth particles to form a flat cape behind the character
    // Get the character's back direction from world transform
    // In Mixamo, +Z is forward, so -Z is back
    glm::vec3 back = -glm::normalize(glm::vec3(worldTransform[2]));  // -Z axis = back

    for (int y = 0; y < clothHeight; ++y) {
        for (int x = 0; x < clothWidth; ++x) {
            // Interpolate position based on grid coordinates
            float u = static_cast<float>(x) / static_cast<float>(clothWidth - 1);
            float v = static_cast<float>(y) / static_cast<float>(clothHeight - 1);

            // Horizontal position: interpolate across width
            glm::vec3 left = attachWorldPos.size() > 0 ? attachWorldPos[0] : topCenter;
            glm::vec3 right = attachWorldPos.size() > 1 ? attachWorldPos[1] : topCenter;
            glm::vec3 horizontal = glm::mix(left, right, u);

            // Vertical position: go down from top
            float capeLength = clothHeight * particleSpacing;
            glm::vec3 pos = horizontal + down * (v * capeLength);

            // Slight backward offset to keep cape behind character
            pos += back * (v * 0.1f);

            clothSim.setParticlePosition(x, y, pos);
        }
    }

    positionsInitialized = true;
    SDL_Log("PlayerCape: Initialized cloth positions from skeleton");
}

void PlayerCape::update(const Skeleton& skeleton, const glm::mat4& worldTransform,
                         float deltaTime, const WindSystem* windSystem) {
    if (!initialized) return;

    // Cache global transforms
    skeleton.computeGlobalTransforms(cachedGlobalTransforms);

    // Initialize positions on first update
    if (!positionsInitialized) {
        initializeFromSkeleton(skeleton, worldTransform);
    }

    // Update attachment positions by moving pinned particles
    for (const auto& att : attachments) {
        glm::vec3 worldPos = getBoneWorldPosition(skeleton, worldTransform, att.boneName, att.localOffset);
        clothSim.setParticlePosition(att.clothX, att.clothY, worldPos);
    }

    // Apply body colliders
    applyBodyColliders(skeleton, worldTransform);

    // Update cloth simulation
    clothSim.update(deltaTime, windSystem);
}

void PlayerCape::createMesh(Mesh& mesh) const {
    if (!initialized) return;
    clothSim.createMesh(mesh);
}

void PlayerCape::updateMesh(Mesh& mesh) const {
    if (!initialized) return;
    clothSim.updateMesh(mesh);
}

CapeDebugData PlayerCape::getDebugData() const {
    return lastDebugData;
}
