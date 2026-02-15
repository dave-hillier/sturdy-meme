// Shared Hi-Z occlusion culling functions
// Used by scene_cull.comp, hiz_culling.comp, and tree_impostor_cull.comp
//
// Requirements: caller must provide:
//   - uniform sampler2D hiZPyramid  (the Hi-Z depth pyramid)
//   - mat4 viewProjMatrix           (current frame view-projection)
//   - vec4 screenParams             (x=width, y=height, z=1/width, w=1/height)
//   - float hiZMaxMipLevel          (max valid mip level, e.g. numMipLevels - 1)
//
// Usage: #include "hiz_occlusion_common.glsl"

#ifndef HIZ_OCCLUSION_COMMON_GLSL
#define HIZ_OCCLUSION_COMMON_GLSL

// Project an AABB to a screen-space bounding rectangle.
// Returns vec4(minScreen.xy, maxScreen.xy) in pixels.
// If any corner is behind the camera, returns full screen rect.
vec4 hizProjectAABBToScreen(vec3 aabbMin, vec3 aabbMax,
                            mat4 viewProjMat, vec4 scrParams) {
    vec3 corners[8];
    corners[0] = vec3(aabbMin.x, aabbMin.y, aabbMin.z);
    corners[1] = vec3(aabbMax.x, aabbMin.y, aabbMin.z);
    corners[2] = vec3(aabbMin.x, aabbMax.y, aabbMin.z);
    corners[3] = vec3(aabbMax.x, aabbMax.y, aabbMin.z);
    corners[4] = vec3(aabbMin.x, aabbMin.y, aabbMax.z);
    corners[5] = vec3(aabbMax.x, aabbMin.y, aabbMax.z);
    corners[6] = vec3(aabbMin.x, aabbMax.y, aabbMax.z);
    corners[7] = vec3(aabbMax.x, aabbMax.y, aabbMax.z);

    vec2 minScreen = vec2(1e10);
    vec2 maxScreen = vec2(-1e10);

    for (int i = 0; i < 8; i++) {
        vec4 clipPos = viewProjMat * vec4(corners[i], 1.0);

        // Corner behind camera — conservatively expand to full screen
        if (clipPos.w <= 0.0) {
            return vec4(0.0, 0.0, scrParams.x, scrParams.y);
        }

        vec3 ndc = clipPos.xyz / clipPos.w;
        vec2 screen = (ndc.xy * 0.5 + 0.5) * scrParams.xy;

        minScreen = min(minScreen, screen);
        maxScreen = max(maxScreen, screen);
    }

    // Clamp to screen bounds
    minScreen = max(minScreen, vec2(0.0));
    maxScreen = min(maxScreen, scrParams.xy);

    return vec4(minScreen, maxScreen);
}

// Get the closest depth of an AABB (maximum in reversed-Z = nearest to camera).
float hizGetClosestDepth(vec3 aabbMin, vec3 aabbMax, mat4 viewProjMat) {
    vec3 corners[8];
    corners[0] = vec3(aabbMin.x, aabbMin.y, aabbMin.z);
    corners[1] = vec3(aabbMax.x, aabbMin.y, aabbMin.z);
    corners[2] = vec3(aabbMin.x, aabbMax.y, aabbMin.z);
    corners[3] = vec3(aabbMax.x, aabbMax.y, aabbMin.z);
    corners[4] = vec3(aabbMin.x, aabbMin.y, aabbMax.z);
    corners[5] = vec3(aabbMax.x, aabbMin.y, aabbMax.z);
    corners[6] = vec3(aabbMin.x, aabbMax.y, aabbMax.z);
    corners[7] = vec3(aabbMax.x, aabbMax.y, aabbMax.z);

    float closestZ = 0.0; // Farthest in reversed-Z

    for (int i = 0; i < 8; i++) {
        vec4 clipPos = viewProjMat * vec4(corners[i], 1.0);
        if (clipPos.w > 0.0) {
            float ndcZ = clipPos.z / clipPos.w;
            closestZ = max(closestZ, ndcZ);
        }
    }

    return closestZ;
}

// Hi-Z occlusion test for an AABB.
// Returns true if the object is occluded (should be culled).
// Uses reversed-Z: larger depth = closer to camera.
bool hizOcclusionTestAABB(vec3 aabbMin, vec3 aabbMax,
                          sampler2D hiZPyr, mat4 viewProjMat,
                          vec4 scrParams, float maxMipLevel) {
    // Project AABB to screen
    vec4 screenRect = hizProjectAABBToScreen(aabbMin, aabbMax, viewProjMat, scrParams);

    // Off screen
    if (screenRect.x >= screenRect.z || screenRect.y >= screenRect.w) {
        return true;
    }

    // Choose mip level: we want the rect to cover roughly 2x2 texels
    vec2 rectSize = screenRect.zw - screenRect.xy;
    float maxDim = max(rectSize.x, rectSize.y);
    float mipLevel = max(0.0, log2(maxDim) - 1.0);
    mipLevel = min(mipLevel, maxMipLevel);

    // Sample Hi-Z at all 4 corners of the projected rectangle.
    // A single center sample can miss occluders that don't cover the AABB center,
    // leading to false culling. Sampling 4 corners and taking the min (farthest
    // in reversed-Z) is conservative: we only cull if the entire region is occluded.
    vec2 minUV = screenRect.xy * scrParams.zw;
    vec2 maxUV = screenRect.zw * scrParams.zw;

    float d0 = textureLod(hiZPyr, vec2(minUV.x, minUV.y), mipLevel).r;
    float d1 = textureLod(hiZPyr, vec2(maxUV.x, minUV.y), mipLevel).r;
    float d2 = textureLod(hiZPyr, vec2(minUV.x, maxUV.y), mipLevel).r;
    float d3 = textureLod(hiZPyr, vec2(maxUV.x, maxUV.y), mipLevel).r;

    // Reversed-Z: min = farthest depth = most conservative for occlusion
    float hiZDepth = min(min(d0, d1), min(d2, d3));

    // Get closest depth of the AABB
    float objectDepth = hizGetClosestDepth(aabbMin, aabbMax, viewProjMat);

    // Reversed-Z: objectDepth < hiZDepth means object is behind the depth buffer
    return objectDepth < hiZDepth;
}

#endif // HIZ_OCCLUSION_COMMON_GLSL
