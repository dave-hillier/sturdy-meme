# Phase 1: Foundation - Proper Lighting System

[← Back to Overview](LIGHTING_OVERVIEW.md) | [Next: Phase 2 - Shadow Mapping →](LIGHTING_PHASE2_SHADOWS.md)

---

## 1.1 Light Data Structures

**Goal:** Replace hardcoded lighting with configurable uniform-based system.

**Tasks:**
1. Create `Light` struct in C++ and corresponding GLSL struct
2. Add light uniform buffer to descriptor set layout
3. Support directional light with: direction, color, intensity
4. Add ambient light color/intensity as separate uniform

**Files to modify:**
- `src/Renderer.h` - Add light data structures
- `src/Renderer.cpp` - Create light uniform buffer, update descriptor sets
- `shaders/shader.frag` - Accept light uniforms instead of hardcoded values

**GLSL Light Struct:**
```glsl
struct DirectionalLight {
    vec3 direction;
    float intensity;
    vec3 color;
    float padding;
};

layout(set = 0, binding = 2) uniform LightData {
    DirectionalLight sun;
    vec3 ambientColor;
    float ambientIntensity;
} lights;
```

---

## 1.2 Physically-Based Lighting Model

**Goal:** Implement energy-conserving PBR lighting.

**Tasks:**
1. Add material properties: albedo, roughness, metallic
2. Implement GGX specular distribution function (NDF)
3. Implement Smith geometry/visibility function
4. Implement Schlick Fresnel approximation
5. Keep diffuse as Lambertian (energy-conserving)

**Key Equations:**

```glsl
// GGX Normal Distribution Function
float D_GGX(float NoH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NoH2 = NoH * NoH;
    float denom = NoH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith Visibility Function
float V_SmithGGX(float NoV, float NoL, float roughness) {
    float a = roughness * roughness;
    float GGXV = NoL * sqrt(NoV * NoV * (1.0 - a) + a);
    float GGXL = NoV * sqrt(NoL * NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL + 0.0001);
}

// Schlick Fresnel
vec3 F_Schlick(float VoH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}
```

**Files to create/modify:**
- `shaders/pbr_common.glsl` - Shared PBR functions
- `shaders/shader.frag` - Integrate PBR lighting

---

## 1.3 Normal Mapping Support

**Goal:** Add per-pixel normal detail to surfaces.

**Tasks:**
1. Add tangent vectors to vertex data
2. Calculate TBN matrix in vertex shader
3. Sample normal map and transform to world space
4. Update Mesh class to compute tangents

**Vertex attribute additions:**
```cpp
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec3 tangent;   // NEW
    glm::vec3 bitangent; // NEW
};
```

---

[← Back to Overview](LIGHTING_OVERVIEW.md) | [Next: Phase 2 - Shadow Mapping →](LIGHTING_PHASE2_SHADOWS.md)
