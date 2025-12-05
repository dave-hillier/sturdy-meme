#include "FBXPostProcess.h"
#include "GLTFLoader.h"
#include "SkinnedMesh.h"
#include "Animation.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>

namespace FBXPostProcess {

namespace {

// Convert Euler angles (degrees) to quaternion using XYZ intrinsic order
glm::quat eulerToQuat(const glm::vec3& eulerDeg) {
    glm::vec3 eulerRad = glm::radians(eulerDeg);
    glm::quat qX = glm::angleAxis(eulerRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::quat qY = glm::angleAxis(eulerRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat qZ = glm::angleAxis(eulerRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return qZ * qY * qX;
}

// Get the rotation matrix for coordinate system conversion
glm::mat3 getCoordinateSystemRotation(UpAxis sourceUp, ForwardAxis sourceFwd) {
    // Target coordinate system: Y-up, -Z forward (OpenGL/Vulkan convention)
    //
    // We need to build a matrix that transforms from source to target.
    // This is done by determining which source axis maps to which target axis.

    glm::vec3 targetUp(0.0f, 1.0f, 0.0f);      // +Y
    glm::vec3 targetFwd(0.0f, 0.0f, -1.0f);    // -Z
    glm::vec3 targetRight(1.0f, 0.0f, 0.0f);   // +X

    // Determine source up vector in source coordinates
    glm::vec3 srcUpVec;
    switch (sourceUp) {
        case UpAxis::Y_UP:     srcUpVec = glm::vec3(0, 1, 0); break;
        case UpAxis::Z_UP:     srcUpVec = glm::vec3(0, 0, 1); break;
        case UpAxis::NEG_Y_UP: srcUpVec = glm::vec3(0, -1, 0); break;
        case UpAxis::NEG_Z_UP: srcUpVec = glm::vec3(0, 0, -1); break;
    }

    // Determine source forward vector in source coordinates
    glm::vec3 srcFwdVec;
    switch (sourceFwd) {
        case ForwardAxis::NEG_Z: srcFwdVec = glm::vec3(0, 0, -1); break;
        case ForwardAxis::Z:     srcFwdVec = glm::vec3(0, 0, 1); break;
        case ForwardAxis::NEG_Y: srcFwdVec = glm::vec3(0, -1, 0); break;
        case ForwardAxis::Y:     srcFwdVec = glm::vec3(0, 1, 0); break;
        case ForwardAxis::X:     srcFwdVec = glm::vec3(1, 0, 0); break;
        case ForwardAxis::NEG_X: srcFwdVec = glm::vec3(-1, 0, 0); break;
    }

    // Derive right vector from forward and up (right-handed: right = forward × up)
    glm::vec3 srcRightVec = glm::cross(srcFwdVec, srcUpVec);

    // Handle degenerate case (up and forward parallel)
    if (glm::length(srcRightVec) < 0.001f) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "FBXPostProcess: Source up and forward axes are parallel, using fallback");
        // Use a fallback right vector
        srcRightVec = glm::vec3(1, 0, 0);
        if (std::abs(glm::dot(srcRightVec, srcUpVec)) > 0.9f) {
            srcRightVec = glm::vec3(0, 1, 0);
        }
    }
    srcRightVec = glm::normalize(srcRightVec);

    // Re-derive forward to ensure orthogonality (forward = up × right for right-handed)
    srcFwdVec = glm::cross(srcUpVec, srcRightVec);
    srcFwdVec = glm::normalize(srcFwdVec);

    // Build transformation matrix
    // Each column tells where each source axis ends up in target space
    //
    // If source has Y-up, -Z forward (same as target), result is identity
    // If source has Z-up, -Y forward, we need:
    //   Source X -> Target X
    //   Source Y -> Target -Z
    //   Source Z -> Target Y
    //
    // The transformation matrix M satisfies: target = M * source
    // Build it by mapping source basis vectors to target positions

    // The matrix maps: [srcRight, srcUp, srcFwd] -> [targetRight, targetUp, targetFwd]
    // This is achieved by: M = [targetRight | targetUp | targetFwd] * inverse([srcRight | srcUp | srcFwd])

    // Since both are orthonormal bases, inverse = transpose
    glm::mat3 srcBasis(srcRightVec, srcUpVec, -srcFwdVec);  // Columns: right, up, -forward (standard basis)
    glm::mat3 targetBasis(targetRight, targetUp, -targetFwd);  // Same convention

    // For a simple axis remapping approach:
    // Build a matrix where row i tells how source axis i contributes to result
    // Actually, let's use a more direct approach:

    // If source is Z-up, -Y forward:
    //   srcUp = (0,0,1) should become targetUp = (0,1,0)
    //   srcFwd = (0,-1,0) should become targetFwd = (0,0,-1)
    //   srcRight = (1,0,0) should become targetRight = (1,0,0)

    // Determine the mapping: given a source coord (x,y,z), what is target (x',y',z')?
    // We want: target_up_component = source dot srcUpVec (how much is in source up direction)
    // And that should become the target Y component

    // Build transformation by determining where each source axis goes:
    // Column 0 (source X): where does (1,0,0) go?
    // Column 1 (source Y): where does (0,1,0) go?
    // Column 2 (source Z): where does (0,0,1) go?

    // For Y-up, -Z forward (identity case):
    //   (1,0,0) -> (1,0,0), (0,1,0) -> (0,1,0), (0,0,1) -> (0,0,1)

    // For Z-up, -Y forward (Blender):
    //   Source X (1,0,0) is right -> stays right (1,0,0) in target
    //   Source Y (0,1,0) is -forward in source, so it's forward in target space -> (0,0,-1)
    //   Source Z (0,0,1) is up in source -> becomes Y in target (0,1,0)

    // So the matrix columns are the destination of each source axis
    glm::vec3 col0, col1, col2;  // Where source X, Y, Z go respectively

    // Source X is always right (assuming right-handed), check the derived srcRightVec
    // Actually let's compute this more directly:

    // The source coordinate (sx, sy, sz) has:
    // - Component along source right = sx (if srcRight = +X)
    // - Component along source up = depends on srcUpVec
    // - Component along source forward = depends on srcFwdVec

    // We want the component along srcUpVec to become the Y component (target up)
    // We want the component along srcFwdVec to become the -Z component (target forward = -Z)
    // We want the component along srcRightVec to become the X component (target right)

    // The transformation is:
    // target = [dot(srcRight, source), dot(srcUp, source), -dot(srcFwd, source)]
    // Which is equivalent to the matrix with rows: srcRight, srcUp, -srcFwd

    // But we want the matrix in column form for GLM (column-major)
    glm::mat3 result;
    result[0] = srcRightVec;   // First column: where does source contribute to target X
    result[1] = srcUpVec;      // Second column: where does source contribute to target Y
    result[2] = -srcFwdVec;    // Third column: where does source contribute to target Z

    // Wait, that's transposed. Let me think again...
    // If result * srcVec = targetVec, and we want srcUp to become targetUp:
    // result * srcUpVec = targetUp = (0,1,0)

    // The rows of the result matrix should be:
    // Row 0: transform that extracts right component -> srcRightVec
    // Row 1: transform that extracts up component -> srcUpVec
    // Row 2: transform that extracts forward component (but negated for -Z) -> -srcFwdVec

    // GLM uses column-major, so to set rows we transpose:
    glm::mat3 transform;
    transform[0][0] = srcRightVec.x; transform[1][0] = srcRightVec.y; transform[2][0] = srcRightVec.z;
    transform[0][1] = srcUpVec.x;    transform[1][1] = srcUpVec.y;    transform[2][1] = srcUpVec.z;
    transform[0][2] = -srcFwdVec.x;  transform[1][2] = -srcFwdVec.y;  transform[2][2] = -srcFwdVec.z;

    return transform;
}

} // anonymous namespace

glm::mat4 buildTransformMatrix(const FBXImportSettings& settings) {
    // Start with identity
    glm::mat4 transform(1.0f);

    // 1. Apply scale
    transform = glm::scale(transform, glm::vec3(settings.scaleFactor));

    // 2. Apply coordinate system conversion
    glm::mat3 coordRot = getCoordinateSystemRotation(settings.sourceUpAxis, settings.sourceForwardAxis);
    transform = glm::mat4(coordRot) * transform;

    // 3. Apply rotation correction
    if (glm::length(settings.rotationCorrection) > 0.001f) {
        glm::quat correctionQuat = eulerToQuat(settings.rotationCorrection);
        glm::mat4 correctionMat = glm::mat4_cast(correctionQuat);
        transform = correctionMat * transform;
    }

    return transform;
}

glm::mat3 buildRotationMatrix(const FBXImportSettings& settings) {
    // Build rotation-only matrix (no scale) for normals and directions
    glm::mat3 coordRot = getCoordinateSystemRotation(settings.sourceUpAxis, settings.sourceForwardAxis);

    if (glm::length(settings.rotationCorrection) > 0.001f) {
        glm::quat correctionQuat = eulerToQuat(settings.rotationCorrection);
        glm::mat3 correctionMat = glm::mat3_cast(correctionQuat);
        coordRot = correctionMat * coordRot;
    }

    return coordRot;
}

void process(GLTFSkinnedLoadResult& result, const FBXImportSettings& settings) {
    SDL_Log("FBXPostProcess: Processing skinned mesh with preset '%s' (scale=%.4f)",
            settings.presetName.c_str(), settings.scaleFactor);

    // Build transformation matrices
    glm::mat4 transform = buildTransformMatrix(settings);
    glm::mat3 rotationMat = buildRotationMatrix(settings);
    glm::mat3 normalMat = glm::transpose(glm::inverse(rotationMat));  // For non-uniform scale safety

    // Check if transform flips handedness (negative determinant)
    float det = glm::determinant(glm::mat3(transform));
    bool flipWinding = det < 0.0f;
    SDL_Log("FBXPostProcess: Transform determinant=%.4f, flipWinding=%s", det, flipWinding ? "yes" : "no");

    // Process vertices
    for (auto& vertex : result.vertices) {
        // Transform position (includes scale and coordinate conversion)
        glm::vec4 pos4 = transform * glm::vec4(vertex.position, 1.0f);
        vertex.position = glm::vec3(pos4);

        // Transform normal (rotation only, normalized)
        vertex.normal = glm::normalize(normalMat * vertex.normal);

        // Transform tangent direction (keep w component for handedness)
        float tangentW = vertex.tangent.w;
        glm::vec3 tangentDir = glm::normalize(rotationMat * glm::vec3(vertex.tangent));
        vertex.tangent = glm::vec4(tangentDir, tangentW);
    }

    // Flip triangle winding if transform has negative determinant (reflects handedness)
    if (flipWinding) {
        SDL_Log("FBXPostProcess: Flipping triangle winding due to negative determinant");
        for (size_t i = 0; i + 2 < result.indices.size(); i += 3) {
            std::swap(result.indices[i + 1], result.indices[i + 2]);
        }
    }

    // Process skeleton joints - first pass: transform local transforms
    for (auto& joint : result.skeleton.joints) {
        glm::vec3 localPos = glm::vec3(joint.localTransform[3]);
        glm::mat3 localRot = glm::mat3(joint.localTransform);

        // Apply coordinate system conversion to rotation
        glm::mat3 newLocalRot = rotationMat * localRot * glm::transpose(rotationMat);

        // Apply scale and coordinate conversion to position
        glm::vec3 newLocalPos = rotationMat * (localPos * settings.scaleFactor);

        // Rebuild local transform with unit scale
        // FBX files (especially Mixamo cm exports) often have scale baked into bones
        // (e.g., scale of 100 for cm units). We normalize to 1.0 since our position
        // scaling already handles unit conversion.
        joint.localTransform = glm::mat4(1.0f);
        joint.localTransform[0] = glm::vec4(glm::normalize(newLocalRot[0]), 0.0f);
        joint.localTransform[1] = glm::vec4(glm::normalize(newLocalRot[1]), 0.0f);
        joint.localTransform[2] = glm::vec4(glm::normalize(newLocalRot[2]), 0.0f);
        joint.localTransform[3] = glm::vec4(newLocalPos, 1.0f);

        // Transform pre-rotation
        if (joint.preRotation != glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
            glm::mat3 preRotMat = glm::mat3_cast(joint.preRotation);
            glm::mat3 newPreRotMat = rotationMat * preRotMat * glm::transpose(rotationMat);
            joint.preRotation = glm::quat_cast(newPreRotMat);
        }
    }

    // Second pass: recompute inverseBindMatrix from the transformed skeleton's bind pose
    //
    // The original inverseBindMatrix was: inverse(originalGlobalBindPose)
    // where originalGlobalBindPose was computed from the original local transforms.
    //
    // Since we've modified the local transforms (normalized scales, transformed rotations
    // and positions), we need to recompute what the global bind pose is now and invert it.
    //
    // This ensures that at bind pose: boneMatrix = globalTransform * IBM = identity
    // which is required for correct skinning.
    {
        std::vector<glm::mat4> globalTransforms(result.skeleton.joints.size());
        for (size_t i = 0; i < result.skeleton.joints.size(); ++i) {
            const auto& joint = result.skeleton.joints[i];
            if (joint.parentIndex < 0) {
                globalTransforms[i] = joint.localTransform;
            } else {
                globalTransforms[i] = globalTransforms[joint.parentIndex] * joint.localTransform;
            }
        }

        for (size_t i = 0; i < result.skeleton.joints.size(); ++i) {
            result.skeleton.joints[i].inverseBindMatrix = glm::inverse(globalTransforms[i]);
        }
    }

    // Process animations
    processAnimations(result.animations, result.skeleton, settings);

    SDL_Log("FBXPostProcess: Processed %zu vertices, %zu joints, %zu animations",
            result.vertices.size(), result.skeleton.joints.size(), result.animations.size());
}

void process(GLTFLoadResult& result, const FBXImportSettings& settings) {
    SDL_Log("FBXPostProcess: Processing static mesh with preset '%s' (scale=%.4f)",
            settings.presetName.c_str(), settings.scaleFactor);

    // Build transformation matrices
    glm::mat4 transform = buildTransformMatrix(settings);
    glm::mat3 rotationMat = buildRotationMatrix(settings);
    glm::mat3 normalMat = glm::transpose(glm::inverse(rotationMat));

    // Process vertices
    for (auto& vertex : result.vertices) {
        // Transform position
        glm::vec4 pos4 = transform * glm::vec4(vertex.position, 1.0f);
        vertex.position = glm::vec3(pos4);

        // Transform normal
        vertex.normal = glm::normalize(normalMat * vertex.normal);

        // Transform tangent
        float tangentW = vertex.tangent.w;
        glm::vec3 tangentDir = glm::normalize(rotationMat * glm::vec3(vertex.tangent));
        vertex.tangent = glm::vec4(tangentDir, tangentW);
    }

    SDL_Log("FBXPostProcess: Processed %zu vertices", result.vertices.size());
}

void processAnimations(std::vector<AnimationClip>& animations,
                       const Skeleton& skeleton,
                       const FBXImportSettings& settings) {
    glm::mat3 rotationMat = buildRotationMatrix(settings);

    for (auto& clip : animations) {
        // Transform root motion
        clip.rootMotionPerCycle = rotationMat * (clip.rootMotionPerCycle * settings.scaleFactor);

        // Transform each channel's keyframes
        for (auto& channel : clip.channels) {
            // Transform translation keyframes
            for (auto& value : channel.translation.values) {
                value = rotationMat * (value * settings.scaleFactor);
            }

            // Transform rotation keyframes
            // Rotations need to be transformed to the new coordinate system
            for (auto& value : channel.rotation.values) {
                glm::mat3 rotMat = glm::mat3_cast(value);
                glm::mat3 newRotMat = rotationMat * rotMat * glm::transpose(rotationMat);
                value = glm::quat_cast(newRotMat);
            }

            // Normalize scale keyframes to unit scale
            // Since we normalize skeleton bone scales to 1.0, animation scale keyframes
            // that were set to 100 (for cm units) should also be normalized.
            for (auto& value : channel.scale.values) {
                if (value.x > 10.0f || value.y > 10.0f || value.z > 10.0f) {
                    value = value * settings.scaleFactor;
                }
            }
        }
    }
}

} // namespace FBXPostProcess
