#include "AnimatedCharacter.h"
#include "GLTFLoader.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>

bool AnimatedCharacter::load(const std::string& path, VmaAllocator allocator, VkDevice device,
                              VkCommandPool commandPool, VkQueue queue) {
    auto result = GLTFLoader::loadSkinned(path);
    if (!result) {
        SDL_Log("AnimatedCharacter: Failed to load %s", path.c_str());
        return false;
    }

    // Store bind pose data
    bindPoseVertices = std::move(result->vertices);
    indices = std::move(result->indices);
    skeleton = std::move(result->skeleton);
    animations = std::move(result->animations);

    // Store bind pose local transforms so we can reset before each animation sample
    bindPoseLocalTransforms.resize(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        bindPoseLocalTransforms[i] = skeleton.joints[i].localTransform;
    }

    // Initialize skinned vertices (same count as bind pose)
    skinnedVertices.resize(bindPoseVertices.size());
    for (size_t i = 0; i < bindPoseVertices.size(); ++i) {
        skinnedVertices[i].position = bindPoseVertices[i].position;
        skinnedVertices[i].normal = bindPoseVertices[i].normal;
        skinnedVertices[i].texCoord = bindPoseVertices[i].texCoord;
        skinnedVertices[i].tangent = bindPoseVertices[i].tangent;
        skinnedVertices[i].color = bindPoseVertices[i].color;
    }

    // Create render mesh
    renderMesh.setCustomGeometry(skinnedVertices, indices);
    renderMesh.upload(allocator, device, commandPool, queue);

    // Set up default animation (play first one if available)
    if (!animations.empty()) {
        animationPlayer.setAnimation(&animations[0]);
        SDL_Log("AnimatedCharacter: Loaded with %zu animations, playing '%s'",
                animations.size(), animations[0].name.c_str());

        // Set up animation state machine with locomotion animations
        // Look for idle, walk, run, jump animations by name
        const AnimationClip* idleClip = nullptr;
        const AnimationClip* walkClip = nullptr;
        const AnimationClip* runClip = nullptr;
        const AnimationClip* jumpClip = nullptr;

        for (const auto& clip : animations) {
            std::string lowerName = clip.name;
            for (char& c : lowerName) c = std::tolower(c);

            if (lowerName.find("idle") != std::string::npos) {
                idleClip = &clip;
            } else if (lowerName.find("walk") != std::string::npos) {
                walkClip = &clip;
            } else if (lowerName.find("run") != std::string::npos) {
                runClip = &clip;
            } else if (lowerName.find("jump") != std::string::npos) {
                jumpClip = &clip;
            }
        }

        // Add states to state machine
        if (idleClip) {
            stateMachine.addState("idle", idleClip, true);
            SDL_Log("AnimatedCharacter: Added 'idle' state");
        }
        if (walkClip) {
            stateMachine.addState("walk", walkClip, true);
            SDL_Log("AnimatedCharacter: Added 'walk' state");
        }
        if (runClip) {
            stateMachine.addState("run", runClip, true);
            SDL_Log("AnimatedCharacter: Added 'run' state");
        }
        if (jumpClip) {
            stateMachine.addState("jump", jumpClip, false);
            SDL_Log("AnimatedCharacter: Added 'jump' state");
        }

        // Enable state machine if we have at least idle
        if (idleClip) {
            stateMachine.setState("idle");
            useStateMachine = true;
            SDL_Log("AnimatedCharacter: State machine enabled with %s locomotion animations",
                    (walkClip && runClip) ? "full" : "partial");
        }
    } else {
        SDL_Log("AnimatedCharacter: Loaded but no animations found");
    }

    loaded = true;
    return true;
}

void AnimatedCharacter::destroy(VmaAllocator allocator) {
    renderMesh.destroy(allocator);
    bindPoseVertices.clear();
    indices.clear();
    skeleton.joints.clear();
    bindPoseLocalTransforms.clear();
    animations.clear();
    skinnedVertices.clear();
    loaded = false;
}

void AnimatedCharacter::playAnimation(const std::string& name) {
    for (size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == name || animations[i].name.find(name) != std::string::npos) {
            playAnimation(i);
            return;
        }
    }
    SDL_Log("AnimatedCharacter: Animation '%s' not found", name.c_str());
}

