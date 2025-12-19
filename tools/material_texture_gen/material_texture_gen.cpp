// Procedural material texture generator for virtual texturing
// Generates placeholder textures for all biome materials
// Supports both PNG and BCn compressed DDS output

#include <SDL3/SDL_log.h>
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>
#include <lodepng.h>
#include "../common/bc_compress.h"
#include "../common/dds_file.h"
#include <vector>
#include <string>
#include <filesystem>
#include <cmath>
#include <functional>
#include <cstring>

namespace fs = std::filesystem;

// Texture size for generated materials
constexpr int TEXTURE_SIZE = 512;

// Global option for output format
static bool g_useCompression = false;

// Simple noise functions
float fbm(glm::vec2 p, int octaves, float lacunarity = 2.0f, float gain = 0.5f) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * glm::simplex(p * frequency);
        frequency *= lacunarity;
        amplitude *= gain;
    }
    return value;
}

float turbulence(glm::vec2 p, int octaves) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * std::abs(glm::simplex(p * frequency));
        frequency *= 2.0f;
        amplitude *= 0.5f;
    }
    return value;
}

float worley(glm::vec2 p, float scale) {
    glm::vec2 sp = p * scale;
    glm::ivec2 cell = glm::ivec2(glm::floor(sp));
    glm::vec2 frac = glm::fract(sp);

    float minDist = 1.0f;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            glm::ivec2 neighbor = cell + glm::ivec2(x, y);
            // Simple hash for cell center
            glm::vec2 point = glm::vec2(
                glm::fract(std::sin(float(neighbor.x * 127 + neighbor.y * 311)) * 43758.5453f),
                glm::fract(std::sin(float(neighbor.x * 269 + neighbor.y * 183)) * 43758.5453f)
            );
            glm::vec2 diff = point + glm::vec2(x, y) - frac;
            float dist = glm::length(diff);
            minDist = std::min(minDist, dist);
        }
    }
    return minDist;
}

struct TextureGenerator {
    using ColorFunc = std::function<glm::vec3(glm::vec2, float, float)>;

    std::string name;
    glm::vec3 baseColor;
    ColorFunc colorFunc;
    float noiseScale = 8.0f;
    int octaves = 4;
};

glm::vec3 grassColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightGreen(0.35f, 0.55f, 0.2f);
    glm::vec3 darkGreen(0.15f, 0.35f, 0.1f);
    float blend = glm::clamp(noise * 0.5f + 0.5f + detail * 0.3f, 0.0f, 1.0f);
    return glm::mix(darkGreen, lightGreen, blend);
}

glm::vec3 sandColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightSand(0.93f, 0.87f, 0.7f);
    glm::vec3 darkSand(0.75f, 0.65f, 0.45f);
    float blend = glm::clamp(noise * 0.5f + 0.5f + detail * 0.2f, 0.0f, 1.0f);
    return glm::mix(darkSand, lightSand, blend);
}

glm::vec3 wetSandColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightSand(0.7f, 0.62f, 0.5f);
    glm::vec3 darkSand(0.45f, 0.38f, 0.3f);
    float blend = glm::clamp(noise * 0.5f + 0.5f + detail * 0.2f, 0.0f, 1.0f);
    return glm::mix(darkSand, lightSand, blend);
}

glm::vec3 pebbleColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 light(0.65f, 0.6f, 0.55f);
    glm::vec3 dark(0.4f, 0.35f, 0.3f);
    float pebbles = worley(uv, 20.0f);
    float blend = glm::clamp(pebbles + noise * 0.3f, 0.0f, 1.0f);
    return glm::mix(dark, light, blend);
}

glm::vec3 chalkColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 white(0.95f, 0.95f, 0.92f);
    glm::vec3 gray(0.8f, 0.78f, 0.75f);
    float blend = glm::clamp(noise * 0.5f + 0.5f + detail * 0.15f, 0.0f, 1.0f);
    return glm::mix(gray, white, blend);
}

glm::vec3 rockColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 light(0.55f, 0.52f, 0.48f);
    glm::vec3 dark(0.3f, 0.28f, 0.25f);
    float cracks = turbulence(uv * 4.0f, 4);
    float blend = glm::clamp(noise * 0.5f + 0.5f - cracks * 0.3f, 0.0f, 1.0f);
    return glm::mix(dark, light, blend);
}

