#pragma once

#include <glm/glm.hpp>

// Hemi-octahedral encoding/decoding utilities for impostor atlas
// These functions match the GLSL implementations in shaders

namespace OctahedralMapping {

// Encode a view direction (upper hemisphere) to UV coordinates [0,1]
inline glm::vec2 hemiOctaEncode(glm::vec3 dir) {
    dir.y = glm::max(dir.y, 0.001f);
    float sum = glm::abs(dir.x) + glm::abs(dir.y) + glm::abs(dir.z);
    dir /= sum;
    glm::vec2 enc(dir.x, dir.z);
    glm::vec2 result;
    result.x = enc.x + enc.y;
    result.y = enc.y - enc.x;
    return result * 0.5f + 0.5f;
}

// Decode UV coordinates [0,1] to a view direction (upper hemisphere)
inline glm::vec3 hemiOctaDecode(glm::vec2 uv) {
    uv = uv * 2.0f - 1.0f;
    glm::vec2 enc;
    enc.x = (uv.x - uv.y) * 0.5f;
    enc.y = (uv.x + uv.y) * 0.5f;
    float y = 1.0f - glm::abs(enc.x) - glm::abs(enc.y);
    return glm::normalize(glm::vec3(enc.x, glm::max(y, 0.0f), enc.y));
}

} // namespace OctahedralMapping
