// Caustics texture generator
// Generates tileable caustics pattern for underwater light effects
// Uses overlapping sine waves to simulate light refraction patterns

#include <SDL3/SDL_log.h>
#include <glm/glm.hpp>
#include <lodepng.h>
#include <vector>
#include <random>
#include <string>
#include <cmath>
#include <algorithm>

struct CausticsConfig {
    int resolution = 512;
    int numWaves = 8;          // Number of overlapping wave patterns
    float brightness = 1.5f;   // Overall brightness multiplier
    float contrast = 2.0f;     // Contrast for sharper caustic lines
    float scale = 4.0f;        // Base frequency scale
    unsigned int seed = 12345;
    std::string outputPath = "assets/textures/caustics.png";
};

// Generate a single caustic wave pattern
float causticWave(glm::vec2 uv, glm::vec2 direction, float frequency, float phase) {
    float wave = sin(glm::dot(uv, direction) * frequency + phase);
    // Square the wave to get sharper caustic-like patterns
    wave = wave * wave;
    return wave;
}

// Generate caustics using overlapping wave interference
float generateCaustics(glm::vec2 uv, const CausticsConfig& config,
                       const std::vector<glm::vec2>& directions,
                       const std::vector<float>& frequencies,
                       const std::vector<float>& phases) {
    float value = 0.0f;

    // Accumulate multiple wave patterns
    for (size_t i = 0; i < directions.size(); i++) {
        float wave = causticWave(uv, directions[i], frequencies[i], phases[i]);
        value += wave;
    }

    // Normalize
    value /= float(directions.size());

    // Apply contrast curve to sharpen caustic lines
    value = pow(value, config.contrast);

    // Boost brightness
    value *= config.brightness;

    return std::clamp(value, 0.0f, 1.0f);
}

// Alternative: Voronoi-based caustics for more organic look
float voronoiCaustics(glm::vec2 uv, float scale, unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    // Grid-based Voronoi for tileability
    glm::vec2 scaledUV = uv * scale;
    glm::vec2 cell = glm::floor(scaledUV);
    glm::vec2 frac = glm::fract(scaledUV);

    float minDist1 = 10.0f;
    float minDist2 = 10.0f;

    // Check 3x3 neighborhood
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            glm::vec2 neighbor = cell + glm::vec2(float(dx), float(dy));

            // Hash the cell to get a random point position
            unsigned int h = static_cast<unsigned int>(neighbor.x + neighbor.y * 127.0f);
            h = (h ^ (h >> 16)) * 0x85ebca6b;
            h = (h ^ (h >> 13)) * 0xc2b2ae35;
            h = h ^ (h >> 16);

            float rx = float(h & 0xFFFF) / 65535.0f;
            float ry = float((h >> 16) & 0xFFFF) / 65535.0f;

            glm::vec2 point = neighbor + glm::vec2(rx, ry);

            // Wrap for tileability
            glm::vec2 wrappedDiff = scaledUV - point;
            wrappedDiff.x = fmod(wrappedDiff.x + scale * 0.5f, scale) - scale * 0.5f;
            wrappedDiff.y = fmod(wrappedDiff.y + scale * 0.5f, scale) - scale * 0.5f;

            float dist = glm::length(frac - glm::vec2(float(dx), float(dy)) - glm::vec2(rx, ry));

            if (dist < minDist1) {
                minDist2 = minDist1;
                minDist1 = dist;
            } else if (dist < minDist2) {
                minDist2 = dist;
            }
        }
    }

    // Use edge distance for caustic-like lines
    float edge = minDist2 - minDist1;

    // Invert and sharpen for caustic appearance
    float caustic = 1.0f - smoothstep(0.0f, 0.15f, edge);
    caustic = pow(caustic, 0.5f); // Soften slightly

    return caustic;
}