glm::vec3 mudColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightMud(0.45f, 0.38f, 0.28f);
    glm::vec3 darkMud(0.25f, 0.2f, 0.15f);
    float blend = glm::clamp(noise * 0.5f + 0.5f + detail * 0.25f, 0.0f, 1.0f);
    return glm::mix(darkMud, lightMud, blend);
}

glm::vec3 marshGrassColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightGreen(0.4f, 0.5f, 0.25f);
    glm::vec3 darkGreen(0.2f, 0.3f, 0.12f);
    glm::vec3 brown(0.35f, 0.3f, 0.2f);
    float blend = glm::clamp(noise * 0.5f + 0.5f, 0.0f, 1.0f);
    glm::vec3 grass = glm::mix(darkGreen, lightGreen, blend);
    return glm::mix(grass, brown, detail * 0.4f);
}

glm::vec3 gravelColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 light(0.6f, 0.58f, 0.55f);
    glm::vec3 dark(0.35f, 0.33f, 0.3f);
    float stones = worley(uv, 15.0f);
    float blend = glm::clamp(stones + noise * 0.25f, 0.0f, 1.0f);
    return glm::mix(dark, light, blend);
}

glm::vec3 wetGrassColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightGreen(0.25f, 0.45f, 0.2f);
    glm::vec3 darkGreen(0.1f, 0.25f, 0.08f);
    float blend = glm::clamp(noise * 0.5f + 0.5f + detail * 0.2f, 0.0f, 1.0f);
    return glm::mix(darkGreen, lightGreen, blend);
}

glm::vec3 ploughedColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightBrown(0.5f, 0.4f, 0.3f);
    glm::vec3 darkBrown(0.25f, 0.18f, 0.12f);
    // Add furrow pattern
    float furrows = std::sin(uv.y * 40.0f) * 0.5f + 0.5f;
    float blend = glm::clamp(noise * 0.3f + furrows * 0.5f + 0.2f, 0.0f, 1.0f);
    return glm::mix(darkBrown, lightBrown, blend);
}

glm::vec3 pastureColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightGreen(0.4f, 0.55f, 0.25f);
    glm::vec3 darkGreen(0.25f, 0.4f, 0.15f);
    glm::vec3 yellow(0.55f, 0.55f, 0.3f);
    float blend = glm::clamp(noise * 0.5f + 0.5f, 0.0f, 1.0f);
    glm::vec3 grass = glm::mix(darkGreen, lightGreen, blend);
    return glm::mix(grass, yellow, detail * 0.3f);
}

glm::vec3 forestFloorColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 brown(0.35f, 0.28f, 0.18f);
    glm::vec3 darkBrown(0.18f, 0.12f, 0.08f);
    glm::vec3 leaf(0.45f, 0.38f, 0.2f);
    float blend = glm::clamp(noise * 0.5f + 0.5f, 0.0f, 1.0f);
    glm::vec3 base = glm::mix(darkBrown, brown, blend);
    return glm::mix(base, leaf, detail * 0.4f);
}

glm::vec3 dirtPathColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 light(0.55f, 0.48f, 0.38f);
    glm::vec3 dark(0.35f, 0.28f, 0.2f);
    float blend = glm::clamp(noise * 0.5f + 0.5f + detail * 0.2f, 0.0f, 1.0f);
    return glm::mix(dark, light, blend);
}

glm::vec3 tarmacColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 light(0.35f, 0.35f, 0.38f);
    glm::vec3 dark(0.15f, 0.15f, 0.18f);
    float blend = glm::clamp(noise * 0.3f + 0.4f + detail * 0.1f, 0.0f, 1.0f);
    return glm::mix(dark, light, blend);
}

glm::vec3 waterColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 light(0.2f, 0.4f, 0.5f);
    glm::vec3 dark(0.1f, 0.25f, 0.35f);
    float blend = glm::clamp(noise * 0.5f + 0.5f, 0.0f, 1.0f);
    return glm::mix(dark, light, blend);
}

