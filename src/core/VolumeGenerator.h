#pragma once

#include "TreeParameters.h"
#include <glm/glm.hpp>
#include <vector>
#include <random>

// Utilities for generating points within volume shapes
class VolumeGenerator {
public:
    explicit VolumeGenerator(std::mt19937& rng) : rng(rng) {}

    // Generate random point within a volume shape
    glm::vec3 randomPointInVolume(VolumeShape shape,
                                  float radius,
                                  float height,
                                  const glm::vec3& scale);

    // Check if point is inside volume
    static bool isPointInVolume(const glm::vec3& point,
                                const glm::vec3& center,
                                VolumeShape shape,
                                float radius,
                                float height,
                                const glm::vec3& scale,
                                float exclusionRadius);

    // Generate attraction points for space colonisation
    void generateAttractionPoints(const SpaceColonisationParams& scParams,
                                  const glm::vec3& center,
                                  bool isRoot,
                                  std::vector<glm::vec3>& outPoints);

private:
    float randomFloat(float min, float max);

    std::mt19937& rng;
};