float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void printUsage(const char* programName) {
    SDL_Log("Usage: %s [options]", programName);
    SDL_Log("");
    SDL_Log("Options:");
    SDL_Log("  --resolution <n>     Texture resolution (default: 512)");
    SDL_Log("  --waves <n>          Number of wave patterns (default: 8)");
    SDL_Log("  --brightness <f>     Brightness multiplier (default: 1.5)");
    SDL_Log("  --contrast <f>       Contrast exponent (default: 2.0)");
    SDL_Log("  --scale <f>          Base frequency scale (default: 4.0)");
    SDL_Log("  --seed <n>           Random seed (default: 12345)");
    SDL_Log("  --output <path>      Output PNG path (default: assets/textures/caustics.png)");
    SDL_Log("  --help               Show this help");
}

int main(int argc, char* argv[]) {
    CausticsConfig config;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--resolution" && i + 1 < argc) {
            config.resolution = std::stoi(argv[++i]);
        } else if (arg == "--waves" && i + 1 < argc) {
            config.numWaves = std::stoi(argv[++i]);
        } else if (arg == "--brightness" && i + 1 < argc) {
            config.brightness = std::stof(argv[++i]);
        } else if (arg == "--contrast" && i + 1 < argc) {
            config.contrast = std::stof(argv[++i]);
        } else if (arg == "--scale" && i + 1 < argc) {
            config.scale = std::stof(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::stoul(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            config.outputPath = argv[++i];
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown option: %s", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    SDL_Log("Caustics Texture Generator");
    SDL_Log("==========================");
    SDL_Log("Resolution: %d x %d", config.resolution, config.resolution);
    SDL_Log("Number of waves: %d", config.numWaves);
    SDL_Log("Brightness: %.2f", config.brightness);
    SDL_Log("Contrast: %.2f", config.contrast);
    SDL_Log("Scale: %.2f", config.scale);
    SDL_Log("Seed: %u", config.seed);
    SDL_Log("Output: %s", config.outputPath.c_str());

    // Generate random wave parameters
    std::mt19937 rng(config.seed);
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159265359f);
    std::uniform_real_distribution<float> freqDist(0.8f, 1.2f);
    std::uniform_real_distribution<float> phaseDist(0.0f, 2.0f * 3.14159265359f);

    std::vector<glm::vec2> directions;
    std::vector<float> frequencies;
    std::vector<float> phases;

    for (int i = 0; i < config.numWaves; i++) {
        float angle = angleDist(rng);
        directions.push_back(glm::vec2(cos(angle), sin(angle)));
        frequencies.push_back(config.scale * freqDist(rng) * (1.0f + float(i) * 0.3f));
        phases.push_back(phaseDist(rng));
    }

    // Generate caustics texture
    SDL_Log("Generating caustics pattern...");
    std::vector<float> data(config.resolution * config.resolution);

    for (int y = 0; y < config.resolution; y++) {
        for (int x = 0; x < config.resolution; x++) {
            glm::vec2 uv(
                float(x) / float(config.resolution) * 2.0f * 3.14159265359f,
                float(y) / float(config.resolution) * 2.0f * 3.14159265359f
            );

            // Combine wave-based and Voronoi caustics for richer look
            float waveCaustic = generateCaustics(uv, config, directions, frequencies, phases);
            float voronoiCaustic = voronoiCaustics(
                glm::vec2(float(x), float(y)) / float(config.resolution),
                config.scale * 2.0f, config.seed + 999
            );

            // Blend the two patterns
            float value = waveCaustic * 0.6f + voronoiCaustic * 0.4f;

            // Add some variation
            value = std::clamp(value * config.brightness, 0.0f, 1.0f);

            data[y * config.resolution + x] = value;
        }

        if (y % 64 == 0) {
            SDL_Log("  Progress: %d%%", (y * 100) / config.resolution);
        }
    }

    // Convert to 8-bit grayscale PNG
    SDL_Log("Saving PNG...");
    std::vector<unsigned char> imageData(config.resolution * config.resolution);
    for (size_t i = 0; i < data.size(); i++) {
        imageData[i] = static_cast<unsigned char>(std::clamp(data[i] * 255.0f, 0.0f, 255.0f));
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
