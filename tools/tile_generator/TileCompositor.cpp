#include "TileCompositor.h"
#include <lodepng.h>
#include <nlohmann/json.hpp>
#include <SDL3/SDL_log.h>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include "../common/bc_compress.h"
#include "../common/dds_file.h"

namespace VirtualTexture {

// ============================================================================
// TextureData implementation
// ============================================================================

glm::vec4 TextureData::sample(glm::vec2 uv) const {
    if (!isValid()) return glm::vec4(1.0f, 0.0f, 1.0f, 1.0f); // Magenta for missing

    // Clamp UV to [0, 1]
    uv = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));

    // Convert to pixel coordinates
    float fx = uv.x * (width - 1);
    float fy = uv.y * (height - 1);

    uint32_t x0 = static_cast<uint32_t>(fx);
    uint32_t y0 = static_cast<uint32_t>(fy);
    uint32_t x1 = std::min(x0 + 1, width - 1);
    uint32_t y1 = std::min(y0 + 1, height - 1);

    float tx = fx - x0;
    float ty = fy - y0;

    // Sample four corners
    auto getPixel = [this](uint32_t x, uint32_t y) -> glm::vec4 {
        size_t idx = (y * width + x) * 4;
        return glm::vec4(
            pixels[idx + 0] / 255.0f,
            pixels[idx + 1] / 255.0f,
            pixels[idx + 2] / 255.0f,
            pixels[idx + 3] / 255.0f
        );
    };

    glm::vec4 c00 = getPixel(x0, y0);
    glm::vec4 c10 = getPixel(x1, y0);
    glm::vec4 c01 = getPixel(x0, y1);
    glm::vec4 c11 = getPixel(x1, y1);

    // Bilinear interpolation
    glm::vec4 c0 = glm::mix(c00, c10, tx);
    glm::vec4 c1 = glm::mix(c01, c11, tx);
    return glm::mix(c0, c1, ty);
}

glm::vec4 TextureData::sampleWrap(glm::vec2 uv) const {
    // Wrap UV coordinates
    uv.x = uv.x - std::floor(uv.x);
    uv.y = uv.y - std::floor(uv.y);
    return sample(uv);
}

// ============================================================================
// HeightmapData implementation
// ============================================================================

float HeightmapData::sampleHeight(float x, float z, float terrainSize) const {
    if (!isValid()) return 0.0f;

    // Convert world coords to UV
    float u = x / terrainSize;
    float v = z / terrainSize;

    // Clamp to valid range
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    // Convert to pixel coordinates
    float fx = u * (width - 1);
    float fy = v * (height - 1);

    uint32_t x0 = static_cast<uint32_t>(fx);
    uint32_t y0 = static_cast<uint32_t>(fy);
    uint32_t x1 = std::min(x0 + 1, width - 1);
    uint32_t y1 = std::min(y0 + 1, height - 1);

    float tx = fx - x0;
    float ty = fy - y0;

    // Bilinear interpolation
    float h00 = heights[y0 * width + x0];
    float h10 = heights[y0 * width + x1];
    float h01 = heights[y1 * width + x0];
    float h11 = heights[y1 * width + x1];

    float h0 = h00 + (h10 - h00) * tx;
    float h1 = h01 + (h11 - h01) * tx;
    return h0 + (h1 - h0) * ty;
}

float HeightmapData::sampleSlope(float x, float z, float terrainSize) const {
    if (!isValid()) return 0.0f;

    float step = terrainSize / width;
    float hL = sampleHeight(x - step, z, terrainSize);
    float hR = sampleHeight(x + step, z, terrainSize);
    float hD = sampleHeight(x, z - step, terrainSize);
    float hU = sampleHeight(x, z + step, terrainSize);

    float dx = (hR - hL) / (2.0f * step);
    float dz = (hU - hD) / (2.0f * step);

    return std::sqrt(dx * dx + dz * dz);
}

