// SBSAR file renderer
// Processes Substance Archive (.sbsar) files to generate texture maps
// Uses Adobe's sbsrender CLI tool if available, otherwise generates fallback textures
// with procedural noise-based detail
//
// .sbsar files are ZIP archives containing:
// - XML metadata describing inputs, outputs, and presets
// - .sbsasm binary compiled substance graph files

#include <SDL3/SDL_log.h>
#include <glm/glm.hpp>
#include <lodepng.h>
#include <miniz.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

struct RenderConfig {
    std::string inputPath;
    std::string outputDir;
    std::string outputName;
    int resolution = 1024;
    bool generateFallback = true;
    bool verbose = false;
};

// Output map types that Substance materials can produce
struct OutputMap {
    std::string name;
    std::string identifier;
    glm::vec4 fallbackColor;
    bool isSRGB;
};

// Common Substance output map types with sensible fallback colors
static const std::vector<OutputMap> STANDARD_OUTPUTS = {
    {"basecolor", "basecolor", glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), true},
    {"normal", "normal", glm::vec4(0.5f, 0.5f, 1.0f, 1.0f), false},
    {"roughness", "roughness", glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), false},
    {"metallic", "metallic", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), false},
    {"height", "height", glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), false},
    {"ambientocclusion", "ambientocclusion", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), false},
    {"emissive", "emissive", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), true},
};

// ============================================================================
// Material Parameters extracted from .sbsar archive
// ============================================================================

struct MaterialParameters {
    // Base colors
    glm::vec4 baseColor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    glm::vec4 emissiveColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    // PBR values
    float roughness = 0.5f;
    float metallic = 0.0f;
    float normalIntensity = 1.0f;
    float heightScale = 0.5f;

    // Pattern controls
    float patternScale = 8.0f;
    float patternRandomness = 0.8f;
    int patternOctaves = 6;

    // Material type hint
    std::string materialType;  // e.g., "stone", "wood", "metal", "fabric"
    std::string materialName;

    // Parsed from archive
    bool parsed = false;
};

// Simple XML attribute parser (finds value="..." or just the text content)
std::string extractXmlAttribute(const std::string& xml, const std::string& tag,
                                 const std::string& attr) {
    std::string searchTag = "<" + tag;
    size_t tagStart = xml.find(searchTag);
    while (tagStart != std::string::npos) {
        size_t tagEnd = xml.find('>', tagStart);
        if (tagEnd == std::string::npos) break;

        std::string tagContent = xml.substr(tagStart, tagEnd - tagStart);
        std::string attrSearch = attr + "=\"";
        size_t attrPos = tagContent.find(attrSearch);
        if (attrPos != std::string::npos) {
            size_t valueStart = attrPos + attrSearch.length();
            size_t valueEnd = tagContent.find('"', valueStart);
            if (valueEnd != std::string::npos) {
                return tagContent.substr(valueStart, valueEnd - valueStart);
            }
        }
        tagStart = xml.find(searchTag, tagEnd);
    }
    return "";
}

// Extract all values for a specific input parameter
std::vector<float> extractInputValues(const std::string& xml, const std::string& inputId) {
    std::vector<float> values;

    // Look for input definitions like <input identifier="basecolor" ...>
    // and extract default values from the same element
    std::string searchPattern = "identifier=\"" + inputId + "\"";
    size_t pos = xml.find(searchPattern);
    if (pos != std::string::npos) {
        // Find the start of this <input element
        size_t elementStart = xml.rfind('<', pos);
        // Find the end of this element (either /> or </input>)
        size_t elementEnd = xml.find('>', pos);
        if (elementEnd != std::string::npos && xml[elementEnd - 1] != '/') {
            // Not self-closing, find the closing tag
            elementEnd = xml.find("</", pos);
        }

        if (elementStart != std::string::npos && elementEnd != std::string::npos) {
            std::string element = xml.substr(elementStart, elementEnd - elementStart + 1);

            // Try to find float values like value="0.5" or default="0.5" within THIS element
            for (const auto& attr : {"default", "value", "defaultvalue"}) {
                std::string attrSearch = std::string(attr) + "=\"";
                size_t attrPos = element.find(attrSearch);
                if (attrPos != std::string::npos) {
                    size_t valueStart = attrPos + attrSearch.length();
                    size_t valueEnd = element.find('"', valueStart);
                    if (valueEnd != std::string::npos) {
                        std::string valueStr = element.substr(valueStart, valueEnd - valueStart);
                        try {
                            // Handle comma-separated values (for colors)
                            std::istringstream iss(valueStr);
                            std::string token;
                            while (std::getline(iss, token, ',')) {
                                values.push_back(std::stof(token));
                            }
                            if (!values.empty()) break;  // Found values, stop looking
                        } catch (...) {
                            // Ignore parse errors
                        }
                    }
                }
            }
        }
    }
    return values;
}

