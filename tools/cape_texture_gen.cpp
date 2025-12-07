// Cape texture generator
// Generates a fabric-like texture with decorative trim for the player cape

#include <SDL3/SDL_log.h>
#include <glm/glm.hpp>
#include <lodepng.h>
#include <vector>
#include <random>
#include <string>
#include <cmath>
#include <algorithm>

struct CapeConfig {
    int width = 256;
    int height = 512;
    glm::vec3 baseColor = glm::vec3(0.6f, 0.1f, 0.1f);    // Deep red
    glm::vec3 trimColor = glm::vec3(0.9f, 0.75f, 0.2f);   // Gold
    glm::vec3 innerColor = glm::vec3(0.15f, 0.1f, 0.25f); // Dark purple lining
    float trimWidth = 0.08f;   // Trim width as fraction of texture
    bool addPattern = true;
    unsigned int seed = 42;
    std::string outputPath = "assets/textures/cape_diffuse.png";
    std::string normalPath = "assets/textures/cape_normal.png";
};

// Simple noise for fabric texture
float hash(glm::vec2 p) {
    return glm::fract(std::sin(glm::dot(p, glm::vec2(127.1f, 311.7f))) * 43758.5453f);
}

float noise(glm::vec2 p) {
    glm::vec2 i = glm::floor(p);
    glm::vec2 f = glm::fract(p);
    f = f * f * (3.0f - 2.0f * f);

    float a = hash(i);
    float b = hash(i + glm::vec2(1.0f, 0.0f));
    float c = hash(i + glm::vec2(0.0f, 1.0f));
    float d = hash(i + glm::vec2(1.0f, 1.0f));

    return glm::mix(glm::mix(a, b, f.x), glm::mix(c, d, f.x), f.y);
}

float fbm(glm::vec2 p, int octaves) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return value;
}

// Generate weave pattern for fabric
float weavePattern(glm::vec2 uv, float scale) {
    glm::vec2 p = uv * scale;

    // Create a warp and weft pattern
    float warp = std::sin(p.x * 3.14159f * 2.0f) * 0.5f + 0.5f;
    float weft = std::sin(p.y * 3.14159f * 2.0f) * 0.5f + 0.5f;

    // Interleave pattern
    float pattern = glm::mix(warp, weft, std::fmod(std::floor(p.x) + std::floor(p.y), 2.0f));

    return pattern * 0.15f + 0.85f;  // Subtle variation
}

// Diamond/rhombus pattern for decoration
float diamondPattern(glm::vec2 uv, float scale) {
    glm::vec2 p = glm::fract(uv * scale);
    p = glm::abs(p - 0.5f);
    float d = p.x + p.y;
    return d < 0.35f ? 1.0f : 0.0f;
}

void generateCapeTexture(const CapeConfig& config) {
    SDL_Log("Generating cape diffuse texture...");

    std::vector<unsigned char> imageData(config.width * config.height * 4);
    std::mt19937 rng(config.seed);
    std::uniform_real_distribution<float> dist(-0.02f, 0.02f);

    for (int y = 0; y < config.height; y++) {
        for (int x = 0; x < config.width; x++) {
            glm::vec2 uv(
                float(x) / float(config.width),
                float(y) / float(config.height)
            );

            glm::vec3 color = config.baseColor;

            // Fabric noise for subtle variation
            float fabricNoise = fbm(uv * 50.0f, 4);
            color *= 0.9f + fabricNoise * 0.2f;

            // Weave pattern
            float weave = weavePattern(uv, 40.0f);
            color *= weave;

            // Edge trim
            float edgeDist = std::min({uv.x, 1.0f - uv.x, uv.y, 1.0f - uv.y});
            if (edgeDist < config.trimWidth) {
                float t = edgeDist / config.trimWidth;
                t = t * t;  // Smooth edge

                // Inner trim line
                if (edgeDist > config.trimWidth * 0.3f && edgeDist < config.trimWidth * 0.7f) {
                    color = glm::mix(config.trimColor, color, t);
                } else {
                    color = glm::mix(config.trimColor * 0.8f, color, t);
                }
            }

            // Diamond pattern in central area
            if (config.addPattern && edgeDist > config.trimWidth * 1.5f) {
                float diamond = diamondPattern(uv - glm::vec2(0.5f), 4.0f);
                if (diamond > 0.5f) {
                    color = glm::mix(color, config.trimColor * 0.7f, 0.3f);
                }
            }

            // Bottom hem decoration
            if (uv.y > 0.9f) {
                float hemPattern = std::sin(uv.x * 3.14159f * 20.0f) * 0.5f + 0.5f;
                color = glm::mix(color, config.trimColor, hemPattern * 0.2f * (uv.y - 0.9f) * 10.0f);
            }

            // Add slight random variation
            color += glm::vec3(dist(rng), dist(rng), dist(rng));

            // Clamp and convert to bytes
            color = glm::clamp(color, 0.0f, 1.0f);

            int idx = (y * config.width + x) * 4;
            imageData[idx + 0] = static_cast<unsigned char>(color.r * 255.0f);
            imageData[idx + 1] = static_cast<unsigned char>(color.g * 255.0f);
            imageData[idx + 2] = static_cast<unsigned char>(color.b * 255.0f);
            imageData[idx + 3] = 255;  // Opaque
        }
    }

    unsigned error = lodepng::encode(config.outputPath, imageData,
                                     config.width, config.height);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PNG encode error: %s",
                     lodepng_error_text(error));
        return;
    }

    SDL_Log("Saved diffuse texture: %s", config.outputPath.c_str());
}

