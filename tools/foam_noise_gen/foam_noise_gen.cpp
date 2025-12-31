// Foam noise texture generator
// Generates tileable Worley (cellular) noise for water foam rendering
// Uses flip-and-blend technique for seamless tiling

#include <SDL3/SDL_log.h>
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>
#include <lodepng.h>
#include <vector>
#include <random>
#include <string>
#include <cmath>
#include <algorithm>
#include "../common/ParallelProgress.h"

struct NoiseConfig {
    int resolution = 512;
    int numPoints = 64;        // Points per layer for Worley noise
    int octaves = 4;           // Number of octaves to layer
    float persistence = 0.5f;  // Amplitude reduction per octave
    float lacunarity = 2.0f;   // Frequency increase per octave
    bool invert = true;        // Invert so cells are white (foam-like)
    unsigned int seed = 42;
    std::string outputPath = "assets/textures/foam_noise.png";
};

// Generate random points for Worley noise (tileable)
std::vector<glm::vec2> generateTileablePoints(int numPoints, unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    std::vector<glm::vec2> points;
    points.reserve(numPoints);

    for (int i = 0; i < numPoints; i++) {
        points.push_back(glm::vec2(dist(rng), dist(rng)));
    }

    return points;
}

// Worley noise at a point (F1 - distance to nearest point)
// Returns tileable result by checking wrapped neighbors
float worleyNoise(glm::vec2 uv, const std::vector<glm::vec2>& points) {
    float minDist = 1.0f;

    // Check all points including wrapped versions for tiling
    for (const auto& p : points) {
        // Check 3x3 grid of wrapped positions
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                glm::vec2 wrappedP = p + glm::vec2(float(dx), float(dy));
                float dist = glm::length(uv - wrappedP);
                minDist = std::min(minDist, dist);
            }
        }
    }

    return minDist;
}

// F2-F1 Worley noise (creates more cellular look)
float worleyNoiseF2F1(glm::vec2 uv, const std::vector<glm::vec2>& points) {
    float minDist1 = 1.0f;
    float minDist2 = 1.0f;

    for (const auto& p : points) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                glm::vec2 wrappedP = p + glm::vec2(float(dx), float(dy));
                float dist = glm::length(uv - wrappedP);

                if (dist < minDist1) {
                    minDist2 = minDist1;
                    minDist1 = dist;
                } else if (dist < minDist2) {
                    minDist2 = dist;
                }
            }
        }
    }

    return minDist2 - minDist1;
}

// Generate multi-octave Worley noise
float generateFBMWorley(glm::vec2 uv, const NoiseConfig& config,
                        const std::vector<std::vector<glm::vec2>>& pointsPerOctave) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < config.octaves; i++) {
        // Scale UV by frequency and wrap to [0,1]
        glm::vec2 scaledUV = glm::fract(uv * frequency);

        // Use F2-F1 for more defined cell edges (foam-like)
        float noise = worleyNoiseF2F1(scaledUV, pointsPerOctave[i]);

        value += noise * amplitude;
        maxValue += amplitude;

        amplitude *= config.persistence;
        frequency *= config.lacunarity;
    }

    return value / maxValue;
}

// Make texture seamlessly tileable using flip-and-blend (parallelized)
void makeSeamless(std::vector<float>& data, int resolution) {
    // Create a copy for blending
    std::vector<float> original = data;

    // Blend size (how far from edge to blend)
    int blendSize = resolution / 4;

    ParallelProgress::parallel_for(0, resolution, [&](int y) {
        for (int x = 0; x < resolution; x++) {
            float origValue = original[y * resolution + x];

            // Left edge - blend with right side (flipped)
            if (x < blendSize) {
                float t = float(x) / float(blendSize);
                t = t * t * (3.0f - 2.0f * t); // Smoothstep
                int mirrorX = resolution - 1 - x;
                float mirrorValue = original[y * resolution + mirrorX];
                origValue = glm::mix(mirrorValue, origValue, t);
            }
            // Right edge - blend with left side (flipped)
            else if (x >= resolution - blendSize) {
                float t = float(resolution - 1 - x) / float(blendSize);
                t = t * t * (3.0f - 2.0f * t);
                int mirrorX = resolution - 1 - x;
                float mirrorValue = original[y * resolution + mirrorX];
                origValue = glm::mix(mirrorValue, origValue, t);
            }

            // Top edge - blend with bottom (flipped)
            if (y < blendSize) {
                float t = float(y) / float(blendSize);
                t = t * t * (3.0f - 2.0f * t);
                int mirrorY = resolution - 1 - y;
                float mirrorValue = original[mirrorY * resolution + x];
                origValue = glm::mix(mirrorValue, origValue, t);
            }
            // Bottom edge - blend with top (flipped)
            else if (y >= resolution - blendSize) {
                float t = float(resolution - 1 - y) / float(blendSize);
                t = t * t * (3.0f - 2.0f * t);
                int mirrorY = resolution - 1 - y;
                float mirrorValue = original[mirrorY * resolution + x];
                origValue = glm::mix(mirrorValue, origValue, t);
            }

            data[y * resolution + x] = origValue;
        }
    });
}