// Parse material parameters from XML content
MaterialParameters parseXmlParameters(const std::string& xml) {
    MaterialParameters params;

    // Extract material name/label
    params.materialName = extractXmlAttribute(xml, "graph", "label");
    if (params.materialName.empty()) {
        params.materialName = extractXmlAttribute(xml, "package", "label");
    }

    // Try to determine material type from keywords in the XML
    std::string lowerXml = xml;
    std::transform(lowerXml.begin(), lowerXml.end(), lowerXml.begin(), ::tolower);

    if (lowerXml.find("stone") != std::string::npos ||
        lowerXml.find("rock") != std::string::npos ||
        lowerXml.find("brick") != std::string::npos) {
        params.materialType = "stone";
        params.roughness = 0.7f;
        params.patternScale = 4.0f;
    } else if (lowerXml.find("wood") != std::string::npos ||
               lowerXml.find("bark") != std::string::npos) {
        params.materialType = "wood";
        params.roughness = 0.6f;
        params.patternScale = 6.0f;
        params.baseColor = glm::vec4(0.4f, 0.25f, 0.15f, 1.0f);
    } else if (lowerXml.find("metal") != std::string::npos ||
               lowerXml.find("steel") != std::string::npos ||
               lowerXml.find("iron") != std::string::npos) {
        params.materialType = "metal";
        params.metallic = 0.9f;
        params.roughness = 0.3f;
        params.baseColor = glm::vec4(0.7f, 0.7f, 0.75f, 1.0f);
    } else if (lowerXml.find("fabric") != std::string::npos ||
               lowerXml.find("cloth") != std::string::npos ||
               lowerXml.find("leather") != std::string::npos) {
        params.materialType = "fabric";
        params.roughness = 0.8f;
        params.patternScale = 12.0f;
    } else if (lowerXml.find("sand") != std::string::npos ||
               lowerXml.find("dirt") != std::string::npos ||
               lowerXml.find("ground") != std::string::npos) {
        params.materialType = "ground";
        params.roughness = 0.9f;
        params.baseColor = glm::vec4(0.6f, 0.5f, 0.4f, 1.0f);
    } else if (lowerXml.find("grass") != std::string::npos) {
        params.materialType = "grass";
        params.roughness = 0.7f;
        params.baseColor = glm::vec4(0.3f, 0.5f, 0.2f, 1.0f);
    }

    // Try to extract explicit color values
    auto colorValues = extractInputValues(xml, "basecolor");
    if (colorValues.size() >= 3) {
        params.baseColor = glm::vec4(colorValues[0], colorValues[1], colorValues[2],
                                     colorValues.size() > 3 ? colorValues[3] : 1.0f);
    }

    // Try to extract roughness
    auto roughnessValues = extractInputValues(xml, "roughness");
    if (!roughnessValues.empty()) {
        params.roughness = roughnessValues[0];
    }

    // Try to extract metallic
    auto metallicValues = extractInputValues(xml, "metallic");
    if (!metallicValues.empty()) {
        params.metallic = metallicValues[0];
    }

    params.parsed = true;
    return params;
}