void generateCapeNormal(const CapeConfig& config) {
    SDL_Log("Generating cape normal map...");

    std::vector<unsigned char> imageData(config.width * config.height * 4);

    // Generate height map first
    std::vector<float> heightMap(config.width * config.height);

    for (int y = 0; y < config.height; y++) {
        for (int x = 0; x < config.width; x++) {
            glm::vec2 uv(
                float(x) / float(config.width),
                float(y) / float(config.height)
            );

            // Fabric weave creates slight bumps
            float height = 0.0f;

            // Weave pattern normals
            float weaveH = weavePattern(uv, 40.0f);
            height += weaveH * 0.3f;

            // Add fabric noise
            height += fbm(uv * 80.0f, 3) * 0.2f;

            // Trim is slightly raised
            float edgeDist = std::min({uv.x, 1.0f - uv.x, uv.y, 1.0f - uv.y});
            if (edgeDist < config.trimWidth * 0.7f) {
                height += 0.3f * (1.0f - edgeDist / (config.trimWidth * 0.7f));
            }

            heightMap[y * config.width + x] = height;
        }
    }

    // Convert height map to normal map using Sobel
    for (int y = 0; y < config.height; y++) {
        for (int x = 0; x < config.width; x++) {
            int xp = std::min(x + 1, config.width - 1);
            int xm = std::max(x - 1, 0);
            int yp = std::min(y + 1, config.height - 1);
            int ym = std::max(y - 1, 0);

            float dX = heightMap[y * config.width + xp] - heightMap[y * config.width + xm];
            float dY = heightMap[yp * config.width + x] - heightMap[ym * config.width + x];

            glm::vec3 normal = glm::normalize(glm::vec3(-dX * 2.0f, -dY * 2.0f, 1.0f));

            // Convert from [-1,1] to [0,1] for storage
            normal = normal * 0.5f + 0.5f;

            int idx = (y * config.width + x) * 4;
            imageData[idx + 0] = static_cast<unsigned char>(normal.x * 255.0f);
            imageData[idx + 1] = static_cast<unsigned char>(normal.y * 255.0f);
            imageData[idx + 2] = static_cast<unsigned char>(normal.z * 255.0f);
            imageData[idx + 3] = 255;
        }
    }

    unsigned error = lodepng::encode(config.normalPath, imageData,
                                     config.width, config.height);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PNG encode error: %s",
                     lodepng_error_text(error));
        return;
    }

    SDL_Log("Saved normal map: %s", config.normalPath.c_str());
}

void printUsage(const char* programName) {
    SDL_Log("Usage: %s [options]", programName);
    SDL_Log("Options:");
    SDL_Log("  --width <n>          Texture width (default: 256)");
    SDL_Log("  --height <n>         Texture height (default: 512)");
    SDL_Log("  --output <path>      Output diffuse PNG path");
    SDL_Log("  --normal <path>      Output normal PNG path");
    SDL_Log("  --color <r,g,b>      Base color (0-1 values, comma separated)");
    SDL_Log("  --seed <n>           Random seed (default: 42)");
    SDL_Log("  --no-pattern         Disable diamond pattern");
    SDL_Log("  --help               Show this help");
}

int main(int argc, char* argv[]) {
    CapeConfig config;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--width" && i + 1 < argc) {
            config.width = std::stoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            config.height = std::stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            config.outputPath = argv[++i];
        } else if (arg == "--normal" && i + 1 < argc) {
            config.normalPath = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::stoul(argv[++i]);
        } else if (arg == "--no-pattern") {
            config.addPattern = false;
        } else if (arg == "--color" && i + 1 < argc) {
            std::string colorStr = argv[++i];
            float r, g, b;
            if (sscanf(colorStr.c_str(), "%f,%f,%f", &r, &g, &b) == 3) {
                config.baseColor = glm::vec3(r, g, b);
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown option: %s", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    SDL_Log("Cape Texture Generator");
    SDL_Log("======================");
    SDL_Log("Resolution: %d x %d", config.width, config.height);
    SDL_Log("Base color: (%.2f, %.2f, %.2f)", config.baseColor.r, config.baseColor.g, config.baseColor.b);
    SDL_Log("Pattern: %s", config.addPattern ? "enabled" : "disabled");

    generateCapeTexture(config);
    generateCapeNormal(config);

    SDL_Log("Done!");
    return 0;
}
