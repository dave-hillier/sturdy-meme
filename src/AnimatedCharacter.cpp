#include "AnimatedCharacter.h"
#include "GLTFLoader.h"
#include "FBXLoader.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>

namespace {
bool endsWith(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}

bool AnimatedCharacter::load(const std::string& path, VmaAllocator allocator, VkDevice device,
                              VkCommandPool commandPool, VkQueue queue) {
    std::optional<GLTFSkinnedLoadResult> result;

    // Detect file format and use appropriate loader
    if (endsWith(path, ".fbx") || endsWith(path, ".FBX")) {
        result = FBXLoader::loadSkinned(path);
    } else {
        result = GLTFLoader::loadSkinned(path);
    }

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

    // GPU skinning: Upload skinned mesh with original bind pose vertices
    // The GPU will apply bone matrices in the vertex shader
    SkinnedMeshData meshData;
    meshData.vertices = bindPoseVertices;
    meshData.indices = indices;
    meshData.skeleton = skeleton;
    skinnedMesh.setData(meshData);
    skinnedMesh.upload(allocator, device, commandPool, queue);

    // CPU skinning fallback (kept for compatibility, but not used with GPU skinning)
    if (!useGPUSkinning) {
        skinnedVertices.resize(bindPoseVertices.size());
        for (size_t i = 0; i < bindPoseVertices.size(); ++i) {
            skinnedVertices[i].position = bindPoseVertices[i].position;
            skinnedVertices[i].normal = bindPoseVertices[i].normal;
            skinnedVertices[i].texCoord = bindPoseVertices[i].texCoord;
            skinnedVertices[i].tangent = bindPoseVertices[i].tangent;
            skinnedVertices[i].color = bindPoseVertices[i].color;
        }
        renderMesh.setCustomGeometry(skinnedVertices, indices);
        renderMesh.upload(allocator, device, commandPool, queue);
    }

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
    skinnedMesh.destroy(allocator);
    renderMesh.destroy(allocator);
    bindPoseVertices.clear();
    indices.clear();
    skeleton.joints.clear();
    bindPoseLocalTransforms.clear();
    animations.clear();
    skinnedVertices.clear();
    loaded = false;
}

void AnimatedCharacter::loadAdditionalAnimations(const std::vector<std::string>& paths) {
    if (!loaded) {
        SDL_Log("AnimatedCharacter: Cannot load animations before loading character");
        return;
    }

    for (const auto& path : paths) {
        auto newAnims = FBXLoader::loadAnimations(path, skeleton);
        for (auto& anim : newAnims) {
            animations.push_back(std::move(anim));
        }
    }

    // Re-setup state machine with all animations
    stateMachine = AnimationStateMachine();  // Reset

    const AnimationClip* idleClip = nullptr;
    const AnimationClip* walkClip = nullptr;
    const AnimationClip* runClip = nullptr;
    const AnimationClip* jumpClip = nullptr;
    const AnimationClip* turnLeftClip = nullptr;
    const AnimationClip* turnRightClip = nullptr;
    const AnimationClip* turn180Clip = nullptr;

    for (const auto& clip : animations) {
        std::string lowerName = clip.name;
        for (char& c : lowerName) c = std::tolower(c);

        if (lowerName.find("idle") != std::string::npos) {
            idleClip = &clip;
        } else if (lowerName.find("turn_180") != std::string::npos || lowerName.find("turn180") != std::string::npos) {
            turn180Clip = &clip;
        } else if (lowerName.find("turn_left") != std::string::npos || lowerName.find("turnleft") != std::string::npos) {
            turnLeftClip = &clip;
        } else if (lowerName.find("turn_right") != std::string::npos || lowerName.find("turnright") != std::string::npos) {
            turnRightClip = &clip;
        } else if (lowerName.find("walk") != std::string::npos) {
            walkClip = &clip;
        } else if (lowerName.find("run") != std::string::npos) {
            runClip = &clip;
        } else if (lowerName.find("jump") != std::string::npos) {
            jumpClip = &clip;
        }
    }

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
    if (turnLeftClip) {
        stateMachine.addState("turn_left", turnLeftClip, false);
        SDL_Log("AnimatedCharacter: Added 'turn_left' state");
    }
    if (turnRightClip) {
        stateMachine.addState("turn_right", turnRightClip, false);
        SDL_Log("AnimatedCharacter: Added 'turn_right' state");
    }
    if (turn180Clip) {
        stateMachine.addState("turn_180", turn180Clip, false);
        SDL_Log("AnimatedCharacter: Added 'turn_180' state");
    }

    if (idleClip) {
        stateMachine.setState("idle");
        useStateMachine = true;
        SDL_Log("AnimatedCharacter: State machine refreshed with %zu total animations",
                animations.size());
    }
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
                                float movementSpeed, bool isGrounded, bool isJumping,
                                float turnAngle) {
    if (!loaded) return;

    // Reset skeleton to bind pose before applying animation
    // This ensures joints not affected by the current animation keep their bind pose
    for (size_t i = 0; i < skeleton.joints.size(); ++i) {
        skeleton.joints[i].localTransform = bindPoseLocalTransforms[i];
    }

    if (useStateMachine) {
        // Use state machine for animation selection and blending
        stateMachine.update(deltaTime, movementSpeed, isGrounded, isJumping, turnAngle);
        stateMachine.applyToSkeleton(skeleton);
    } else {
        // Fallback to simple animation player
        animationPlayer.update(deltaTime);
        animationPlayer.applyToSkeleton(skeleton);
    }

    // GPU skinning: Bone matrices are computed and uploaded by Renderer each frame
    // No mesh re-upload needed - the vertex shader applies skinning
    if (useGPUSkinning) {
        return;  // Bone matrices updated externally via computeBoneMatrices()
    }

    // CPU skinning fallback (deprecated path)
    applySkinning();
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
    // Compute bone matrices (globalTransform * inverseBindMatrix)
    std::vector<glm::mat4> boneMatrices;
    computeBoneMatrices(boneMatrices);

    // Also compute global transforms alone (for non-skinned primitives)
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    // Apply skinning to each vertex
    for (size_t i = 0; i < bindPoseVertices.size(); ++i) {
        const SkinnedVertex& bindVert = bindPoseVertices[i];
        Vertex& skinnedVert = skinnedVertices[i];

        glm::mat4 skinMatrix;

        // Negative weight on x indicates non-skinned primitive - no transformation needed
        // These vertices are already in model space and should stay there
        if (bindVert.boneWeights.x < 0.0f) {
            // Identity matrix - keep vertices in their original position
            skinMatrix = glm::mat4(1.0f);
        } else {
            // Build skin matrix from weighted bone matrices (normal skinning path)
            skinMatrix = glm::mat4(0.0f);

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
