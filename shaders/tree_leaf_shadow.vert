#version 450

const int NUM_CASCADES = 4;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 cascadeViewProj[NUM_CASCADES];
    vec4 cascadeSplits;
    vec4 sunDirection;
    vec4 moonDirection;
    vec4 sunColor;
    vec4 moonColor;
    vec4 ambientColor;
    vec4 cameraPosition;
    vec4 pointLightPosition;
    vec4 pointLightColor;
    vec4 windDirectionAndSpeed;
    float timeOfDay;
    float shadowMapSize;
    float debugCascades;
    float julianDay;
} ubo;

// Leaf instance data
struct LeafInstance {
    vec4 position;      // xyz = world position, w = size
    vec4 facing;        // xy = facing direction, z = windPhase, w = flutter
    vec4 color;         // rgb = color tint, a = alpha
    uvec4 metadata;     // x = tree index, y = flags, z = clumpId bits, w = unused
};

layout(std430, binding = 1) readonly buffer LeafBuffer {
    LeafInstance leaves[];
};

// Wind uniform buffer
layout(binding = 2) uniform WindUniforms {
    vec4 windDirectionAndStrength;
    vec4 windParams;
} wind;

layout(push_constant) uniform PushConstants {
    float time;
    int cascadeIndex;
    float padding[2];
} push;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out float fragHash;

// Simple noise for wind
float hash(float p) {
    return fract(sin(p * 127.1) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(dot(i, vec2(127.1, 311.7)));
    float b = hash(dot(i + vec2(1.0, 0.0), vec2(127.1, 311.7)));
    float c = hash(dot(i + vec2(0.0, 1.0), vec2(127.1, 311.7)));
    float d = hash(dot(i + vec2(1.0, 1.0), vec2(127.1, 311.7)));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float sampleWind(vec2 worldPos) {
    vec2 windDir = wind.windDirectionAndStrength.xy;
    float windStrength = wind.windDirectionAndStrength.z;
    float windTime = wind.windParams.w;
    vec2 scrolledPos = worldPos - windDir * windTime * 0.4;
    float n = noise(scrolledPos * 0.1);
    return n * windStrength;
}

void main() {
    uint leafIndex = gl_InstanceIndex;
    uint vertIndex = gl_VertexIndex;

    LeafInstance leaf = leaves[leafIndex];

    vec3 leafPos = leaf.position.xyz;
    float leafSize = leaf.position.w;
    vec2 facingDir = leaf.facing.xy;
    float windPhase = leaf.facing.z;
    float flutter = leaf.facing.w;
    float leafHash = hash(float(leafIndex) * 127.1);

    // Sun direction for shadow casting orientation
    vec3 lightDir = normalize(ubo.sunDirection.xyz);

    // Create billboard facing light (better shadow shape)
    vec3 up = vec3(0.0, 1.0, 0.0);
    up = normalize(up + vec3(facingDir.x, 0.0, facingDir.y) * 0.3);
    vec3 right = normalize(cross(up, lightDir));
    vec3 forward = cross(right, up);

    // Quad vertex positions
    vec2 quadPos;
    vec2 uv;

    if (vertIndex == 0u) {
        quadPos = vec2(-0.5, -0.5); uv = vec2(0.0, 1.0);
    } else if (vertIndex == 1u) {
        quadPos = vec2(0.5, -0.5); uv = vec2(1.0, 1.0);
    } else if (vertIndex == 2u) {
        quadPos = vec2(0.5, 0.5); uv = vec2(1.0, 0.0);
    } else if (vertIndex == 3u) {
        quadPos = vec2(-0.5, -0.5); uv = vec2(0.0, 1.0);
    } else if (vertIndex == 4u) {
        quadPos = vec2(0.5, 0.5); uv = vec2(1.0, 0.0);
    } else {
        quadPos = vec2(-0.5, 0.5); uv = vec2(0.0, 0.0);
    }

    // Apply leaf size
    vec3 offset = right * quadPos.x * leafSize + up * quadPos.y * leafSize;
    vec3 worldPos = leafPos + offset;

    // Apply wind (matching main shader)
    vec2 windDir2D = wind.windDirectionAndStrength.xy;
    float windSample = sampleWind(leafPos.xz);

    float primarySway = windSample * 0.4;
    float flutterFreq = 6.0 + flutter * 4.0;
    float flutterPhase1 = push.time * flutterFreq + windPhase;
    float flutterAmount = sin(flutterPhase1) * flutter * windSample;

    vec3 windOffset = vec3(windDir2D.x, 0.0, windDir2D.y) * primarySway;
    vec3 perpDir = normalize(vec3(-windDir2D.y, 0.25, windDir2D.x));
    windOffset += perpDir * flutterAmount * 0.25;

    worldPos += windOffset;

    // Use cascade light space matrix
    gl_Position = ubo.cascadeViewProj[push.cascadeIndex] * vec4(worldPos, 1.0);

    fragUV = uv;
    fragHash = leafHash;
}