// Extract and parse .sbsar archive to get material parameters
MaterialParameters parseSbsarArchive(const std::string& path) {
    MaterialParameters params;

    SDL_Log("Parsing SBSAR archive: %s", path.c_str());

    // Open the archive using miniz
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to open SBSAR as ZIP archive: %s", path.c_str());
        return params;
    }

    // List files in archive and look for XML
    int numFiles = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    SDL_Log("SBSAR archive contains %d files", numFiles);

    std::string xmlContent;

    for (int i = 0; i < numFiles; i++) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            continue;
        }

        std::string filename = fileStat.m_filename;
        SDL_Log("  Archive file: %s (%zu bytes)", filename.c_str(),
                static_cast<size_t>(fileStat.m_uncomp_size));

        // Look for XML files (substance description)
        if (filename.find(".xml") != std::string::npos ||
            filename.find("desc") != std::string::npos) {

            // Extract file content
            size_t uncompSize = static_cast<size_t>(fileStat.m_uncomp_size);
            std::vector<char> buffer(uncompSize + 1);

            if (mz_zip_reader_extract_to_mem(&zip, i, buffer.data(), uncompSize, 0)) {
                buffer[uncompSize] = '\0';
                xmlContent = buffer.data();
                SDL_Log("  Extracted XML content (%zu bytes)", xmlContent.size());
            }
        }
    }

    mz_zip_reader_end(&zip);

    // Parse XML if found
    if (!xmlContent.empty()) {
        params = parseXmlParameters(xmlContent);
        if (!params.materialName.empty()) {
            SDL_Log("Material name: %s", params.materialName.c_str());
        }
        if (!params.materialType.empty()) {
            SDL_Log("Material type: %s", params.materialType.c_str());
        }
        SDL_Log("Extracted parameters - baseColor: (%.2f, %.2f, %.2f), roughness: %.2f, metallic: %.2f",
                params.baseColor.r, params.baseColor.g, params.baseColor.b,
                params.roughness, params.metallic);
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "No XML metadata found in SBSAR archive");
    }

    return params;
}

// ============================================================================
// Procedural Noise Generation
// ============================================================================

// Permutation table for Perlin noise (doubled to avoid modulo operations)
static int perm[512];
static bool permInitialized = false;

void initPermutationTable(unsigned int seed) {
    if (permInitialized) return;

    std::mt19937 rng(seed);
    for (int i = 0; i < 256; i++) {
        perm[i] = i;
    }
    for (int i = 255; i > 0; i--) {
        std::uniform_int_distribution<int> dist(0, i);
        int j = dist(rng);
        std::swap(perm[i], perm[j]);
    }
    for (int i = 0; i < 256; i++) {
        perm[256 + i] = perm[i];
    }
    permInitialized = true;
}

// Fade function for smooth interpolation
float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Linear interpolation
float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

// Gradient function - returns dot product with gradient vector
float grad(int hash, float x, float y) {
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0f * v : 2.0f * v);
}

// 2D Perlin noise function
float perlinNoise(float x, float y) {
    // Find unit grid cell containing point
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;

    // Get relative position within cell
    x -= std::floor(x);
    y -= std::floor(y);

    // Compute fade curves
    float u = fade(x);
    float v = fade(y);

    // Hash coordinates of the 4 cube corners
    int A = perm[X] + Y;
    int B = perm[X + 1] + Y;

    // Blend the results
    float res = lerp(
        lerp(grad(perm[A], x, y), grad(perm[B], x - 1, y), u),
        lerp(grad(perm[A + 1], x, y - 1), grad(perm[B + 1], x - 1, y - 1), u),
        v
    );

    // Normalize to [0, 1]
    return (res + 1.0f) * 0.5f;
}