void printUsage(const char* programName) {
    SDL_Log("Usage: %s [options]", programName);
    SDL_Log("");
    SDL_Log("Options:");
    SDL_Log("  --resolution <n>     Texture resolution (default: 512)");
    SDL_Log("  --points <n>         Worley points per octave (default: 64)");
    SDL_Log("  --octaves <n>        Number of FBM octaves (default: 4)");
    SDL_Log("  --persistence <f>    Amplitude falloff (default: 0.5)");
    SDL_Log("  --seed <n>           Random seed (default: 42)");
    SDL_Log("  --output <path>      Output PNG path (default: assets/textures/foam_noise.png)");
    SDL_Log("  --no-invert          Don't invert (cells dark instead of white)");
    SDL_Log("  --help               Show this help");
}

int main(int argc, char* argv[]) {
    NoiseConfig config;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--resolution" && i + 1 < argc) {
            config.resolution = std::stoi(argv[++i]);
        } else if (arg == "--points" && i + 1 < argc) {
            config.numPoints = std::stoi(argv[++i]);
        } else if (arg == "--octaves" && i + 1 < argc) {
            config.octaves = std::stoi(argv[++i]);
        } else if (arg == "--persistence" && i + 1 < argc) {
            config.persistence = std::stof(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::stoul(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            config.outputPath = argv[++i];
        } else if (arg == "--no-invert") {
            config.invert = false;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown option: %s", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    SDL_Log("Foam Noise Texture Generator");
    SDL_Log("============================");
    SDL_Log("Resolution: %d x %d", config.resolution, config.resolution);
    SDL_Log("Worley points per octave: %d", config.numPoints);
    SDL_Log("Octaves: %d", config.octaves);
    SDL_Log("Persistence: %.2f", config.persistence);
    SDL_Log("Seed: %u", config.seed);
    SDL_Log("Output: %s", config.outputPath.c_str());

    // Generate point sets for each octave (different seeds)
    std::vector<std::vector<glm::vec2>> pointsPerOctave;
    for (int i = 0; i < config.octaves; i++) {
        // More points at higher frequencies for detail
        int numPoints = config.numPoints * (1 << i);
        numPoints = std::min(numPoints, 512); // Cap for performance
        pointsPerOctave.push_back(generateTileablePoints(numPoints, config.seed + i * 1337));
    }

    // Generate noise using parallel processing
    SDL_Log("Generating Worley noise (%u threads)...", ParallelProgress::getThreadCount());
    std::vector<float> noiseData(config.resolution * config.resolution);

    // Thread-safe min/max accumulator
    ParallelProgress::MinMaxAccumulator<float> minmax(1.0f, 0.0f);

    ParallelProgress::parallel_for_progress(0, config.resolution, [&](int y) {
        float localMin = 1.0f, localMax = 0.0f;

        for (int x = 0; x < config.resolution; x++) {
            glm::vec2 uv(
                float(x) / float(config.resolution),
                float(y) / float(config.resolution)
            );

            float value = generateFBMWorley(uv, config, pointsPerOctave);
            noiseData[y * config.resolution + x] = value;

            localMin = std::min(localMin, value);
            localMax = std::max(localMax, value);
        }

        minmax.update(localMin, localMax);
    }, nullptr, "Generating noise");

    float minVal = minmax.getMin();
    float maxVal = minmax.getMax();

    // Normalize to 0-1
    SDL_Log("Normalizing (range was %.3f - %.3f)...", minVal, maxVal);
    float range = maxVal - minVal;
    if (range > 0.0001f) {
        for (auto& v : noiseData) {
            v = (v - minVal) / range;
        }
    }

    // Apply contrast curve to make foam more defined
    SDL_Log("Applying contrast curve...");
    for (auto& v : noiseData) {
        // S-curve for more contrast
        v = v * v * (3.0f - 2.0f * v);
        // Clamp
        v = std::clamp(v, 0.0f, 1.0f);
    }

    // Make seamlessly tileable
    SDL_Log("Making seamlessly tileable...");
    makeSeamless(noiseData, config.resolution);

    // Invert if requested (foam = white cells)
    if (config.invert) {
        SDL_Log("Inverting...");
        for (auto& v : noiseData) {
            v = 1.0f - v;
        }
    }

    // Convert to 8-bit grayscale PNG
    SDL_Log("Saving PNG...");
    std::vector<unsigned char> imageData(config.resolution * config.resolution);
    for (size_t i = 0; i < noiseData.size(); i++) {
        imageData[i] = static_cast<unsigned char>(std::clamp(noiseData[i] * 255.0f, 0.0f, 255.0f));
    }

    unsigned error = lodepng::encode(config.outputPath, imageData,
                                     config.resolution, config.resolution,
                                     LCT_GREY, 8);

    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PNG encode error %u: %s",
                     error, lodepng_error_text(error));
        return 1;
    }

    SDL_Log("Successfully wrote %s", config.outputPath.c_str());
    return 0;
}