glm::vec3 wildflowerColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 grass(0.3f, 0.5f, 0.2f);
    glm::vec3 yellow(0.9f, 0.85f, 0.3f);
    glm::vec3 purple(0.6f, 0.4f, 0.7f);
    glm::vec3 white(0.95f, 0.95f, 0.9f);

    float flowerNoise = worley(uv, 30.0f);
    float blend = glm::clamp(noise * 0.5f + 0.5f, 0.0f, 1.0f);

    glm::vec3 base = glm::mix(grass * 0.8f, grass, blend);

    if (flowerNoise < 0.15f) {
        float type = glm::fract(noise * 10.0f);
        if (type < 0.33f) return yellow;
        else if (type < 0.66f) return purple;
        else return white;
    }
    return base;
}

glm::vec3 reedColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightGreen(0.5f, 0.55f, 0.35f);
    glm::vec3 darkGreen(0.3f, 0.35f, 0.2f);
    glm::vec3 brown(0.55f, 0.45f, 0.3f);
    float blend = glm::clamp(noise * 0.5f + 0.5f, 0.0f, 1.0f);
    glm::vec3 base = glm::mix(darkGreen, lightGreen, blend);
    return glm::mix(base, brown, detail * 0.3f);
}

glm::vec3 gorseColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 green(0.25f, 0.35f, 0.15f);
    glm::vec3 yellow(0.85f, 0.8f, 0.2f);
    float flowerNoise = worley(uv, 25.0f);
    if (flowerNoise < 0.2f) return yellow;
    float blend = glm::clamp(noise * 0.5f + 0.5f, 0.0f, 1.0f);
    return glm::mix(green * 0.7f, green, blend);
}

// Get output path with correct extension
std::string getOutputPath(const std::string& basePath) {
    if (g_useCompression) {
        // Replace .png with .dds
        std::string path = basePath;
        size_t pos = path.rfind(".png");
        if (pos != std::string::npos) {
            path.replace(pos, 4, ".dds");
        }
        return path;
    }
    return basePath;
}

bool generateTexture(const std::string& path, TextureGenerator::ColorFunc colorFunc,
                     float noiseScale, int octaves) {
    std::vector<unsigned char> pixels(TEXTURE_SIZE * TEXTURE_SIZE * 4);

    for (int y = 0; y < TEXTURE_SIZE; y++) {
        for (int x = 0; x < TEXTURE_SIZE; x++) {
            glm::vec2 uv = glm::vec2(float(x), float(y)) / float(TEXTURE_SIZE);
            glm::vec2 noisePos = uv * noiseScale;

            float noise = fbm(noisePos, octaves);
            float detail = turbulence(noisePos * 2.0f, 3);

            glm::vec3 color = colorFunc(uv, noise, detail);

            // Clamp and convert to 8-bit
            int idx = (y * TEXTURE_SIZE + x) * 4;
            pixels[idx + 0] = static_cast<unsigned char>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 1] = static_cast<unsigned char>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 2] = static_cast<unsigned char>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
            pixels[idx + 3] = 255;
        }
    }

    std::string outputPath = getOutputPath(path);

    if (g_useCompression) {
        // Compress to BC1 (RGB, 4 bits per pixel)
        BCCompress::CompressedImage compressed = BCCompress::compressImage(
            pixels.data(), TEXTURE_SIZE, TEXTURE_SIZE, BCCompress::BCFormat::BC1);

        if (!DDS::write(outputPath, TEXTURE_SIZE, TEXTURE_SIZE, DDS::Format::BC1_SRGB,
                        compressed.data.data(), static_cast<uint32_t>(compressed.data.size()))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save %s", outputPath.c_str());
            return false;
        }
    } else {
        unsigned error = lodepng::encode(outputPath, pixels, TEXTURE_SIZE, TEXTURE_SIZE);
        if (error) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save %s: %s",
                         outputPath.c_str(), lodepng_error_text(error));
            return false;
        }
    }

    SDL_Log("Generated: %s", outputPath.c_str());
    return true;
}