glm::vec3 HeightmapData::sampleNormal(float x, float z, float terrainSize) const {
    if (!isValid()) return glm::vec3(0.0f, 1.0f, 0.0f);

    float step = terrainSize / width;
    float hL = sampleHeight(x - step, z, terrainSize);
    float hR = sampleHeight(x + step, z, terrainSize);
    float hD = sampleHeight(x, z - step, terrainSize);
    float hU = sampleHeight(x, z + step, terrainSize);

    glm::vec3 normal(
        (hL - hR) / (2.0f * step),
        1.0f,
        (hD - hU) / (2.0f * step)
    );

    return glm::normalize(normal);
}

// ============================================================================
// BiomeMapData implementation
// ============================================================================

BiomeZone BiomeMapData::sampleZone(float x, float z, float terrainSize) const {
    if (!isValid()) return BiomeZone::Grassland;

    float u = std::clamp(x / terrainSize, 0.0f, 1.0f);
    float v = std::clamp(z / terrainSize, 0.0f, 1.0f);

    uint32_t px = static_cast<uint32_t>(u * (width - 1));
    uint32_t py = static_cast<uint32_t>(v * (height - 1));

    uint8_t zone = zones[py * width + px];
    return static_cast<BiomeZone>(std::min(zone, static_cast<uint8_t>(8)));
}

uint8_t BiomeMapData::sampleSubZone(float x, float z, float terrainSize) const {
    if (subZones.empty()) return 0;

    float u = std::clamp(x / terrainSize, 0.0f, 1.0f);
    float v = std::clamp(z / terrainSize, 0.0f, 1.0f);

    uint32_t px = static_cast<uint32_t>(u * (width - 1));
    uint32_t py = static_cast<uint32_t>(v * (height - 1));

    return subZones[py * width + px] % 4;
}

// ============================================================================
// MaterialTextureCache implementation
// ============================================================================

const TextureData* MaterialTextureCache::getTexture(const std::string& path) {
    // Check if already cached
    auto it = textures.find(path);
    if (it != textures.end()) {
        return it->second.isValid() ? &it->second : nullptr;
    }

    // Try to load the texture
    TextureData& tex = textures[path];

    std::vector<unsigned char> image;
    unsigned w, h;
    unsigned error = lodepng::decode(image, w, h, path);

    if (error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load texture %s: %s",
                    path.c_str(), lodepng_error_text(error));
        return nullptr;
    }

    tex.pixels = std::move(image);
    tex.width = w;
    tex.height = h;

    return &tex;
}

// ============================================================================
// OutputTile implementation
// ============================================================================

void OutputTile::setPixel(uint32_t x, uint32_t y, glm::vec4 color) {
    if (x >= resolution || y >= resolution) return;

    size_t idx = pixelIndex(x, y);
    pixels[idx + 0] = static_cast<uint8_t>(std::clamp(color.r * 255.0f, 0.0f, 255.0f));
    pixels[idx + 1] = static_cast<uint8_t>(std::clamp(color.g * 255.0f, 0.0f, 255.0f));
    pixels[idx + 2] = static_cast<uint8_t>(std::clamp(color.b * 255.0f, 0.0f, 255.0f));
    pixels[idx + 3] = static_cast<uint8_t>(std::clamp(color.a * 255.0f, 0.0f, 255.0f));
}

glm::vec4 OutputTile::getPixel(uint32_t x, uint32_t y) const {
    if (x >= resolution || y >= resolution) return glm::vec4(0.0f);

    size_t idx = pixelIndex(x, y);
    return glm::vec4(
        pixels[idx + 0] / 255.0f,
        pixels[idx + 1] / 255.0f,
        pixels[idx + 2] / 255.0f,
        pixels[idx + 3] / 255.0f
    );
}

// ============================================================================
// TileCompositor implementation
// ============================================================================

TileCompositor::TileCompositor() = default;

void TileCompositor::init(const TileCompositorConfig& cfg) {
    config = cfg;

    // Initialize spline rasterizer with matching config
    SplineRasterizerConfig splineConfig;
    splineConfig.terrainSize = config.terrainSize;
    splineConfig.tileResolution = config.tileResolution;
    splineConfig.tilesPerAxis = config.tilesPerAxis;
    splineRasterizer.init(splineConfig);
}

