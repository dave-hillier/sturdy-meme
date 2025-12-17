// SBSAR file renderer
// Processes Substance Archive (.sbsar) files to generate texture maps
// Uses Adobe's sbsrender CLI tool if available, otherwise generates fallback textures
// with procedural noise-based detail
//
// .sbsar files are 7-zip archives containing:
// - XML metadata describing inputs, outputs, and presets (e.g., MaterialName.xml)
// - .sbsasm binary compiled substance graph files
// Reference: https://blog.jdboyd.net/2018/09/substance-designer-sbsprs-sbsar-file-format-notes/
//
// Uses libarchive for 7-zip extraction (no external 7z command required)

#include <SDL3/SDL_log.h>
#include <archive.h>
#include <archive_entry.h>
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>
#include <lodepng.h>
#include <miniz.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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

// Helper to detect material type from a string (name/label)
std::string detectMaterialType(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Check for specific material keywords in priority order
    // More specific matches first, generic ones last
    if (lower.find("grass") != std::string::npos) return "grass";
    if (lower.find("sand") != std::string::npos) return "sand";
    if (lower.find("dirt") != std::string::npos ||
        lower.find("ground") != std::string::npos ||
        lower.find("soil") != std::string::npos) return "ground";
    if (lower.find("stone") != std::string::npos ||
        lower.find("rock") != std::string::npos) return "stone";
    if (lower.find("brick") != std::string::npos) return "brick";
    if (lower.find("marble") != std::string::npos) return "marble";
    if (lower.find("concrete") != std::string::npos) return "concrete";
    if (lower.find("wood") != std::string::npos ||
        lower.find("bark") != std::string::npos ||
        lower.find("plank") != std::string::npos) return "wood";
    if (lower.find("steel") != std::string::npos ||
        lower.find("iron") != std::string::npos) return "metal";
    // Check "metal" last to avoid matching "metallic" attribute
    if (lower.find("metal ") != std::string::npos ||
        lower.find("metal_") != std::string::npos ||
        lower.find(" metal") != std::string::npos ||
        lower == "metal") return "metal";
    if (lower.find("fabric") != std::string::npos ||
        lower.find("cloth") != std::string::npos) return "fabric";
    if (lower.find("leather") != std::string::npos) return "leather";
    if (lower.find("plastic") != std::string::npos) return "plastic";
    return "";
}

