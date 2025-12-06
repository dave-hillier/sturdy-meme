#include "PlayerCape.h"
#include "WindSystem.h"
#include <SDL3/SDL_log.h>

void PlayerCape::create(int width, int height, float spacing) {
    clothWidth = width;
    clothHeight = height;
    particleSpacing = spacing;

    // Create cloth with initial position at origin
    // Will be repositioned when attachments are updated
    glm::vec3 topLeft(0.0f, 0.0f, 0.0f);
    clothSim.create(width, height, spacing, topLeft);

    initialized = true;
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

    // Spine/Torso capsule (from hips to spine2)
    bodyColliders.push_back({
        makeBone("Hips"), makeBone("Spine2"),
        0.15f,                           // radius
        glm::vec3(0.0f, 0.0f, 0.0f),    // offset1
        glm::vec3(0.0f, 0.0f, 0.0f),    // offset2
        true                             // isCapsule
    });

    // Upper back sphere (to prevent cape going through chest)
    bodyColliders.push_back({
        makeBone("Spine1"), "",
        0.18f,                           // radius
        glm::vec3(0.0f, 0.0f, -0.05f),  // offset (slightly back)
        glm::vec3(0.0f),
        false                            // isSphere
    });

    // Left upper arm
    bodyColliders.push_back({
        makeBone("LeftArm"), makeBone("LeftForeArm"),
        0.06f,
        glm::vec3(0.0f), glm::vec3(0.0f),
        true
    });

    // Right upper arm
    bodyColliders.push_back({
        makeBone("RightArm"), makeBone("RightForeArm"),
        0.06f,
        glm::vec3(0.0f), glm::vec3(0.0f),
        true
    });

    // Left upper leg
    bodyColliders.push_back({
        makeBone("LeftUpLeg"), makeBone("LeftLeg"),
        0.08f,
        glm::vec3(0.0f), glm::vec3(0.0f),
        true
    });

    // Right upper leg
    bodyColliders.push_back({
        makeBone("RightUpLeg"), makeBone("RightLeg"),
        0.08f,
        glm::vec3(0.0f), glm::vec3(0.0f),
        true
    });

    // Head sphere
    bodyColliders.push_back({
        makeBone("Head"), "",
        0.12f,
        glm::vec3(0.0f, 0.05f, 0.0f),   // offset up
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

    // Attach top-left corner to left shoulder
    attachments.push_back({
        makeBone("LeftShoulder"),
        glm::vec3(-0.05f, 0.0f, -0.1f),  // Offset: slightly left and back
        0, 0                              // Top-left corner
    });

    // Attach top-right corner to right shoulder
    attachments.push_back({
        makeBone("RightShoulder"),
        glm::vec3(0.05f, 0.0f, -0.1f),   // Offset: slightly right and back
        clothWidth - 1, 0                 // Top-right corner
    });

    // Attach top-center to spine2 (upper back)
    int centerX = clothWidth / 2;
    attachments.push_back({
        makeBone("Spine2"),
        glm::vec3(0.0f, 0.0f, -0.12f),   // Offset: back of spine
        centerX, 0                        // Top-center
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

    // Get bone world position
    glm::mat4 boneWorld = worldTransform * cachedGlobalTransforms[boneIndex];
    glm::vec4 worldPos = boneWorld * glm::vec4(offset, 1.0f);
    return glm::vec3(worldPos);
}

void PlayerCape::updateAttachments(const Skeleton& skeleton, const glm::mat4& worldTransform) {
    // Access cloth particles directly to move pinned particles
    // This is a bit of a hack - we need to expose particle access in ClothSimulation
    // For now, we'll recreate the cloth at the correct position

    // Get positions for each attachment point
    for (const auto& att : attachments) {
        glm::vec3 worldPos = getBoneWorldPosition(skeleton, worldTransform, att.boneName, att.localOffset);

        // We need to update the pinned particle position
        // ClothSimulation doesn't expose this directly, so we work around it
        // by setting up the cloth simulation's internal particle positions
    }
}

void PlayerCape::applyBodyColliders(const Skeleton& skeleton, const glm::mat4& worldTransform) {
    clothSim.clearCollisions();

    for (const auto& collider : bodyColliders) {
        glm::vec3 pos1 = getBoneWorldPosition(skeleton, worldTransform, collider.boneName1, collider.offset1);

        if (collider.isCapsule && !collider.boneName2.empty()) {
            glm::vec3 pos2 = getBoneWorldPosition(skeleton, worldTransform, collider.boneName2, collider.offset2);
            clothSim.addCapsuleCollision(pos1, pos2, collider.radius);
        } else {
            clothSim.addSphereCollision(pos1, collider.radius);
        }
    }
}

void PlayerCape::update(const Skeleton& skeleton, const glm::mat4& worldTransform,
                         float deltaTime, const WindSystem* windSystem) {
    if (!initialized) return;

    // Cache global transforms
    skeleton.computeGlobalTransforms(cachedGlobalTransforms);

    // Update pinned particle positions based on bone positions
    // We need direct access to particles, so let's extend ClothSimulation
    // For now, work with the existing API

    // Update attachment positions by moving pinned particles
    for (const auto& att : attachments) {
        glm::vec3 worldPos = getBoneWorldPosition(skeleton, worldTransform, att.boneName, att.localOffset);
        // Note: This requires extending ClothSimulation to allow setting particle positions
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