bool TileCompositor::loadHeightmap(const std::string& path) {
    std::vector<unsigned char> image;
    unsigned w, h;
    unsigned error = lodepng::decode(image, w, h, path, LCT_GREY, 16);

    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load heightmap %s: %s",
                     path.c_str(), lodepng_error_text(error));
        return false;
    }

    heightmap.width = w;
    heightmap.height = h;
    heightmap.heights.resize(w * h);

    // Convert 16-bit grayscale to normalized floats
    for (size_t i = 0; i < w * h; ++i) {
        uint16_t val = (static_cast<uint16_t>(image[i * 2]) << 8) | image[i * 2 + 1];
        heightmap.heights[i] = val / 65535.0f;
    }

    SDL_Log("Loaded heightmap %s (%ux%u)", path.c_str(), w, h);
    return true;
}

bool TileCompositor::loadBiomeMap(const std::string& path) {
    std::vector<unsigned char> image;
    unsigned w, h;
    unsigned error = lodepng::decode(image, w, h, path, LCT_GREY, 8);

    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load biome map %s: %s",
                     path.c_str(), lodepng_error_text(error));
        return false;
    }

    biomeMap.width = w;
    biomeMap.height = h;
    biomeMap.zones = std::move(image);

    // Generate sub-zone variation using simple hash
    biomeMap.subZones.resize(w * h);
    for (size_t i = 0; i < w * h; ++i) {
        uint32_t x = i % w;
        uint32_t y = i / w;
        // Simple spatial hash for sub-zone variation
        uint32_t hash = (x * 73856093u) ^ (y * 19349663u);
        biomeMap.subZones[i] = static_cast<uint8_t>(hash % 4);
    }

    SDL_Log("Loaded biome map %s (%ux%u)", path.c_str(), w, h);
    return true;
}

bool TileCompositor::loadRoads(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not open roads file: %s", jsonPath.c_str());
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        std::vector<RoadGen::RoadSpline> roads;

        if (j.contains("roads") && j["roads"].is_array()) {
            for (const auto& road : j["roads"]) {
                RoadGen::RoadSpline spline;

                if (road.contains("type")) {
                    spline.type = static_cast<RoadGen::RoadType>(road["type"].get<int>());
                }

                if (road.contains("points") && road["points"].is_array()) {
                    for (const auto& pt : road["points"]) {
                        RoadGen::RoadControlPoint cp;
                        cp.position = glm::vec2(pt["x"].get<float>(), pt["z"].get<float>());
                        cp.widthOverride = pt.value("width", 0.0f); // 0 means use default
                        spline.controlPoints.push_back(cp);
                    }
                }

                if (!spline.controlPoints.empty()) {
                    roads.push_back(std::move(spline));
                }
            }
        }

        splineRasterizer.setRoads(roads);
        SDL_Log("Loaded %zu roads from %s", roads.size(), jsonPath.c_str());
        return true;

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error parsing roads JSON: %s", e.what());
        return false;
    }
}

bool TileCompositor::loadRivers(const std::string& erosionCachePath) {
    // Rivers would come from erosion simulation cache
    // For now, just log that we'd load them
    SDL_Log("River loading from erosion cache not yet implemented: %s", erosionCachePath.c_str());
    return true;
}

void TileCompositor::setMaterialBasePath(const std::string& path) {
    materialBasePath = path;
    // MaterialLibrary uses its internal base path, but we store the path here
    // for direct texture loading in TileCompositor
}