// Fractal Brownian Motion - layered noise for natural-looking detail
float fbm(float x, float y, int octaves, float persistence, float lacunarity) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; i++) {
        total += perlinNoise(x * frequency, y * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return total / maxValue;
}

// Voronoi/cellular noise for patterns like stone, scales, etc.
float voronoiNoise(float x, float y, float randomness) {
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));

    float minDist = 10.0f;

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int cx = xi + dx;
            int cy = yi + dy;

            // Generate pseudo-random point within this cell
            int hash = perm[(perm[cx & 255] + cy) & 255];
            float px = cx + (static_cast<float>(hash) / 255.0f) * randomness;
            float py = cy + (static_cast<float>(perm[hash]) / 255.0f) * randomness;

            float dist = std::sqrt((x - px) * (x - px) + (y - py) * (y - py));
            minDist = std::min(minDist, dist);
        }
    }

    return glm::clamp(minDist, 0.0f, 1.0f);
}

bool checkSbsrenderAvailable() {
    // Try to run sbsrender --version to check if it's available
#ifdef _WIN32
    int result = std::system("sbsrender --version >nul 2>&1");
#else
    int result = std::system("sbsrender --version >/dev/null 2>&1");
#endif
    return result == 0;
}

bool renderWithSbsrender(const RenderConfig& config) {
    SDL_Log("Rendering SBSAR with sbsrender: %s", config.inputPath.c_str());

    // Build the sbsrender command
    // sbsrender render <input.sbsar> --output-path <dir> --output-name <name>_{outputNodeName}
    //   --output-format png --set-value $outputsize@<log2(resolution)>,<log2(resolution)>

    int log2Res = 0;
    int res = config.resolution;
    while (res > 1) {
        res >>= 1;
        log2Res++;
    }

    std::string cmd = "sbsrender render \"" + config.inputPath + "\"";
    cmd += " --output-path \"" + config.outputDir + "\"";
    cmd += " --output-name \"" + config.outputName + "_{outputNodeName}\"";
    cmd += " --output-format png";
    cmd += " --set-value \"$outputsize@" + std::to_string(log2Res) + "," + std::to_string(log2Res) + "\"";

    if (config.verbose) {
        SDL_Log("Command: %s", cmd.c_str());
    }

    int result = std::system(cmd.c_str());

    if (result != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "sbsrender failed with exit code %d", result);
        return false;
    }

    SDL_Log("Successfully rendered SBSAR to %s", config.outputDir.c_str());
    return true;
}

// ============================================================================
// Procedural Texture Generators
// ============================================================================

// Generate basecolor texture with natural color variation
void generateBasecolorTexture(const std::string& path, int resolution,
                               const glm::vec4& baseColor) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    float scale = 8.0f;  // Controls pattern scale

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            float u = static_cast<float>(x) / resolution;
            float v = static_cast<float>(y) / resolution;

            // Multi-octave noise for natural variation
            float noise1 = fbm(u * scale, v * scale, 6, 0.5f, 2.0f);
            float noise2 = fbm(u * scale * 2.0f + 100.0f, v * scale * 2.0f, 4, 0.5f, 2.0f);
            float noise3 = voronoiNoise(u * scale * 0.5f, v * scale * 0.5f, 0.8f);

            // Combine noises for rich variation
            float variation = noise1 * 0.5f + noise2 * 0.3f + noise3 * 0.2f;

            // Apply variation to base color (subtle color shifts)
            float r = baseColor.r + (variation - 0.5f) * 0.3f;
            float g = baseColor.g + (variation - 0.5f) * 0.25f;
            float b = baseColor.b + (variation - 0.5f) * 0.2f;

            int idx = (y * resolution + x) * 4;
            imageData[idx + 0] = static_cast<unsigned char>(glm::clamp(r * 255.0f, 0.0f, 255.0f));
            imageData[idx + 1] = static_cast<unsigned char>(glm::clamp(g * 255.0f, 0.0f, 255.0f));
            imageData[idx + 2] = static_cast<unsigned char>(glm::clamp(b * 255.0f, 0.0f, 255.0f));
            imageData[idx + 3] = 255;
        }
    }

    unsigned error = lodepng::encode(path, imageData, resolution, resolution);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to write basecolor texture %s: %s",
                     path.c_str(), lodepng_error_text(error));
    } else {
        SDL_Log("Generated basecolor texture: %s", path.c_str());
    }
}