void AnimatedCharacter::playAnimation(size_t index) {
    if (index < animations.size()) {
        animationPlayer.setAnimation(&animations[index]);
        SDL_Log("AnimatedCharacter: Now playing '%s'", animations[index].name.c_str());
    }
}

void AnimatedCharacter::setPlaybackSpeed(float speed) {
    animationPlayer.setPlaybackSpeed(speed);
}

void AnimatedCharacter::setLooping(bool loop) {
    animationPlayer.setLooping(loop);
}

const AnimationClip* AnimatedCharacter::getCurrentAnimation() const {
    for (const auto& clip : animations) {
        if (animationPlayer.getDuration() == clip.duration) {
            return &clip;
        }
    }
    return nullptr;
}

void AnimatedCharacter::update(float deltaTime, VmaAllocator allocator, VkDevice device,
                                VkCommandPool commandPool, VkQueue queue,
                                float movementSpeed, bool isGrounded, bool isJumping) {
    if (!loaded) return;

    // Reset skeleton to bind pose before applying animation
    // This ensures joints not affected by the current animation keep their bind pose
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        skeleton.joints[i].localTransform = bindPoseLocalTransforms[i];
    }

    if (useStateMachine) {
        // Use state machine for animation selection and blending
        stateMachine.update(deltaTime, movementSpeed, isGrounded, isJumping);
        stateMachine.applyToSkeleton(skeleton);
    } else {
        // Fallback to simple animation player
        animationPlayer.update(deltaTime);
        animationPlayer.applyToSkeleton(skeleton);
    }

    // Apply CPU skinning
    applySkinning();

    // Re-upload mesh
    uploadMesh(allocator, device, commandPool, queue);
}

void AnimatedCharacter::computeBoneMatrices(std::vector<glm::mat4>& outBoneMatrices) const {
    // First compute global transforms
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    // Then multiply by inverse bind matrices to get final bone matrices
    outBoneMatrices.resize(skeleton.joints.size());
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        outBoneMatrices[i] = globalTransforms[i] * skeleton.joints[i].inverseBindMatrix;
    }
}

void AnimatedCharacter::applySkinning() {
    // Compute bone matrices
    std::vector<glm::mat4> boneMatrices;
    computeBoneMatrices(boneMatrices);

    // Apply skinning to each vertex
    for (size_t i = 0; i < bindPoseVertices.size(); ++i) {
        const SkinnedVertex& bindVert = bindPoseVertices[i];
        Vertex& skinnedVert = skinnedVertices[i];

        // Build skin matrix from weighted bone matrices
        glm::mat4 skinMatrix(0.0f);

        if (bindVert.boneIndices.x < boneMatrices.size()) {
            skinMatrix += boneMatrices[bindVert.boneIndices.x] * bindVert.boneWeights.x;
        }
        if (bindVert.boneIndices.y < boneMatrices.size()) {
            skinMatrix += boneMatrices[bindVert.boneIndices.y] * bindVert.boneWeights.y;
        }
        if (bindVert.boneIndices.z < boneMatrices.size()) {
            skinMatrix += boneMatrices[bindVert.boneIndices.z] * bindVert.boneWeights.z;
        }
        if (bindVert.boneIndices.w < boneMatrices.size()) {
            skinMatrix += boneMatrices[bindVert.boneIndices.w] * bindVert.boneWeights.w;
        }

        // Transform position
        glm::vec4 skinnedPos = skinMatrix * glm::vec4(bindVert.position, 1.0f);
        skinnedVert.position = glm::vec3(skinnedPos);

        // Transform normal
        glm::mat3 normalMatrix = glm::mat3(skinMatrix);
        skinnedVert.normal = glm::normalize(normalMatrix * bindVert.normal);

        // Transform tangent (keep w component for handedness)
        skinnedVert.tangent = glm::vec4(glm::normalize(normalMatrix * glm::vec3(bindVert.tangent)), bindVert.tangent.w);

        // Copy UV (unchanged)
        skinnedVert.texCoord = bindVert.texCoord;
    }
}

void AnimatedCharacter::uploadMesh(VmaAllocator allocator, VkDevice device,
                                    VkCommandPool commandPool, VkQueue queue) {
    // Destroy old mesh and re-upload with new data
    renderMesh.destroy(allocator);
    renderMesh.setCustomGeometry(skinnedVertices, indices);
    renderMesh.upload(allocator, device, commandPool, queue);
}