// Parse material parameters from XML content
MaterialParameters parseXmlParameters(const std::string& xml) {
    MaterialParameters params;

    // Extract material name/label - prefer graph label, then package label
    params.materialName = extractXmlAttribute(xml, "graph", "label");
    if (params.materialName.empty()) {
        params.materialName = extractXmlAttribute(xml, "package", "label");
    }

    // Try to determine material type from the material name first (most reliable)
    params.materialType = detectMaterialType(params.materialName);

    // If not found in name, try the package identifier
    if (params.materialType.empty()) {
        std::string pkgId = extractXmlAttribute(xml, "package", "identifier");
        params.materialType = detectMaterialType(pkgId);
    }

    // If still not found, try graph identifier
    if (params.materialType.empty()) {
        std::string graphId = extractXmlAttribute(xml, "graph", "identifier");
        params.materialType = detectMaterialType(graphId);
    }

    // Set default parameters based on detected material type
    if (params.materialType == "stone" || params.materialType == "rock") {
        params.roughness = 0.7f;
        params.patternScale = 4.0f;
    } else if (params.materialType == "wood") {
        params.roughness = 0.6f;
        params.patternScale = 6.0f;
        params.baseColor = glm::vec4(0.4f, 0.25f, 0.15f, 1.0f);
    } else if (params.materialType == "metal") {
        params.metallic = 0.9f;
        params.roughness = 0.3f;
        params.baseColor = glm::vec4(0.7f, 0.7f, 0.75f, 1.0f);
    } else if (params.materialType == "fabric" || params.materialType == "leather") {
        params.roughness = 0.8f;
        params.patternScale = 12.0f;
    } else if (params.materialType == "ground" || params.materialType == "sand") {
        params.roughness = 0.9f;
        params.baseColor = glm::vec4(0.6f, 0.5f, 0.4f, 1.0f);
    } else if (params.materialType == "grass") {
        params.roughness = 0.7f;
        params.baseColor = glm::vec4(0.3f, 0.5f, 0.2f, 1.0f);
    } else if (params.materialType == "brick") {
        params.roughness = 0.75f;
        params.patternScale = 4.0f;
    } else if (params.materialType == "concrete") {
        params.roughness = 0.85f;
        params.patternScale = 6.0f;
    } else if (params.materialType == "marble") {
        params.roughness = 0.3f;
        params.patternScale = 3.0f;
    } else if (params.materialType == "plastic") {
        params.roughness = 0.4f;
        params.patternScale = 10.0f;
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

// Extract XML content from 7-zip archive using libarchive
std::string extractLibarchive7zXmlContent(const std::string& path) {
    std::string xmlContent;

    struct archive* a = archive_read_new();
    if (!a) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create archive reader");
        return xmlContent;
    }

    // Enable all formats (including 7z) and all filters
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    int r = archive_read_open_filename(a, path.c_str(), 10240);
    if (r != ARCHIVE_OK) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to open archive: %s", archive_error_string(a));
        archive_read_free(a);
        return xmlContent;
    }

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* entryPath = archive_entry_pathname(entry);
        if (!entryPath) {
            archive_read_data_skip(a);
            continue;
        }

        std::string filename = entryPath;
        SDL_Log("  Archive entry: %s", filename.c_str());

        // Look for XML files
        if (filename.find(".xml") != std::string::npos) {
            SDL_Log("Found XML file in archive: %s", filename.c_str());

            // Get the uncompressed size
            la_int64_t size = archive_entry_size(entry);
            if (size > 0) {
                std::vector<char> buffer(static_cast<size_t>(size) + 1);
                la_ssize_t bytesRead = archive_read_data(a, buffer.data(), static_cast<size_t>(size));
                if (bytesRead > 0) {
                    buffer[static_cast<size_t>(bytesRead)] = '\0';
                    xmlContent = buffer.data();
                    SDL_Log("Extracted XML from 7z archive (%zu bytes)", xmlContent.size());
                    break;  // Found XML, stop searching
                }
            } else {
                // Size unknown, read in chunks
                std::stringstream ss;
                std::array<char, 8192> chunkBuffer;
                la_ssize_t bytesRead;
                while ((bytesRead = archive_read_data(a, chunkBuffer.data(), chunkBuffer.size())) > 0) {
                    ss.write(chunkBuffer.data(), bytesRead);
                }
                xmlContent = ss.str();
                if (!xmlContent.empty()) {
                    SDL_Log("Extracted XML from 7z archive (%zu bytes)", xmlContent.size());
                    break;
                }
            }
        } else {
            archive_read_data_skip(a);
        }
    }

    archive_read_close(a);
    archive_read_free(a);

    return xmlContent;
}

// Try to parse .sbsar as regular ZIP archive (fallback/test files)
std::string extractZipXmlContent(const std::string& path) {
    std::string xmlContent;

    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        return xmlContent;
    }

    int numFiles = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    SDL_Log("ZIP archive contains %d files", numFiles);

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

            size_t uncompSize = static_cast<size_t>(fileStat.m_uncomp_size);
            std::vector<char> buffer(uncompSize + 1);

            if (mz_zip_reader_extract_to_mem(&zip, i, buffer.data(), uncompSize, 0)) {
                buffer[uncompSize] = '\0';
                xmlContent = buffer.data();
                SDL_Log("  Extracted XML content (%zu bytes)", xmlContent.size());
                break;
            }
        }
    }

    mz_zip_reader_end(&zip);
    return xmlContent;
}

// Extract and parse .sbsar archive to get material parameters
MaterialParameters parseSbsarArchive(const std::string& path) {
    MaterialParameters params;

    SDL_Log("Parsing SBSAR archive: %s", path.c_str());

    std::string xmlContent;

    // Try 7-zip format using libarchive (real .sbsar files are 7z archives)
    SDL_Log("Trying 7-zip format with libarchive...");
    xmlContent = extractLibarchive7zXmlContent(path);

    // Fall back to regular ZIP using miniz (for test files or older formats)
    if (xmlContent.empty()) {
        SDL_Log("Trying ZIP format with miniz...");
        xmlContent = extractZipXmlContent(path);
    }

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
// Procedural Noise Generation using glm::simplex
// ============================================================================

// Fractal Brownian Motion - layered simplex noise for natural-looking detail
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

// Turbulence - absolute value noise for crack-like patterns
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

// Worley/cellular noise for stone, pebbles, gravel patterns
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

// ============================================================================
// Material-Specific Color Functions
// ============================================================================

glm::vec3 stoneColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 light(0.55f, 0.52f, 0.48f);
    glm::vec3 dark(0.3f, 0.28f, 0.25f);
    float cracks = turbulence(uv * 4.0f, 4);
    float voronoi = worley(uv, 8.0f);
    float blend = glm::clamp(noise * 0.5f + 0.5f - cracks * 0.3f + voronoi * 0.2f, 0.0f, 1.0f);
    return glm::mix(dark, light, blend);
}

