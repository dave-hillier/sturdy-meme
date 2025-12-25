// Octahedral mapping utilities for impostor atlases
// Maps 3D directions to 2D UV coordinates using octahedral projection
// This provides continuous, smooth sampling across all view angles

// Encode a unit direction vector to octahedral UV coordinates [0,1]
// Uses hemispherical mapping (Y-up convention)
// Input: normalized direction FROM tree TO viewer
// Output: UV coordinates in [0,1] range
vec2 octahedralEncode(vec3 dir) {
    // Project onto octahedron: |x| + |y| + |z| = 1
    dir /= (abs(dir.x) + abs(dir.y) + abs(dir.z));

    // For lower hemisphere (Y < 0), fold onto upper hemisphere
    // This is hemispherical mapping - we only care about upper hemisphere for trees
    if (dir.y < 0.0) {
        vec2 signNotZero = vec2(dir.x >= 0.0 ? 1.0 : -1.0, dir.z >= 0.0 ? 1.0 : -1.0);
        dir.xz = (1.0 - abs(dir.zx)) * signNotZero;
    }

    // Map from [-1,1] to [0,1]
    return dir.xz * 0.5 + 0.5;
}

// Decode octahedral UV coordinates back to a unit direction vector
// Input: UV coordinates in [0,1] range
// Output: normalized direction
vec3 octahedralDecode(vec2 uv) {
    // Map from [0,1] to [-1,1]
    uv = uv * 2.0 - 1.0;

    // Reconstruct direction on octahedron
    vec3 dir = vec3(uv.x, 1.0 - abs(uv.x) - abs(uv.y), uv.y);

    // Handle lower hemisphere fold (though for trees we stay in upper)
    if (dir.y < 0.0) {
        vec2 signNotZero = vec2(dir.x >= 0.0 ? 1.0 : -1.0, dir.z >= 0.0 ? 1.0 : -1.0);
        dir.xz = (1.0 - abs(dir.zx)) * signNotZero;
    }

    return normalize(dir);
}

// Convert view direction to impostor-space direction
// Takes the world-space direction from tree to camera and tree's Y rotation
// Returns direction in tree's local space for octahedral lookup
vec3 viewToImpostorSpace(vec3 toCamera, float treeRotation) {
    // Normalize and ensure we're in upper hemisphere
    vec3 dir = normalize(toCamera);

    // Clamp Y to positive (we don't have views from below)
    dir.y = max(dir.y, 0.001);
    dir = normalize(dir);

    // Rotate by tree's rotation to get consistent local-space direction
    float cosR = cos(-treeRotation);
    float sinR = sin(-treeRotation);
    vec3 localDir;
    localDir.x = dir.x * cosR - dir.z * sinR;
    localDir.y = dir.y;
    localDir.z = dir.x * sinR + dir.z * cosR;

    return normalize(localDir);
}

// Compute billboard orientation from octahedral view direction
// Returns the right, up, forward vectors for billboard positioning
// viewDir: direction from tree to camera (world space)
// treeRotation: tree's Y rotation in radians
void computeOctahedralBillboard(
    vec3 viewDir,
    float treeRotation,
    out vec3 right,
    out vec3 up,
    out vec3 forward
) {
    // Elevation angle determines billboard tilt
    float elevation = asin(clamp(viewDir.y, 0.0, 1.0));

    // For nearly top-down views, use flat billboard
    if (elevation > radians(75.0)) {
        forward = vec3(0.0, 1.0, 0.0);
        right = vec3(cos(treeRotation), 0.0, -sin(treeRotation));
        up = vec3(sin(treeRotation), 0.0, cos(treeRotation));
    } else {
        // Normal billboard facing camera, upright but tilted for elevation
        vec3 horizontalDir = normalize(vec3(viewDir.x, 0.0, viewDir.z));
        forward = -horizontalDir;
        up = vec3(0.0, 1.0, 0.0);
        right = cross(up, forward);

        // Tilt billboard to match capture elevation
        // Smoothly interpolate tilt based on elevation
        float tiltAmount = smoothstep(radians(15.0), radians(60.0), elevation);
        float tiltAngle = tiltAmount * radians(45.0);

        vec3 tiltedUp = cos(tiltAngle) * up + sin(tiltAngle) * forward;
        vec3 tiltedForward = -sin(tiltAngle) * up + cos(tiltAngle) * forward;
        forward = tiltedForward;
        up = tiltedUp;
        right = cross(up, forward);
    }
}