// Generate normal map from height data using Sobel filter
void generateNormalTexture(const std::string& path, int resolution) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    std::vector<float> heightData(resolution * resolution);
    float scale = 8.0f;
    float normalStrength = 2.0f;  // Controls bump intensity

    // First generate height data
    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            float u = static_cast<float>(x) / resolution;
            float v = static_cast<float>(y) / resolution;

            // Multi-scale noise for height
            float height = fbm(u * scale, v * scale, 6, 0.5f, 2.0f);
            height += voronoiNoise(u * scale * 0.5f, v * scale * 0.5f, 0.8f) * 0.3f;

            heightData[y * resolution + x] = height;
        }
    }

    // Convert height to normals using Sobel filter
    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            // Sample neighboring heights (with wrapping)
            auto getHeight = [&](int px, int py) {
                px = (px + resolution) % resolution;
                py = (py + resolution) % resolution;
                return heightData[py * resolution + px];
            };

            // Sobel filter for X gradient
            float dx = getHeight(x - 1, y - 1) * -1.0f + getHeight(x + 1, y - 1) * 1.0f
                     + getHeight(x - 1, y)     * -2.0f + getHeight(x + 1, y)     * 2.0f
                     + getHeight(x - 1, y + 1) * -1.0f + getHeight(x + 1, y + 1) * 1.0f;

            // Sobel filter for Y gradient
            float dy = getHeight(x - 1, y - 1) * -1.0f + getHeight(x, y - 1) * -2.0f + getHeight(x + 1, y - 1) * -1.0f
                     + getHeight(x - 1, y + 1) *  1.0f + getHeight(x, y + 1) *  2.0f + getHeight(x + 1, y + 1) *  1.0f;

            // Create normal vector
            glm::vec3 normal(-dx * normalStrength, -dy * normalStrength, 1.0f);
            normal = glm::normalize(normal);

            // Convert from [-1,1] to [0,1] range for storage
            int idx = (y * resolution + x) * 4;
            imageData[idx + 0] = static_cast<unsigned char>((normal.x * 0.5f + 0.5f) * 255.0f);
            imageData[idx + 1] = static_cast<unsigned char>((normal.y * 0.5f + 0.5f) * 255.0f);
            imageData[idx + 2] = static_cast<unsigned char>((normal.z * 0.5f + 0.5f) * 255.0f);
            imageData[idx + 3] = 255;
        }
    }

    unsigned error = lodepng::encode(path, imageData, resolution, resolution);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to write normal texture %s: %s",
                     path.c_str(), lodepng_error_text(error));
    } else {
        SDL_Log("Generated normal texture: %s", path.c_str());
    }
}

// Generate roughness map with variation
void generateRoughnessTexture(const std::string& path, int resolution,
                               float baseRoughness) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    float scale = 8.0f;

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            float u = static_cast<float>(x) / resolution;
            float v = static_cast<float>(y) / resolution;

            // Combine different noise types for interesting roughness variation
            float noise1 = fbm(u * scale, v * scale, 4, 0.5f, 2.0f);
            float noise2 = voronoiNoise(u * scale * 0.7f, v * scale * 0.7f, 0.9f);

            // Mix and apply to base roughness
            float variation = noise1 * 0.7f + noise2 * 0.3f;
            float roughness = baseRoughness + (variation - 0.5f) * 0.4f;
            roughness = glm::clamp(roughness, 0.0f, 1.0f);

            unsigned char val = static_cast<unsigned char>(roughness * 255.0f);
            int idx = (y * resolution + x) * 4;
            imageData[idx + 0] = val;
            imageData[idx + 1] = val;
            imageData[idx + 2] = val;
            imageData[idx + 3] = 255;
        }
    }

    unsigned error = lodepng::encode(path, imageData, resolution, resolution);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to write roughness texture %s: %s",
                     path.c_str(), lodepng_error_text(error));
    } else {
        SDL_Log("Generated roughness texture: %s", path.c_str());
    }
}