bool generateNormalMap(const std::string& path, float scale, float strength) {
    std::vector<unsigned char> pixels(TEXTURE_SIZE * TEXTURE_SIZE * 4);

    // First generate height values
    std::vector<float> heights(TEXTURE_SIZE * TEXTURE_SIZE);
    for (int y = 0; y < TEXTURE_SIZE; y++) {
        for (int x = 0; x < TEXTURE_SIZE; x++) {
            glm::vec2 uv = glm::vec2(float(x), float(y)) / float(TEXTURE_SIZE);
            heights[y * TEXTURE_SIZE + x] = fbm(uv * scale, 4) * strength;
        }
    }

    // Then compute normals
    for (int y = 0; y < TEXTURE_SIZE; y++) {
        for (int x = 0; x < TEXTURE_SIZE; x++) {
            int x0 = (x - 1 + TEXTURE_SIZE) % TEXTURE_SIZE;
            int x1 = (x + 1) % TEXTURE_SIZE;
            int y0 = (y - 1 + TEXTURE_SIZE) % TEXTURE_SIZE;
            int y1 = (y + 1) % TEXTURE_SIZE;

            float dzdx = heights[y * TEXTURE_SIZE + x1] - heights[y * TEXTURE_SIZE + x0];
            float dzdy = heights[y1 * TEXTURE_SIZE + x] - heights[y0 * TEXTURE_SIZE + x];

            glm::vec3 normal = glm::normalize(glm::vec3(-dzdx, -dzdy, 1.0f));

            // Convert to 0-1 range
            normal = normal * 0.5f + 0.5f;

            int idx = (y * TEXTURE_SIZE + x) * 4;
            pixels[idx + 0] = static_cast<unsigned char>(normal.x * 255.0f);
            pixels[idx + 1] = static_cast<unsigned char>(normal.y * 255.0f);
            pixels[idx + 2] = static_cast<unsigned char>(normal.z * 255.0f);
            pixels[idx + 3] = 255;
        }
    }

    std::string outputPath = getOutputPath(path);

    if (g_useCompression) {
        // Use BC5 for normal maps (stores X and Y in two channels)
        BCCompress::CompressedImage compressed = BCCompress::compressImage(
            pixels.data(), TEXTURE_SIZE, TEXTURE_SIZE, BCCompress::BCFormat::BC5);

        if (!DDS::write(outputPath, TEXTURE_SIZE, TEXTURE_SIZE, DDS::Format::BC5,
                        compressed.data.data(), static_cast<uint32_t>(compressed.data.size()))) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save %s", outputPath.c_str());
            return false;
        }
    } else {
        unsigned error = lodepng::encode(outputPath, pixels, TEXTURE_SIZE, TEXTURE_SIZE);
        if (error) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save %s: %s",
                         outputPath.c_str(), lodepng_error_text(error));
            return false;
        }
    }

    SDL_Log("Generated normal: %s", outputPath.c_str());
    return true;
}