glm::vec3 woodColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightWood(0.6f, 0.45f, 0.25f);
    glm::vec3 darkWood(0.3f, 0.2f, 0.1f);
    // Wood grain pattern - elongated in one direction
    float grain = std::sin(uv.y * 50.0f + noise * 8.0f) * 0.5f + 0.5f;
    float rings = std::sin((uv.x + uv.y) * 20.0f + noise * 5.0f) * 0.3f;
    float blend = glm::clamp(grain * 0.6f + noise * 0.3f + rings + 0.2f, 0.0f, 1.0f);
    return glm::mix(darkWood, lightWood, blend);
}

glm::vec3 metalColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 light(0.75f, 0.75f, 0.8f);
    glm::vec3 dark(0.5f, 0.5f, 0.55f);
    // Brushed metal pattern - subtle directional scratches
    float scratches = std::sin(uv.x * 100.0f + noise * 3.0f) * 0.1f;
    float blend = glm::clamp(noise * 0.3f + 0.5f + scratches + detail * 0.1f, 0.0f, 1.0f);
    return glm::mix(dark, light, blend);
}

glm::vec3 fabricColor(glm::vec2 uv, float noise, float detail, const glm::vec3& baseColor) {
    glm::vec3 light = baseColor * 1.2f;
    glm::vec3 dark = baseColor * 0.7f;
    // Woven pattern
    float warp = std::sin(uv.x * 80.0f) * 0.5f + 0.5f;
    float weft = std::sin(uv.y * 80.0f) * 0.5f + 0.5f;
    float weave = warp * weft * 0.5f + (1.0f - warp) * (1.0f - weft) * 0.5f;
    float blend = glm::clamp(weave + noise * 0.3f + detail * 0.15f, 0.0f, 1.0f);
    return glm::clamp(glm::mix(dark, light, blend), 0.0f, 1.0f);
}

glm::vec3 groundColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 light(0.65f, 0.55f, 0.4f);
    glm::vec3 dark(0.35f, 0.28f, 0.18f);
    float pebbles = worley(uv, 12.0f);
    float blend = glm::clamp(noise * 0.5f + 0.5f + pebbles * 0.3f, 0.0f, 1.0f);
    return glm::mix(dark, light, blend);
}

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

glm::vec3 concreteColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 light(0.7f, 0.68f, 0.65f);
    glm::vec3 dark(0.45f, 0.43f, 0.4f);
    float spots = worley(uv, 15.0f);
    float blend = glm::clamp(noise * 0.4f + 0.5f + spots * 0.2f + detail * 0.1f, 0.0f, 1.0f);
    return glm::mix(dark, light, blend);
}

glm::vec3 brickColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 lightBrick(0.7f, 0.35f, 0.25f);
    glm::vec3 darkBrick(0.45f, 0.2f, 0.15f);
    glm::vec3 mortar(0.75f, 0.72f, 0.68f);

    // Brick pattern with offset rows
    float brickWidth = 0.25f;
    float brickHeight = 0.125f;
    float mortarWidth = 0.02f;

    float row = std::floor(uv.y / brickHeight);
    float offset = std::fmod(row, 2.0f) * 0.5f * brickWidth;
    float brickX = std::fmod(uv.x + offset, brickWidth);
    float brickY = std::fmod(uv.y, brickHeight);

    // Check if in mortar
    if (brickX < mortarWidth || brickY < mortarWidth) {
        return mortar + glm::vec3(noise * 0.1f);
    }

    float blend = glm::clamp(noise * 0.5f + 0.5f + detail * 0.2f, 0.0f, 1.0f);
    return glm::mix(darkBrick, lightBrick, blend);
}