// Generate height/displacement map
void generateHeightTexture(const std::string& path, int resolution) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    float scale = 8.0f;

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            float u = static_cast<float>(x) / resolution;
            float v = static_cast<float>(y) / resolution;

            // Rich multi-octave noise for height detail
            float height = fbm(u * scale, v * scale, 6, 0.5f, 2.0f);

            // Add voronoi for stone-like cracks/cells
            float voronoi = voronoiNoise(u * scale * 0.5f, v * scale * 0.5f, 0.8f);
            height = height * 0.7f + voronoi * 0.3f;

            unsigned char val = static_cast<unsigned char>(glm::clamp(height * 255.0f, 0.0f, 255.0f));
            int idx = (y * resolution + x) * 4;
            imageData[idx + 0] = val;
            imageData[idx + 1] = val;
            imageData[idx + 2] = val;
            imageData[idx + 3] = 255;
        }
    }

    unsigned error = lodepng::encode(path, imageData, resolution, resolution);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to write height texture %s: %s",
                     path.c_str(), lodepng_error_text(error));
    } else {
        SDL_Log("Generated height texture: %s", path.c_str());
    }
}

// Generate ambient occlusion map
void generateAOTexture(const std::string& path, int resolution) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    float scale = 8.0f;

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            float u = static_cast<float>(x) / resolution;
            float v = static_cast<float>(y) / resolution;

            // AO is darkening in crevices - use inverted voronoi for crack darkness
            float voronoi = voronoiNoise(u * scale * 0.5f, v * scale * 0.5f, 0.8f);
            float noise = fbm(u * scale, v * scale, 4, 0.5f, 2.0f);

            // AO is mostly white with dark in crevices
            float ao = 0.7f + voronoi * 0.2f + noise * 0.1f;
            ao = glm::clamp(ao, 0.0f, 1.0f);

            unsigned char val = static_cast<unsigned char>(ao * 255.0f);
            int idx = (y * resolution + x) * 4;
            imageData[idx + 0] = val;
            imageData[idx + 1] = val;
            imageData[idx + 2] = val;
            imageData[idx + 3] = 255;
        }
    }

    unsigned error = lodepng::encode(path, imageData, resolution, resolution);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to write AO texture %s: %s",
                     path.c_str(), lodepng_error_text(error));
    } else {
        SDL_Log("Generated AO texture: %s", path.c_str());
    }
}

// Generate metallic map (mostly non-metallic with some spots)
void generateMetallicTexture(const std::string& path, int resolution,
                              float baseMetallic) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    float scale = 8.0f;

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            float u = static_cast<float>(x) / resolution;
            float v = static_cast<float>(y) / resolution;

            // For most materials, metallic is uniform or has subtle variation
            float noise = fbm(u * scale * 2.0f, v * scale * 2.0f, 3, 0.5f, 2.0f);
            float metallic = baseMetallic + (noise - 0.5f) * 0.1f;
            metallic = glm::clamp(metallic, 0.0f, 1.0f);

            unsigned char val = static_cast<unsigned char>(metallic * 255.0f);
            int idx = (y * resolution + x) * 4;
            imageData[idx + 0] = val;
            imageData[idx + 1] = val;
            imageData[idx + 2] = val;
            imageData[idx + 3] = 255;
        }
    }

    unsigned error = lodepng::encode(path, imageData, resolution, resolution);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to write metallic texture %s: %s",
                     path.c_str(), lodepng_error_text(error));
    } else {
        SDL_Log("Generated metallic texture: %s", path.c_str());
    }
}