int main(int argc, char* argv[]) {
    std::string outputDir = "assets/materials";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--compress" || arg == "--dds" || arg == "-c") {
            g_useCompression = true;
        } else if (arg == "--help" || arg == "-h") {
            SDL_Log("Usage: material_texture_gen [options] [output_dir]");
            SDL_Log("Options:");
            SDL_Log("  --compress, --dds, -c  Output BCn compressed DDS files");
            SDL_Log("  --help, -h             Show this help message");
            return 0;
        } else if (arg[0] != '-') {
            outputDir = arg;
        }
    }

    SDL_Log("Material Texture Generator");
    SDL_Log("Output directory: %s", outputDir.c_str());
    SDL_Log("Compression: %s", g_useCompression ? "BC1/BC5 DDS" : "PNG");

    // Create all directories
    std::vector<std::string> dirs = {
        outputDir + "/terrain/beach",
        outputDir + "/terrain/cliff",
        outputDir + "/terrain/marsh",
        outputDir + "/terrain/river",
        outputDir + "/terrain/wetland",
        outputDir + "/terrain/grassland",
        outputDir + "/terrain/agricultural",
        outputDir + "/terrain/woodland",
        outputDir + "/terrain/sea",
        outputDir + "/roads",
        outputDir + "/rivers"
    };

    for (const auto& dir : dirs) {
        fs::create_directories(dir);
    }

    bool success = true;

    // Beach textures
    SDL_Log("Generating beach textures...");
    success &= generateTexture(outputDir + "/terrain/beach/sand_albedo.png", sandColor, 8.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/beach/sand_normal.png", 16.0f, 0.3f);
    success &= generateTexture(outputDir + "/terrain/beach/wet_sand_albedo.png", wetSandColor, 8.0f, 4);
    success &= generateTexture(outputDir + "/terrain/beach/pebbles_albedo.png", pebbleColor, 10.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/beach/pebbles_normal.png", 20.0f, 0.8f);
    success &= generateTexture(outputDir + "/terrain/beach/driftwood_albedo.png", forestFloorColor, 6.0f, 4);
    success &= generateTexture(outputDir + "/terrain/beach/seaweed_albedo.png", marshGrassColor, 10.0f, 4);

    // Cliff textures
    SDL_Log("Generating cliff textures...");
    success &= generateTexture(outputDir + "/terrain/cliff/chalk_albedo.png", chalkColor, 8.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/cliff/chalk_normal.png", 12.0f, 0.5f);
    success &= generateTexture(outputDir + "/terrain/cliff/rock_albedo.png", rockColor, 8.0f, 5);
    success &= generateNormalMap(outputDir + "/terrain/cliff/rock_normal.png", 10.0f, 1.0f);
    success &= generateTexture(outputDir + "/terrain/cliff/exposed_chalk_albedo.png", chalkColor, 6.0f, 3);
    success &= generateTexture(outputDir + "/terrain/cliff/grass_topped_albedo.png", grassColor, 8.0f, 4);
    success &= generateTexture(outputDir + "/terrain/cliff/eroded_chalk_albedo.png", rockColor, 10.0f, 5);
    success &= generateNormalMap(outputDir + "/terrain/cliff/eroded_chalk_normal.png", 15.0f, 1.2f);
    success &= generateTexture(outputDir + "/terrain/cliff/flint_albedo.png", pebbleColor, 12.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/cliff/flint_normal.png", 18.0f, 0.9f);

    // Marsh textures
    SDL_Log("Generating marsh textures...");
    success &= generateTexture(outputDir + "/terrain/marsh/muddy_grass_albedo.png", marshGrassColor, 8.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/marsh/muddy_grass_normal.png", 12.0f, 0.4f);
    success &= generateTexture(outputDir + "/terrain/marsh/mudflat_albedo.png", mudColor, 8.0f, 4);
    success &= generateTexture(outputDir + "/terrain/marsh/saltpan_albedo.png", sandColor, 6.0f, 3);
    success &= generateTexture(outputDir + "/terrain/marsh/cordgrass_albedo.png", reedColor, 10.0f, 4);
    success &= generateTexture(outputDir + "/terrain/marsh/creek_albedo.png", mudColor, 8.0f, 4);

    // River textures
    SDL_Log("Generating river textures...");
    success &= generateTexture(outputDir + "/terrain/river/gravel_albedo.png", gravelColor, 12.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/river/gravel_normal.png", 15.0f, 0.8f);
    success &= generateTexture(outputDir + "/terrain/river/stones_albedo.png", pebbleColor, 8.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/river/stones_normal.png", 12.0f, 1.0f);
    success &= generateTexture(outputDir + "/terrain/river/sand_albedo.png", wetSandColor, 10.0f, 4);
    success &= generateTexture(outputDir + "/terrain/river/mud_albedo.png", mudColor, 8.0f, 4);

    // Wetland textures
    SDL_Log("Generating wetland textures...");
    success &= generateTexture(outputDir + "/terrain/wetland/wet_grass_albedo.png", wetGrassColor, 8.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/wetland/wet_grass_normal.png", 12.0f, 0.4f);
    success &= generateTexture(outputDir + "/terrain/wetland/marsh_grass_albedo.png", marshGrassColor, 10.0f, 4);
    success &= generateTexture(outputDir + "/terrain/wetland/reeds_albedo.png", reedColor, 8.0f, 4);
    success &= generateTexture(outputDir + "/terrain/wetland/muddy_albedo.png", mudColor, 8.0f, 4);
    success &= generateTexture(outputDir + "/terrain/wetland/flooded_albedo.png", waterColor, 6.0f, 3);

    // Grassland textures (chalk downs)
    SDL_Log("Generating grassland textures...");
    success &= generateTexture(outputDir + "/terrain/grassland/chalk_grass_albedo.png", grassColor, 8.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/grassland/chalk_grass_normal.png", 12.0f, 0.35f);
    success &= generateTexture(outputDir + "/terrain/grassland/open_down_albedo.png", grassColor, 6.0f, 4);
    success &= generateTexture(outputDir + "/terrain/grassland/wildflower_albedo.png", wildflowerColor, 10.0f, 4);
    success &= generateTexture(outputDir + "/terrain/grassland/gorse_albedo.png", gorseColor, 8.0f, 4);
    success &= generateTexture(outputDir + "/terrain/grassland/chalk_scrape_albedo.png", chalkColor, 10.0f, 4);

    // Agricultural textures
    SDL_Log("Generating agricultural textures...");
    success &= generateTexture(outputDir + "/terrain/agricultural/ploughed_albedo.png", ploughedColor, 8.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/agricultural/ploughed_normal.png", 8.0f, 0.6f);
    success &= generateTexture(outputDir + "/terrain/agricultural/pasture_albedo.png", pastureColor, 8.0f, 4);
    success &= generateTexture(outputDir + "/terrain/agricultural/crop_albedo.png", grassColor, 6.0f, 3);
    success &= generateTexture(outputDir + "/terrain/agricultural/fallow_albedo.png", dirtPathColor, 8.0f, 4);

    // Woodland textures
    SDL_Log("Generating woodland textures...");
    success &= generateTexture(outputDir + "/terrain/woodland/forest_floor_albedo.png", forestFloorColor, 8.0f, 4);
    success &= generateNormalMap(outputDir + "/terrain/woodland/forest_floor_normal.png", 10.0f, 0.5f);
    success &= generateTexture(outputDir + "/terrain/woodland/beech_floor_albedo.png", forestFloorColor, 6.0f, 4);
    success &= generateTexture(outputDir + "/terrain/woodland/oak_fern_albedo.png", wetGrassColor, 8.0f, 4);
    success &= generateTexture(outputDir + "/terrain/woodland/clearing_albedo.png", grassColor, 8.0f, 4);
    success &= generateTexture(outputDir + "/terrain/woodland/coppice_albedo.png", forestFloorColor, 10.0f, 4);

    // Sea texture (placeholder)
    SDL_Log("Generating sea texture...");
    success &= generateTexture(outputDir + "/terrain/sea/albedo.png", waterColor, 6.0f, 3);

    // Road textures
    SDL_Log("Generating road textures...");
    success &= generateTexture(outputDir + "/roads/footpath_albedo.png", dirtPathColor, 10.0f, 4);
    success &= generateTexture(outputDir + "/roads/bridleway_albedo.png", gravelColor, 12.0f, 4);
    success &= generateNormalMap(outputDir + "/roads/bridleway_normal.png", 15.0f, 0.6f);
    success &= generateTexture(outputDir + "/roads/lane_albedo.png", dirtPathColor, 8.0f, 4);
    success &= generateNormalMap(outputDir + "/roads/lane_normal.png", 10.0f, 0.4f);
    success &= generateTexture(outputDir + "/roads/road_albedo.png", tarmacColor, 8.0f, 3);
    success &= generateNormalMap(outputDir + "/roads/road_normal.png", 12.0f, 0.3f);
    success &= generateTexture(outputDir + "/roads/main_road_albedo.png", tarmacColor, 6.0f, 3);
    success &= generateNormalMap(outputDir + "/roads/main_road_normal.png", 10.0f, 0.25f);

    // Riverbed textures
    SDL_Log("Generating riverbed textures...");
    success &= generateTexture(outputDir + "/rivers/gravel_albedo.png", gravelColor, 15.0f, 4);
    success &= generateTexture(outputDir + "/rivers/mud_albedo.png", mudColor, 10.0f, 4);

    if (success) {
        SDL_Log("All textures generated successfully!");
        return 0;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Some textures failed to generate");
        return 1;
    }
}
