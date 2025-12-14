#include "VolumeGenerator.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float VolumeGenerator::randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng);
}

glm::vec3 VolumeGenerator::randomPointInVolume(VolumeShape shape,
                                                float radius,
                                                float height,
                                                const glm::vec3& scale) {
    glm::vec3 point;

    switch (shape) {
        case VolumeShape::Sphere: {
            // Uniform distribution in sphere
            float r = radius * std::cbrt(randomFloat(0.0f, 1.0f));
            float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            float phi = std::acos(randomFloat(-1.0f, 1.0f));
            point = glm::vec3(
                r * std::sin(phi) * std::cos(theta),
                r * std::cos(phi),
                r * std::sin(phi) * std::sin(theta)
            );
            break;
        }
        case VolumeShape::Hemisphere: {
            // Upper hemisphere
            float r = radius * std::cbrt(randomFloat(0.0f, 1.0f));
            float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            float phi = std::acos(randomFloat(0.0f, 1.0f));  // Only upper half
            point = glm::vec3(
                r * std::sin(phi) * std::cos(theta),
                r * std::cos(phi),
                r * std::sin(phi) * std::sin(theta)
            );
            break;
        }
        case VolumeShape::Cone: {
            // Uniform in cone (apex at top)
            float h = randomFloat(0.0f, 1.0f);
            float r = radius * (1.0f - h) * std::sqrt(randomFloat(0.0f, 1.0f));
            float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            point = glm::vec3(
                r * std::cos(theta),
                h * height,
                r * std::sin(theta)
            );
            break;
        }
        case VolumeShape::Cylinder: {
            float r = radius * std::sqrt(randomFloat(0.0f, 1.0f));
            float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            float h = randomFloat(0.0f, height);
            point = glm::vec3(
                r * std::cos(theta),
                h,
                r * std::sin(theta)
            );
            break;
        }
        case VolumeShape::Ellipsoid: {
            // Uniform in ellipsoid
            float r = std::cbrt(randomFloat(0.0f, 1.0f));
            float theta = randomFloat(0.0f, 2.0f * static_cast<float>(M_PI));
            float phi = std::acos(randomFloat(-1.0f, 1.0f));
            point = glm::vec3(
                r * radius * scale.x * std::sin(phi) * std::cos(theta),
                r * radius * scale.y * std::cos(phi),
                r * radius * scale.z * std::sin(phi) * std::sin(theta)
            );
            break;
        }
        case VolumeShape::Box: {
            point = glm::vec3(
                randomFloat(-radius, radius) * scale.x,
                randomFloat(0.0f, height) * scale.y,
                randomFloat(-radius, radius) * scale.z
            );
            break;
        }
    }

    return point;
}

bool VolumeGenerator::isPointInVolume(const glm::vec3& point,
                                       const glm::vec3& center,
                                       VolumeShape shape,
                                       float radius,
                                       float height,
                                       const glm::vec3& scale,
                                       float exclusionRadius) {
    glm::vec3 local = point - center;

    // Check exclusion zone first
    if (exclusionRadius > 0.0f && glm::length(local) < exclusionRadius) {
        return false;
    }

    switch (shape) {
        case VolumeShape::Sphere:
            return glm::length(local) <= radius;

        case VolumeShape::Hemisphere:
            return glm::length(local) <= radius && local.y >= 0.0f;

        case VolumeShape::Cone: {
            if (local.y < 0.0f || local.y > height) return false;
            float allowedRadius = radius * (1.0f - local.y / height);
            float distXZ = std::sqrt(local.x * local.x + local.z * local.z);
            return distXZ <= allowedRadius;
        }

        case VolumeShape::Cylinder: {
            if (local.y < 0.0f || local.y > height) return false;
            float distXZ = std::sqrt(local.x * local.x + local.z * local.z);
            return distXZ <= radius;
        }

        case VolumeShape::Ellipsoid: {
            glm::vec3 normalized = local / (radius * scale);
            return glm::dot(normalized, normalized) <= 1.0f;
        }

        case VolumeShape::Box:
            return std::abs(local.x) <= radius * scale.x &&
                   local.y >= 0.0f && local.y <= height * scale.y &&
                   std::abs(local.z) <= radius * scale.z;
    }

    return false;
}

void VolumeGenerator::generateAttractionPoints(const SpaceColonisationParams& scParams,
                                                const glm::vec3& center,
                                                bool isRoot,
                                                std::vector<glm::vec3>& outPoints) {
    VolumeShape shape = isRoot ? scParams.rootShape : scParams.crownShape;
    float radius = isRoot ? scParams.rootRadius : scParams.crownRadius;
    float height = isRoot ? scParams.rootDepth : scParams.crownHeight;
    int count = isRoot ? scParams.rootAttractionPointCount : scParams.attractionPointCount;
    float exclusion = isRoot ? 0.0f : scParams.crownExclusionRadius;

    outPoints.reserve(outPoints.size() + count);

    int attempts = 0;
    int maxAttempts = count * 10;

    while (static_cast<int>(outPoints.size()) < count && attempts < maxAttempts) {
        glm::vec3 localPoint = randomPointInVolume(shape, radius, height, scParams.crownScale);

        // For roots, flip Y to go downward
        if (isRoot) {
            localPoint.y = -std::abs(localPoint.y);
        }

        glm::vec3 worldPoint = center + localPoint;

        // Check exclusion zone
        if (exclusion > 0.0f) {
            glm::vec3 fromCenter = worldPoint - center;
            if (glm::length(fromCenter) < exclusion) {
                attempts++;
                continue;
            }
        }

        outPoints.push_back(worldPoint);
        attempts++;
    }
}