// Generate emissive map (usually black for most materials)
void generateEmissiveTexture(const std::string& path, int resolution,
                              const glm::vec4& emissiveColor) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);

    // Emissive is usually flat - no emission for standard materials
    unsigned char r = static_cast<unsigned char>(glm::clamp(emissiveColor.r * 255.0f, 0.0f, 255.0f));
    unsigned char g = static_cast<unsigned char>(glm::clamp(emissiveColor.g * 255.0f, 0.0f, 255.0f));
    unsigned char b = static_cast<unsigned char>(glm::clamp(emissiveColor.b * 255.0f, 0.0f, 255.0f));

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            int idx = (y * resolution + x) * 4;
            imageData[idx + 0] = r;
            imageData[idx + 1] = g;
            imageData[idx + 2] = b;
            imageData[idx + 3] = 255;
        }
    }

    unsigned error = lodepng::encode(path, imageData, resolution, resolution);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to write emissive texture %s: %s",
                     path.c_str(), lodepng_error_text(error));
    } else {
        SDL_Log("Generated emissive texture: %s", path.c_str());
    }
}

bool generateFallbackTextures(const RenderConfig& config) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "sbsrender not available, generating procedural textures for: %s",
                config.inputPath.c_str());

    // Try to parse the .sbsar archive for material parameters
    MaterialParameters matParams = parseSbsarArchive(config.inputPath);

    // Initialize noise permutation table with seed from input filename
    unsigned int seed = 0;
    for (char c : config.outputName) {
        seed = seed * 31 + static_cast<unsigned int>(c);
    }
    initPermutationTable(seed);

    // Create output directory if it doesn't exist
    fs::create_directories(config.outputDir);

    // Use extracted parameters or defaults
    glm::vec4 baseColor = matParams.parsed ? matParams.baseColor :
                          STANDARD_OUTPUTS[0].fallbackColor;
    float roughness = matParams.parsed ? matParams.roughness : 0.5f;
    float metallic = matParams.parsed ? matParams.metallic : 0.0f;
    glm::vec4 emissiveColor = matParams.parsed ? matParams.emissiveColor :
                              STANDARD_OUTPUTS[6].fallbackColor;

    // Generate specialized procedural textures for each output type
    for (const auto& output : STANDARD_OUTPUTS) {
        std::string outputPath = config.outputDir + "/" + config.outputName +
                                 "_" + output.name + ".png";

        if (output.name == "basecolor") {
            generateBasecolorTexture(outputPath, config.resolution, baseColor);
        } else if (output.name == "normal") {
            generateNormalTexture(outputPath, config.resolution);
        } else if (output.name == "roughness") {
            generateRoughnessTexture(outputPath, config.resolution, roughness);
        } else if (output.name == "metallic") {
            generateMetallicTexture(outputPath, config.resolution, metallic);
        } else if (output.name == "height") {
            generateHeightTexture(outputPath, config.resolution);
        } else if (output.name == "ambientocclusion") {
            generateAOTexture(outputPath, config.resolution);
        } else if (output.name == "emissive") {
            generateEmissiveTexture(outputPath, config.resolution, emissiveColor);
        }
    }

    // Write a manifest file indicating procedural textures were generated
    std::string manifestPath = config.outputDir + "/" + config.outputName + "_manifest.txt";
    std::ofstream manifest(manifestPath);
    if (manifest.is_open()) {
        manifest << "# SBSAR Procedural Textures\n";
        manifest << "# Generated with procedural noise (Perlin + Voronoi FBM)\n";
        if (matParams.parsed) {
            manifest << "# Parameters extracted from SBSAR archive\n";
            if (!matParams.materialName.empty()) {
                manifest << "# Material: " << matParams.materialName << "\n";
            }
            if (!matParams.materialType.empty()) {
                manifest << "# Type: " << matParams.materialType << "\n";
            }
        }
        manifest << "# Install Adobe Substance Automation Toolkit for exact .sbsar rendering\n";
        manifest << "source=" << config.inputPath << "\n";
        manifest << "resolution=" << config.resolution << "\n";
        manifest << "fallback=true\n";
        manifest << "parsed=" << (matParams.parsed ? "true" : "false") << "\n";
        manifest << "basecolor=" << baseColor.r << "," << baseColor.g << "," << baseColor.b << "\n";
        manifest << "roughness=" << roughness << "\n";
        manifest << "metallic=" << metallic << "\n";
        for (const auto& output : STANDARD_OUTPUTS) {
            manifest << "output=" << config.outputName << "_" << output.name << ".png\n";
        }
        manifest.close();
    }

    return true;
}