glm::vec3 leatherColor(glm::vec2 uv, float noise, float detail, const glm::vec3& baseColor) {
    glm::vec3 light = baseColor * 1.15f;
    glm::vec3 dark = baseColor * 0.75f;
    // Leather grain pattern
    float grain = worley(uv, 25.0f);
    float blend = glm::clamp(noise * 0.4f + 0.5f + grain * 0.3f, 0.0f, 1.0f);
    return glm::clamp(glm::mix(dark, light, blend), 0.0f, 1.0f);
}

glm::vec3 marbleColor(glm::vec2 uv, float noise, float detail) {
    glm::vec3 white(0.95f, 0.93f, 0.9f);
    glm::vec3 gray(0.6f, 0.58f, 0.55f);
    glm::vec3 dark(0.3f, 0.28f, 0.25f);

    // Marble veins
    float veins = std::sin(uv.x * 10.0f + noise * 8.0f + turbulence(uv * 3.0f, 4) * 4.0f);
    veins = std::pow(std::abs(veins), 0.4f);

    float blend = glm::clamp(noise * 0.3f + 0.6f + detail * 0.1f, 0.0f, 1.0f);
    glm::vec3 base = glm::mix(gray, white, blend);
    return glm::mix(base, dark, veins * 0.4f);
}

glm::vec3 plasticColor(glm::vec2 uv, float noise, float detail, const glm::vec3& baseColor) {
    // Plastic is mostly uniform with very subtle variation
    return baseColor + glm::vec3((noise - 0.5f) * 0.05f);
}