float TileCompositor::smoothstep(float edge0, float edge1, float x) {
    x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

float TileCompositor::noise2D(glm::vec2 pos) const {
    // Simple value noise
    auto hash = [](int x, int y) -> float {
        int n = x + y * 57;
        n = (n << 13) ^ n;
        return (1.0f - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f);
    };

    int xi = static_cast<int>(std::floor(pos.x));
    int yi = static_cast<int>(std::floor(pos.y));
    float xf = pos.x - xi;
    float yf = pos.y - yi;

    // Smooth interpolation
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);

    float n00 = hash(xi, yi);
    float n10 = hash(xi + 1, yi);
    float n01 = hash(xi, yi + 1);
    float n11 = hash(xi + 1, yi + 1);

    float n0 = n00 + (n10 - n00) * u;
    float n1 = n01 + (n11 - n01) * u;
    return n0 + (n1 - n0) * v;
}

glm::vec4 TileCompositor::sampleMaterialTriplanar(const TerrainMaterial& material,
                                                   glm::vec2 worldPos, glm::vec3 normal) {
    // Get the texture - construct full path from base path
    std::string fullPath = materialBasePath + "/" + material.albedoPath;
    const TextureData* tex = textureCache.getTexture(fullPath);

    if (!tex || !tex->isValid()) {
        // Return a debug color based on material name
        return glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    }

    // Calculate triplanar blend weights from normal
    glm::vec3 blend = glm::abs(normal);
    blend = glm::max(blend - glm::vec3(0.2f), glm::vec3(0.0f));
    blend = blend * blend * blend; // Sharpen
    blend /= (blend.x + blend.y + blend.z);

    // For now, mostly use Y projection (top-down) since we're making terrain
    // Full triplanar would sample XZ, XY, YZ planes
    float scale = material.tilingScale * config.materialTilingScale;
    glm::vec2 uvY = worldPos * scale;

    // Sample with the Y-projection UV (top-down view)
    glm::vec4 colorY = tex->sampleWrap(uvY);

    // For steep surfaces, we'd blend with side projections
    // Simplified: just use Y projection weighted by normal.y
    float yWeight = std::max(0.5f, blend.y);
    return colorY * yWeight + glm::vec4(0.4f, 0.35f, 0.3f, 1.0f) * (1.0f - yWeight);
}

glm::vec4 TileCompositor::sampleBaseTerrain(glm::vec2 worldPos) {
    // Sample biome and height info
    BiomeZone zone = biomeMap.sampleZone(worldPos.x, worldPos.y, config.terrainSize);
    uint8_t subZone = biomeMap.sampleSubZone(worldPos.x, worldPos.y, config.terrainSize);

    float slope = heightmap.sampleSlope(worldPos.x, worldPos.y, config.terrainSize);
    glm::vec3 normal = heightmap.sampleNormal(worldPos.x, worldPos.y, config.terrainSize);

    // Get materials for this biome (use const reference instead of pointer)
    const TerrainMaterial& baseMat = materials.getSubZoneMaterial(zone, subZone);
    const TerrainMaterial& cliffMat = materials.getCliffMaterial();

    // Sample base material
    glm::vec4 baseColor = sampleMaterialTriplanar(baseMat, worldPos, normal);

    // Blend with cliff material on steep slopes
    if (slope > config.slopeThreshold) {
        glm::vec4 cliffColor = sampleMaterialTriplanar(cliffMat, worldPos, normal);
        float blendFactor = smoothstep(config.slopeThreshold,
                                        config.slopeThreshold + config.slopeBlendRange,
                                        slope);
        baseColor = glm::mix(baseColor, cliffColor, blendFactor);
    }

    // Add sub-zone noise variation
    float noise = noise2D(worldPos * config.subZoneNoiseScale);
    float variation = noise * config.subZoneBlendStrength;
    baseColor.r = std::clamp(baseColor.r + variation * 0.1f, 0.0f, 1.0f);
    baseColor.g = std::clamp(baseColor.g + variation * 0.1f, 0.0f, 1.0f);
    baseColor.b = std::clamp(baseColor.b + variation * 0.05f, 0.0f, 1.0f);

    return baseColor;
}