void printUsage(const char* programName) {
    SDL_Log("Usage: %s <input.sbsar> <output_dir> [options]", programName);
    SDL_Log(" ");
    SDL_Log("Renders Substance Archive (.sbsar) files to PNG texture maps.");
    SDL_Log("Requires Adobe Substance Automation Toolkit (sbsrender) for full quality.");
    SDL_Log("Falls back to procedural textures with noise-based detail if sbsrender is not available.");
    SDL_Log(" ");
    SDL_Log("Options:");
    SDL_Log("  --name <name>        Output file name prefix (default: input filename)");
    SDL_Log("  --resolution <n>     Texture resolution (default: 1024)");
    SDL_Log("  --no-fallback        Don't generate fallback textures if sbsrender fails");
    SDL_Log("  --verbose            Enable verbose output");
    SDL_Log("  --help               Show this help");
    SDL_Log(" ");
    SDL_Log("Output files:");
    SDL_Log("  <name>_basecolor.png       - Albedo/diffuse color (sRGB)");
    SDL_Log("  <name>_normal.png          - Normal map (linear, tangent space)");
    SDL_Log("  <name>_roughness.png       - Roughness map (linear)");
    SDL_Log("  <name>_metallic.png        - Metallic map (linear)");
    SDL_Log("  <name>_height.png          - Height/displacement map (linear)");
    SDL_Log("  <name>_ambientocclusion.png - Ambient occlusion (linear)");
    SDL_Log("  <name>_emissive.png        - Emissive map (sRGB)");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    RenderConfig config;

    // Parse positional arguments
    config.inputPath = argv[1];
    config.outputDir = argv[2];

    // Default output name from input filename
    fs::path inputFile(config.inputPath);
    config.outputName = inputFile.stem().string();

    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--name" && i + 1 < argc) {
            config.outputName = argv[++i];
        } else if (arg == "--resolution" && i + 1 < argc) {
            config.resolution = std::stoi(argv[++i]);
        } else if (arg == "--no-fallback") {
            config.generateFallback = false;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown option: %s", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    // Validate input file exists
    if (!fs::exists(config.inputPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Input file not found: %s", config.inputPath.c_str());
        return 1;
    }

    // Validate resolution is power of 2
    if ((config.resolution & (config.resolution - 1)) != 0 || config.resolution < 32) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Resolution must be a power of 2 >= 32 (got %d)", config.resolution);
        return 1;
    }

    SDL_Log("SBSAR Renderer");
    SDL_Log("==============");
    SDL_Log("Input: %s", config.inputPath.c_str());
    SDL_Log("Output: %s/%s_*.png", config.outputDir.c_str(), config.outputName.c_str());
    SDL_Log("Resolution: %d x %d", config.resolution, config.resolution);

    // Check if sbsrender is available
    bool sbsrenderAvailable = checkSbsrenderAvailable();

    if (sbsrenderAvailable) {
        SDL_Log("sbsrender found, using Substance rendering");
        if (renderWithSbsrender(config)) {
            return 0;
        }
        // If sbsrender failed, fall back to placeholders
        if (config.generateFallback) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "sbsrender failed, falling back to placeholder textures");
            if (generateFallbackTextures(config)) {
                return 0;
            }
        }
        return 1;
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "sbsrender not found in PATH");
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Install Adobe Substance Automation Toolkit for proper SBSAR rendering");
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Download from: https://www.adobe.com/products/substance3d-designer.html");

        if (config.generateFallback) {
            if (generateFallbackTextures(config)) {
                return 0;
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "No fallback generation requested, aborting");
            return 1;
        }
    }

    return 0;
}