// Get material-specific color based on material type
glm::vec3 getMaterialColor(const std::string& materialType, glm::vec2 uv,
                           float noise, float detail, const glm::vec4& baseColor) {
    if (materialType == "stone" || materialType == "rock") {
        return stoneColor(uv, noise, detail);
    } else if (materialType == "wood" || materialType == "bark") {
        return woodColor(uv, noise, detail);
    } else if (materialType == "metal" || materialType == "steel" || materialType == "iron") {
        return metalColor(uv, noise, detail);
    } else if (materialType == "fabric" || materialType == "cloth") {
        return fabricColor(uv, noise, detail, glm::vec3(baseColor));
    } else if (materialType == "leather") {
        return leatherColor(uv, noise, detail, glm::vec3(baseColor));
    } else if (materialType == "ground" || materialType == "dirt" || materialType == "soil") {
        return groundColor(uv, noise, detail);
    } else if (materialType == "grass") {
        return grassColor(uv, noise, detail);
    } else if (materialType == "sand") {
        return sandColor(uv, noise, detail);
    } else if (materialType == "concrete") {
        return concreteColor(uv, noise, detail);
    } else if (materialType == "brick") {
        return brickColor(uv, noise, detail);
    } else if (materialType == "marble") {
        return marbleColor(uv, noise, detail);
    } else if (materialType == "plastic") {
        return plasticColor(uv, noise, detail, glm::vec3(baseColor));
    } else {
        // Default: use base color with noise variation
        glm::vec3 color(baseColor);
        float variation = noise * 0.5f + 0.5f;
        return color * (0.8f + variation * 0.4f);
    }
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

// Generate basecolor texture using material-specific color function
void generateBasecolorTexture(const std::string& path, int resolution,
                               const MaterialParameters& params) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    float scale = params.patternScale;

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            glm::vec2 uv(static_cast<float>(x) / resolution,
                         static_cast<float>(y) / resolution);
            glm::vec2 noisePos = uv * scale;

            // Multi-octave noise for natural variation
            float noise = fbm(noisePos, params.patternOctaves);
            float detail = turbulence(noisePos * 2.0f, 3);

            // Use material-specific color function
            glm::vec3 color = getMaterialColor(params.materialType, uv, noise, detail, params.baseColor);

            int idx = (y * resolution + x) * 4;
            imageData[idx + 0] = static_cast<unsigned char>(glm::clamp(color.r * 255.0f, 0.0f, 255.0f));
            imageData[idx + 1] = static_cast<unsigned char>(glm::clamp(color.g * 255.0f, 0.0f, 255.0f));
            imageData[idx + 2] = static_cast<unsigned char>(glm::clamp(color.b * 255.0f, 0.0f, 255.0f));
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
void generateNormalTexture(const std::string& path, int resolution,
                            const MaterialParameters& params) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    std::vector<float> heightData(resolution * resolution);
    float scale = params.patternScale;
    float normalStrength = params.normalIntensity * 2.0f;  // Controls bump intensity

    // First generate height data using material-appropriate noise
    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            glm::vec2 uv(static_cast<float>(x) / resolution,
                         static_cast<float>(y) / resolution);
            glm::vec2 noisePos = uv * scale;

            // Multi-scale noise for height - material-specific patterns
            float height = fbm(noisePos, params.patternOctaves);

            // Add material-specific height detail
            if (params.materialType == "stone" || params.materialType == "rock" ||
                params.materialType == "brick" || params.materialType == "concrete") {
                // Add voronoi cracks for stone-like materials
                height += worley(uv, scale * 0.5f) * 0.3f;
                height += turbulence(noisePos * 2.0f, 3) * 0.2f;
            } else if (params.materialType == "wood" || params.materialType == "bark") {
                // Wood grain pattern
                height += std::sin(uv.y * 50.0f + height * 8.0f) * 0.15f;
            } else if (params.materialType == "fabric" || params.materialType == "cloth") {
                // Woven texture
                float warp = std::sin(uv.x * 80.0f) * 0.5f + 0.5f;
                float weft = std::sin(uv.y * 80.0f) * 0.5f + 0.5f;
                height += (warp * weft) * 0.2f;
            } else if (params.materialType == "ground" || params.materialType == "grass" ||
                       params.materialType == "sand") {
                // Natural terrain noise
                height += worley(uv, scale * 0.8f) * 0.15f;
            }

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

// Generate roughness map with material-specific variation
void generateRoughnessTexture(const std::string& path, int resolution,
                               const MaterialParameters& params) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    float scale = params.patternScale;
    float baseRoughness = params.roughness;

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            glm::vec2 uv(static_cast<float>(x) / resolution,
                         static_cast<float>(y) / resolution);
            glm::vec2 noisePos = uv * scale;

            // Combine different noise types for interesting roughness variation
            float noise = fbm(noisePos, 4);
            float voronoi = worley(uv, scale * 0.7f);

            // Material-specific roughness variation
            float variation;
            if (params.materialType == "metal" || params.materialType == "steel") {
                // Metal: mostly uniform with slight scratches
                variation = noise * 0.15f;
            } else if (params.materialType == "stone" || params.materialType == "rock") {
                // Stone: larger variation with cracks being rougher
                float cracks = turbulence(noisePos * 2.0f, 3);
                variation = noise * 0.25f + cracks * 0.15f + voronoi * 0.1f;
            } else if (params.materialType == "wood") {
                // Wood: grain affects roughness
                float grain = std::sin(uv.y * 50.0f + noise * 8.0f) * 0.5f + 0.5f;
                variation = noise * 0.15f + grain * 0.1f;
            } else if (params.materialType == "fabric" || params.materialType == "cloth") {
                // Fabric: woven pattern affects roughness
                float warp = std::sin(uv.x * 80.0f) * 0.5f + 0.5f;
                float weft = std::sin(uv.y * 80.0f) * 0.5f + 0.5f;
                variation = noise * 0.1f + (warp * weft) * 0.15f;
            } else {
                // Default variation
                variation = noise * 0.5f + voronoi * 0.2f - 0.35f;
            }

            float roughness = glm::clamp(baseRoughness + variation, 0.0f, 1.0f);

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

// Generate height/displacement map with material-specific detail
void generateHeightTexture(const std::string& path, int resolution,
                            const MaterialParameters& params) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    float scale = params.patternScale;
    float heightScale = params.heightScale;

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            glm::vec2 uv(static_cast<float>(x) / resolution,
                         static_cast<float>(y) / resolution);
            glm::vec2 noisePos = uv * scale;

            // Base height from multi-octave noise
            float height = fbm(noisePos, params.patternOctaves);

            // Material-specific height detail
            if (params.materialType == "stone" || params.materialType == "rock" ||
                params.materialType == "concrete") {
                // Add voronoi for cracks/cells
                float voronoi = worley(uv, scale * 0.5f);
                float cracks = turbulence(noisePos * 2.0f, 3);
                height = height * 0.5f + voronoi * 0.3f + cracks * 0.2f;
            } else if (params.materialType == "brick") {
                // Brick pattern height
                float brickWidth = 0.25f;
                float brickHeight = 0.125f;
                float mortarWidth = 0.02f;
                float row = std::floor(uv.y / brickHeight);
                float offset = std::fmod(row, 2.0f) * 0.5f * brickWidth;
                float brickX = std::fmod(uv.x + offset, brickWidth);
                float brickY = std::fmod(uv.y, brickHeight);

                // Mortar is lower
                if (brickX < mortarWidth || brickY < mortarWidth) {
                    height = 0.3f + height * 0.1f;
                } else {
                    height = 0.6f + height * 0.3f;
                }
            } else if (params.materialType == "wood" || params.materialType == "bark") {
                // Wood grain height
                float grain = std::sin(uv.y * 50.0f + height * 8.0f) * 0.5f + 0.5f;
                height = height * 0.6f + grain * 0.4f;
            } else if (params.materialType == "fabric" || params.materialType == "cloth") {
                // Woven pattern height
                float warp = std::sin(uv.x * 80.0f) * 0.5f + 0.5f;
                float weft = std::sin(uv.y * 80.0f) * 0.5f + 0.5f;
                float weave = warp * weft;
                height = 0.5f + height * 0.2f + weave * 0.3f;
            } else if (params.materialType == "metal" || params.materialType == "steel") {
                // Metal: mostly flat with subtle scratches
                float scratches = std::sin(uv.x * 100.0f + height * 3.0f) * 0.05f;
                height = 0.5f + height * 0.1f + scratches;
            } else {
                // Default: just add some voronoi detail
                float voronoi = worley(uv, scale * 0.6f);
                height = height * 0.7f + voronoi * 0.3f;
            }

            // Apply height scale and normalize to [0, 1]
            height = glm::clamp((height * 0.5f + 0.5f) * heightScale + (1.0f - heightScale) * 0.5f, 0.0f, 1.0f);

            unsigned char val = static_cast<unsigned char>(height * 255.0f);
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

// Generate ambient occlusion map with material-specific patterns
void generateAOTexture(const std::string& path, int resolution,
                        const MaterialParameters& params) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    float scale = params.patternScale;

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            glm::vec2 uv(static_cast<float>(x) / resolution,
                         static_cast<float>(y) / resolution);
            glm::vec2 noisePos = uv * scale;

            float noise = fbm(noisePos, 4);
            float voronoi = worley(uv, scale * 0.5f);

            // Material-specific AO
            float ao;
            if (params.materialType == "brick") {
                // Brick: mortar areas are occluded
                float brickWidth = 0.25f;
                float brickHeight = 0.125f;
                float mortarWidth = 0.02f;
                float row = std::floor(uv.y / brickHeight);
                float offset = std::fmod(row, 2.0f) * 0.5f * brickWidth;
                float brickX = std::fmod(uv.x + offset, brickWidth);
                float brickY = std::fmod(uv.y, brickHeight);

                if (brickX < mortarWidth || brickY < mortarWidth) {
                    ao = 0.6f + noise * 0.1f;  // Mortar is darker
                } else {
                    ao = 0.85f + noise * 0.1f;  // Brick surface is lighter
                }
            } else if (params.materialType == "stone" || params.materialType == "rock" ||
                       params.materialType == "concrete") {
                // Stone: cracks/crevices are occluded
                float cracks = turbulence(noisePos * 2.0f, 3);
                ao = 0.7f + voronoi * 0.2f - cracks * 0.15f + noise * 0.1f;
            } else if (params.materialType == "wood") {
                // Wood: grain affects AO slightly
                float grain = std::sin(uv.y * 50.0f + noise * 8.0f) * 0.5f + 0.5f;
                ao = 0.8f + noise * 0.1f + grain * 0.05f;
            } else if (params.materialType == "fabric" || params.materialType == "cloth") {
                // Fabric: woven pattern creates subtle AO
                float warp = std::sin(uv.x * 80.0f) * 0.5f + 0.5f;
                float weft = std::sin(uv.y * 80.0f) * 0.5f + 0.5f;
                float weave = warp * weft;
                ao = 0.85f + weave * 0.1f + noise * 0.05f;
            } else if (params.materialType == "metal" || params.materialType == "steel") {
                // Metal: mostly uniform AO
                ao = 0.9f + noise * 0.05f;
            } else {
                // Default: use voronoi for crevice darkness
                ao = 0.7f + voronoi * 0.2f + noise * 0.1f;
            }

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

// Generate metallic map with material-specific patterns
void generateMetallicTexture(const std::string& path, int resolution,
                              const MaterialParameters& params) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);
    float scale = params.patternScale;
    float baseMetallic = params.metallic;

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            glm::vec2 uv(static_cast<float>(x) / resolution,
                         static_cast<float>(y) / resolution);
            glm::vec2 noisePos = uv * scale * 2.0f;

            float noise = fbm(noisePos, 3);

            // Material-specific metallic variation
            float metallic;
            if (params.materialType == "metal" || params.materialType == "steel" ||
                params.materialType == "iron") {
                // Metal: high metallic with subtle variation
                metallic = baseMetallic + noise * 0.1f;
            } else if (params.materialType == "stone" || params.materialType == "rock") {
                // Stone: occasional metallic flecks (like mica/pyrite)
                float spots = worley(uv, 20.0f);
                metallic = spots < 0.15f ? 0.3f + noise * 0.2f : 0.0f;
            } else {
                // Most materials: uniform non-metallic with subtle variation
                metallic = baseMetallic + (noise - 0.5f) * 0.05f;
            }

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
                "sbsrender not available, generating material-specific procedural textures for: %s",
                config.inputPath.c_str());

    // Try to parse the .sbsar archive for material parameters
    MaterialParameters matParams = parseSbsarArchive(config.inputPath);

    // Create output directory if it doesn't exist
    fs::create_directories(config.outputDir);

    // Log material type detection
    if (matParams.parsed) {
        SDL_Log("Material type detected: %s",
                matParams.materialType.empty() ? "generic" : matParams.materialType.c_str());
        SDL_Log("Base parameters - color: (%.2f, %.2f, %.2f), roughness: %.2f, metallic: %.2f",
                matParams.baseColor.r, matParams.baseColor.g, matParams.baseColor.b,
                matParams.roughness, matParams.metallic);
    }

    // Generate specialized procedural textures for each output type
    for (const auto& output : STANDARD_OUTPUTS) {
        std::string outputPath = config.outputDir + "/" + config.outputName +
                                 "_" + output.name + ".png";

        if (output.name == "basecolor") {
            generateBasecolorTexture(outputPath, config.resolution, matParams);
        } else if (output.name == "normal") {
            generateNormalTexture(outputPath, config.resolution, matParams);
        } else if (output.name == "roughness") {
            generateRoughnessTexture(outputPath, config.resolution, matParams);
        } else if (output.name == "metallic") {
            generateMetallicTexture(outputPath, config.resolution, matParams);
        } else if (output.name == "height") {
            generateHeightTexture(outputPath, config.resolution, matParams);
        } else if (output.name == "ambientocclusion") {
            generateAOTexture(outputPath, config.resolution, matParams);
        } else if (output.name == "emissive") {
            generateEmissiveTexture(outputPath, config.resolution, matParams.emissiveColor);
        }
    }

    // Write a manifest file with full material information
    std::string manifestPath = config.outputDir + "/" + config.outputName + "_manifest.txt";
    std::ofstream manifest(manifestPath);
    if (manifest.is_open()) {
        manifest << "# SBSAR Material-Specific Procedural Textures\n";
        manifest << "# Generated using GLM simplex noise with material-aware patterns\n";
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
        manifest << "# Download: https://www.adobe.com/products/substance3d-designer.html\n";
        manifest << "#\n";
        manifest << "source=" << config.inputPath << "\n";
        manifest << "resolution=" << config.resolution << "\n";
        manifest << "fallback=true\n";
        manifest << "parsed=" << (matParams.parsed ? "true" : "false") << "\n";
        manifest << "materialType=" << matParams.materialType << "\n";
        manifest << "basecolor=" << matParams.baseColor.r << "," << matParams.baseColor.g << "," << matParams.baseColor.b << "\n";
        manifest << "roughness=" << matParams.roughness << "\n";
        manifest << "metallic=" << matParams.metallic << "\n";
        manifest << "patternScale=" << matParams.patternScale << "\n";
        manifest << "patternOctaves=" << matParams.patternOctaves << "\n";
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
