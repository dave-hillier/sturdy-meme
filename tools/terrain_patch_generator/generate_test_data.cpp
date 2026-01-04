/**
 * Simple test data generator for terrain_patch_generator
 * Creates a synthetic heightmap and river GeoJSON for testing
 */

#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <lodepng.h>

#include <fstream>
#include <vector>
#include <cmath>
#include <random>

using json = nlohmann::json;

// Simple Perlin-like noise
float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
float lerp(float a, float b, float t) { return a + t * (b - a); }

float grad(int hash, float x, float y) {
    int h = hash & 3;
    float u = h < 2 ? x : y;
    float v = h < 2 ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

class PerlinNoise {
    int p[512];
public:
    PerlinNoise(unsigned int seed = 0) {
        std::mt19937 gen(seed);
        std::vector<int> perm(256);
        for (int i = 0; i < 256; ++i) perm[i] = i;
        std::shuffle(perm.begin(), perm.end(), gen);
        for (int i = 0; i < 256; ++i) {
            p[i] = perm[i];
            p[256 + i] = perm[i];
        }
    }

    float noise(float x, float y) const {
        int X = static_cast<int>(std::floor(x)) & 255;
        int Y = static_cast<int>(std::floor(y)) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        float u = fade(x);
        float v = fade(y);
        int A = p[X] + Y;
        int B = p[X + 1] + Y;
        return lerp(lerp(grad(p[A], x, y), grad(p[B], x - 1, y), u),
                    lerp(grad(p[A + 1], x, y - 1), grad(p[B + 1], x - 1, y - 1), u), v);
    }

    float fbm(float x, float y, int octaves = 4) const {
        float result = 0.0f;
        float amp = 1.0f;
        float freq = 1.0f;
        float maxAmp = 0.0f;
        for (int i = 0; i < octaves; ++i) {
            result += amp * noise(x * freq, y * freq);
            maxAmp += amp;
            amp *= 0.5f;
            freq *= 2.0f;
        }
        return result / maxAmp;
    }
};

int main(int argc, char* argv[]) {
    const int SIZE = 256;
    const float TERRAIN_SIZE = 16384.0f;
    std::string outputDir = ".";

    if (argc > 1) {
        outputDir = argv[1];
    }

    SDL_Log("Generating test terrain data in: %s", outputDir.c_str());

    PerlinNoise noise(12345);

    // Generate heightmap
    std::vector<uint16_t> heightmap(SIZE * SIZE);

    for (int y = 0; y < SIZE; ++y) {
        for (int x = 0; x < SIZE; ++x) {
            float fx = static_cast<float>(x) / SIZE;
            float fy = static_cast<float>(y) / SIZE;

            // Base terrain with multiple octaves
            float h = noise.fbm(fx * 4, fy * 4, 5);

            // Add island shape - higher in center
            float cx = fx - 0.5f;
            float cy = fy - 0.5f;
            float distFromCenter = std::sqrt(cx * cx + cy * cy);
            float islandMask = 1.0f - std::min(1.0f, distFromCenter * 2.5f);
            islandMask = islandMask * islandMask;

            h = h * 0.5f + 0.5f;  // Normalize to 0-1
            h = h * islandMask;

            // Add a ridge/plateau in the middle for terracing
            if (distFromCenter < 0.25f && h > 0.3f) {
                h = std::max(h, 0.5f);
            }

            // Convert to 16-bit
            uint16_t height = static_cast<uint16_t>(std::clamp(h, 0.0f, 1.0f) * 65535.0f);
            heightmap[y * SIZE + x] = height;
        }
    }

    // Save heightmap as 16-bit PNG
    std::string heightmapPath = outputDir + "/test_heightmap.png";
    std::vector<unsigned char> pngData(SIZE * SIZE * 2);
    for (int i = 0; i < SIZE * SIZE; ++i) {
        pngData[i * 2] = (heightmap[i] >> 8) & 0xFF;      // High byte
        pngData[i * 2 + 1] = heightmap[i] & 0xFF;          // Low byte
    }

    unsigned error = lodepng::encode(heightmapPath, pngData, SIZE, SIZE, LCT_GREY, 16);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save heightmap: %s",
                     lodepng_error_text(error));
        return 1;
    }
    SDL_Log("Saved heightmap: %s", heightmapPath.c_str());

    // Generate river GeoJSON
    // Create a river flowing from highlands toward coast
    json geojson;
    geojson["type"] = "FeatureCollection";
    geojson["features"] = json::array();

    // River 1: Main river
    {
        json feature;
        feature["type"] = "Feature";
        feature["geometry"]["type"] = "LineString";

        json coords = json::array();
        // Start near center-top, flow to bottom-left
        float startX = TERRAIN_SIZE * 0.55f;
        float startZ = TERRAIN_SIZE * 0.3f;
        float endX = TERRAIN_SIZE * 0.2f;
        float endZ = TERRAIN_SIZE * 0.7f;

        int numPoints = 20;
        for (int i = 0; i < numPoints; ++i) {
            float t = static_cast<float>(i) / (numPoints - 1);
            float x = startX + (endX - startX) * t;
            float z = startZ + (endZ - startZ) * t;
            // Add some meander
            x += std::sin(t * 6.0f) * 200.0f;
            z += std::cos(t * 4.0f) * 100.0f;
            coords.push_back({x, z});
        }

        feature["geometry"]["coordinates"] = coords;
        feature["properties"]["flow"] = 1000.0f;

        json widths = json::array();
        for (int i = 0; i < numPoints; ++i) {
            float t = static_cast<float>(i) / (numPoints - 1);
            widths.push_back(5.0f + t * 15.0f);  // Widens downstream
        }
        feature["properties"]["widths"] = widths;

        geojson["features"].push_back(feature);
    }

    // River 2: Tributary
    {
        json feature;
        feature["type"] = "Feature";
        feature["geometry"]["type"] = "LineString";

        json coords = json::array();
        float startX = TERRAIN_SIZE * 0.7f;
        float startZ = TERRAIN_SIZE * 0.4f;
        float endX = TERRAIN_SIZE * 0.45f;
        float endZ = TERRAIN_SIZE * 0.5f;

        int numPoints = 10;
        for (int i = 0; i < numPoints; ++i) {
            float t = static_cast<float>(i) / (numPoints - 1);
            float x = startX + (endX - startX) * t;
            float z = startZ + (endZ - startZ) * t;
            coords.push_back({x, z});
        }

        feature["geometry"]["coordinates"] = coords;
        feature["properties"]["flow"] = 300.0f;

        json widths = json::array();
        for (int i = 0; i < numPoints; ++i) {
            widths.push_back(3.0f + i * 0.5f);
        }
        feature["properties"]["widths"] = widths;

        geojson["features"].push_back(feature);
    }

    std::string riverPath = outputDir + "/test_rivers.geojson";
    std::ofstream riverFile(riverPath);
    if (!riverFile.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create river file");
        return 1;
    }
    riverFile << geojson.dump(2);
    riverFile.close();
    SDL_Log("Saved rivers: %s", riverPath.c_str());

    // Generate settlements JSON
    // Place settlements at good locations on the terrain
    json settlements;
    settlements["version"] = 1;
    settlements["terrain_size"] = static_cast<int>(TERRAIN_SIZE);
    settlements["settlements"] = json::array();

    // Town - near center on plateau (high ground)
    {
        json s;
        s["id"] = 0;
        s["type"] = "town";
        s["position"] = json::array({TERRAIN_SIZE * 0.45f, TERRAIN_SIZE * 0.45f});
        s["radius"] = 250.0f;
        s["score"] = 85.0f;
        s["features"] = json::array({"market", "castle"});
        settlements["settlements"].push_back(s);
    }

    // Village 1 - near river
    {
        json s;
        s["id"] = 1;
        s["type"] = "village";
        s["position"] = json::array({TERRAIN_SIZE * 0.35f, TERRAIN_SIZE * 0.55f});
        s["radius"] = 150.0f;
        s["score"] = 65.0f;
        s["features"] = json::array({"mill"});
        settlements["settlements"].push_back(s);
    }

    // Village 2 - on other side
    {
        json s;
        s["id"] = 2;
        s["type"] = "village";
        s["position"] = json::array({TERRAIN_SIZE * 0.6f, TERRAIN_SIZE * 0.4f});
        s["radius"] = 120.0f;
        s["score"] = 55.0f;
        s["features"] = json::array();
        settlements["settlements"].push_back(s);
    }

    // Fishing village - near coast/river mouth
    {
        json s;
        s["id"] = 3;
        s["type"] = "fishing_village";
        s["position"] = json::array({TERRAIN_SIZE * 0.25f, TERRAIN_SIZE * 0.65f});
        s["radius"] = 80.0f;
        s["score"] = 45.0f;
        s["features"] = json::array({"dock"});
        settlements["settlements"].push_back(s);
    }

    // Hamlet - isolated
    {
        json s;
        s["id"] = 4;
        s["type"] = "hamlet";
        s["position"] = json::array({TERRAIN_SIZE * 0.7f, TERRAIN_SIZE * 0.6f});
        s["radius"] = 60.0f;
        s["score"] = 30.0f;
        s["features"] = json::array();
        settlements["settlements"].push_back(s);
    }

    std::string settlementsPath = outputDir + "/test_settlements.json";
    std::ofstream settlementsFile(settlementsPath);
    if (!settlementsFile.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create settlements file");
        return 1;
    }
    settlementsFile << settlements.dump(2);
    settlementsFile.close();
    SDL_Log("Saved settlements: %s", settlementsPath.c_str());

    SDL_Log("Done! Run terrain_patch_generator with:");
    SDL_Log("  --heightmap %s/test_heightmap.png", outputDir.c_str());
    SDL_Log("  --rivers %s/test_rivers.geojson", outputDir.c_str());
    SDL_Log("  --settlements %s/test_settlements.json", outputDir.c_str());

    return 0;
}