void TileCompositor::generateTile(uint32_t tileX, uint32_t tileY, uint32_t mipLevel, OutputTile& outTile) {
    // Calculate effective tile size at this mip level
    uint32_t tilesAtMip = config.getTilesAtMip(mipLevel);
    float tileSize = config.terrainSize / tilesAtMip;

    // Setup output tile
    outTile.tileX = tileX;
    outTile.tileY = tileY;
    outTile.mipLevel = mipLevel;
    outTile.resolution = config.tileResolution;
    outTile.pixels.resize(config.tileResolution * config.tileResolution * 4);

    // Calculate tile world bounds
    float worldMinX = tileX * tileSize;
    float worldMinZ = tileY * tileSize;

    // Get rasterized spline data for this tile (at mip 0 coordinates)
    RasterizedTile splineTile;
    uint32_t mip0TileX = tileX << mipLevel;
    uint32_t mip0TileY = tileY << mipLevel;

    // For higher mip levels, we need to handle multiple mip0 tiles
    // For simplicity, just check if we have roads at this location
    bool hasRoads = false;
    if (mipLevel == 0) {
        splineRasterizer.rasterizeTile(tileX, tileY, splineTile);
        hasRoads = splineTile.hasRoads();
    }

    // Generate each pixel
    for (uint32_t py = 0; py < config.tileResolution; ++py) {
        for (uint32_t px = 0; px < config.tileResolution; ++px) {
            // Calculate world position for this pixel
            float u = (px + 0.5f) / config.tileResolution;
            float v = (py + 0.5f) / config.tileResolution;
            glm::vec2 worldPos(worldMinX + u * tileSize, worldMinZ + v * tileSize);

            // Sample base terrain
            glm::vec4 color = sampleBaseTerrain(worldPos);

            // Composite road layer (only at mip 0 for now)
            if (hasRoads && mipLevel == 0) {
                float roadMask = splineTile.sampleRoadMask(px, py);
                if (roadMask > 0.0f) {
                    glm::vec2 roadUV = splineTile.sampleRoadUV(px, py);
                    RoadGen::RoadType srcRoadType = splineTile.sampleRoadType(px, py);

                    // Convert from RoadGen::RoadType to VirtualTexture::RoadType
                    RoadType roadType = static_cast<RoadType>(static_cast<uint8_t>(srcRoadType));

                    const RoadMaterial& roadMat = materials.getRoadMaterial(roadType);
                    std::string roadTexPath = materialBasePath + "/" + roadMat.albedoPath;
                    const TextureData* roadTex = textureCache.getTexture(roadTexPath);
                    if (roadTex && roadTex->isValid()) {
                        glm::vec4 roadColor = roadTex->sampleWrap(roadUV);
                        color = glm::mix(color, roadColor, roadMask);
                    } else {
                        // Fallback road color
                        glm::vec4 roadColor(0.3f, 0.3f, 0.35f, 1.0f);
                        color = glm::mix(color, roadColor, roadMask);
                    }
                }
            }

            // Composite riverbed layer
            if (mipLevel == 0 && splineTile.hasRiverbeds()) {
                float riverbedMask = splineTile.sampleRiverbedMask(px, py);
                if (riverbedMask > 0.0f) {
                    glm::vec2 riverbedUV = splineTile.sampleRiverbedUV(px, py);

                    const RiverbedMaterial& riverbedMat = materials.getRiverbedMaterial();
                    std::string riverbedTexPath = materialBasePath + "/" + riverbedMat.centerAlbedoPath;
                    const TextureData* riverbedTex = textureCache.getTexture(riverbedTexPath);
                    if (riverbedTex && riverbedTex->isValid()) {
                        glm::vec4 riverbedColor = riverbedTex->sampleWrap(riverbedUV);
                        color = glm::mix(color, riverbedColor, riverbedMask);
                    } else {
                        // Fallback riverbed color
                        glm::vec4 riverbedColor(0.4f, 0.35f, 0.3f, 1.0f);
                        color = glm::mix(color, riverbedColor, riverbedMask);
                    }
                }
            }

            outTile.setPixel(px, py, color);
        }
    }
}

