// Cloud Shadow UBO definition - include this in shaders that need cloud shadows
// Separated from main UBO to reduce size for shaders that don't need cloud shadows

#ifndef UBO_CLOUD_SHADOW_GLSL
#define UBO_CLOUD_SHADOW_GLSL

#include "bindings.glsl"

#ifndef CLOUD_SHADOW_UBO_BINDING
#define CLOUD_SHADOW_UBO_BINDING BINDING_CLOUD_SHADOW_UBO
#endif

layout(binding = CLOUD_SHADOW_UBO_BINDING) uniform CloudShadowUBO {
    mat4 cloudShadowMatrix;      // World XZ to cloud shadow UV transform
    float cloudShadowIntensity;  // How dark cloud shadows are (0-1)
    float cloudShadowEnabled;    // 1.0 = enabled, 0.0 = disabled
    vec2 cloudShadowPadding;
} cloudShadow;

#endif // UBO_CLOUD_SHADOW_GLSL