bool TileCompositor::generateMipLevel(uint32_t mipLevel, const std::string& outputDir,
                                       ProgressCallback callback) {
    uint32_t tilesAtMip = config.getTilesAtMip(mipLevel);
    uint32_t totalTiles = tilesAtMip * tilesAtMip;
    uint32_t processedTiles = 0;

    // Create mip directory
    std::string mipDir = outputDir + "/mip" + std::to_string(mipLevel);
    std::filesystem::create_directories(mipDir);

    OutputTile tile;
    std::string extension = config.useCompression ? ".dds" : ".png";

    for (uint32_t ty = 0; ty < tilesAtMip; ++ty) {
        for (uint32_t tx = 0; tx < tilesAtMip; ++tx) {
            // Generate tile
            generateTile(tx, ty, mipLevel, tile);

            // Save tile
            std::string filename = mipDir + "/tile_" + std::to_string(tx) + "_" + std::to_string(ty) + extension;

            if (config.useCompression) {
                // Compress to BC1 and save as DDS
                BCCompress::CompressedImage compressed = BCCompress::compressImage(
                    tile.pixels.data(), tile.resolution, tile.resolution, BCCompress::BCFormat::BC1);

                if (!DDS::write(filename, tile.resolution, tile.resolution, DDS::Format::BC1_SRGB,
                                compressed.data.data(), static_cast<uint32_t>(compressed.data.size()))) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save tile %s", filename.c_str());
                    return false;
                }
            } else {
                // Save as PNG
                unsigned error = lodepng::encode(filename, tile.pixels, tile.resolution, tile.resolution);
                if (error) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save tile %s: %s",
                                 filename.c_str(), lodepng_error_text(error));
                    return false;
                }
            }

            processedTiles++;

            if (callback) {
                float progress = static_cast<float>(processedTiles) / totalTiles;
                callback(progress, "Generating mip " + std::to_string(mipLevel) +
                         " (" + std::to_string(processedTiles) + "/" + std::to_string(totalTiles) + ")");
            }
        }
    }

    SDL_Log("Generated mip level %u: %u tiles (%s)", mipLevel, totalTiles,
            config.useCompression ? "BC1 DDS" : "PNG");
    return true;
}

bool TileCompositor::generateAllMips(const std::string& outputDir, ProgressCallback callback) {
    std::filesystem::create_directories(outputDir);

    for (uint32_t mip = 0; mip < config.maxMipLevels; ++mip) {
        if (callback) {
            callback(static_cast<float>(mip) / config.maxMipLevels,
                     "Starting mip level " + std::to_string(mip));
        }

        if (!generateMipLevel(mip, outputDir, callback)) {
            return false;
        }
    }

    // Save metadata
    saveMetadata(outputDir);

    return true;
}

bool TileCompositor::saveMetadata(const std::string& outputDir) const {
    nlohmann::json metadata;

    metadata["version"] = 1;
    metadata["terrainSize"] = config.terrainSize;
    metadata["tileResolution"] = config.tileResolution;
    metadata["tilesPerAxis"] = config.tilesPerAxis;
    metadata["maxMipLevels"] = config.maxMipLevels;
    metadata["minAltitude"] = config.minAltitude;
    metadata["maxAltitude"] = config.maxAltitude;

    // Mip level info
    nlohmann::json mips = nlohmann::json::array();
    for (uint32_t mip = 0; mip < config.maxMipLevels; ++mip) {
        uint32_t tilesAtMip = config.getTilesAtMip(mip);
        mips.push_back({
            {"level", mip},
            {"tilesPerAxis", tilesAtMip},
            {"totalTiles", tilesAtMip * tilesAtMip},
            {"directory", "mip" + std::to_string(mip)}
        });
    }
    metadata["mipLevels"] = mips;

    std::string metadataPath = outputDir + "/metadata.json";
    std::ofstream file(metadataPath);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save metadata to %s", metadataPath.c_str());
        return false;
    }

    file << metadata.dump(2);
    SDL_Log("Saved virtual texture metadata to %s", metadataPath.c_str());
    return true;
}

} // namespace VirtualTexture
