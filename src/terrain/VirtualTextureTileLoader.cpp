#include "VirtualTextureTileLoader.h"
#include "../core/DDSLoader.h"
#include <SDL3/SDL_log.h>
#include <lodepng.h>
#include <algorithm>
#include <filesystem>

namespace VirtualTexture {

std::unique_ptr<VirtualTextureTileLoader> VirtualTextureTileLoader::create(const std::string& basePath, uint32_t workerCount) {
    std::unique_ptr<VirtualTextureTileLoader> loader(new VirtualTextureTileLoader());
    if (!loader->initInternal(basePath, workerCount)) {
        return nullptr;
    }
    return loader;
}

VirtualTextureTileLoader::~VirtualTextureTileLoader() {
    cleanup();
}

bool VirtualTextureTileLoader::initInternal(const std::string& path, uint32_t workerCount) {
    basePath = path;
    running = true;

    // Create worker threads
    workers.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        workers.emplace_back(&VirtualTextureTileLoader::workerLoop, this);
    }

    SDL_Log("VirtualTextureTileLoader initialized: %u workers, path: %s",
            workerCount, basePath.c_str());
    return true;
}

void VirtualTextureTileLoader::cleanup() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        running = false;
    }
    queueCondition.notify_all();

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers.clear();

    // Clear remaining data
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!requestQueue.empty()) requestQueue.pop();
        queuedTiles.clear();
    }

    {
        std::lock_guard<std::mutex> lock(loadedMutex);
        loadedTiles.clear();
    }
}

void VirtualTextureTileLoader::queueTile(TileId id, int priority) {
    uint32_t packed = id.pack();

    std::lock_guard<std::mutex> lock(queueMutex);

    // Don't queue duplicates
    if (queuedTiles.find(packed) != queuedTiles.end()) {
        return;
    }

    LoadRequest request;
    request.id = id;
    request.priority = priority;

    requestQueue.push(request);
    queuedTiles.insert(packed);

    queueCondition.notify_one();
}

void VirtualTextureTileLoader::queueTiles(const std::vector<TileId>& ids, int priority) {
    std::lock_guard<std::mutex> lock(queueMutex);

    for (const auto& id : ids) {
        uint32_t packed = id.pack();
        if (queuedTiles.find(packed) != queuedTiles.end()) {
            continue;
        }

        LoadRequest request;
        request.id = id;
        request.priority = priority;

        requestQueue.push(request);
        queuedTiles.insert(packed);
    }

    queueCondition.notify_all();
}

bool VirtualTextureTileLoader::isQueued(TileId id) const {
    std::lock_guard<std::mutex> lock(queueMutex);
    return queuedTiles.find(id.pack()) != queuedTiles.end();
}

void VirtualTextureTileLoader::cancelTile(TileId id) {
    std::lock_guard<std::mutex> lock(queueMutex);
    queuedTiles.erase(id.pack());
    // Note: tile stays in priority queue but will be skipped when processed
}

void VirtualTextureTileLoader::clearQueue() {
    std::lock_guard<std::mutex> lock(queueMutex);
    while (!requestQueue.empty()) requestQueue.pop();
    queuedTiles.clear();
}

std::vector<LoadedTile> VirtualTextureTileLoader::getLoadedTiles() {
    std::lock_guard<std::mutex> lock(loadedMutex);
    std::vector<LoadedTile> result = std::move(loadedTiles);
    loadedTiles.clear();
    return result;
}

void VirtualTextureTileLoader::setLoadedCallback(TileLoadedCallback callback) {
    loadedCallback = std::move(callback);
}

uint32_t VirtualTextureTileLoader::getPendingCount() const {
    std::lock_guard<std::mutex> lock(queueMutex);
    return static_cast<uint32_t>(requestQueue.size());
}

uint32_t VirtualTextureTileLoader::getLoadedCount() const {
    std::lock_guard<std::mutex> lock(loadedMutex);
    return static_cast<uint32_t>(loadedTiles.size());
}

void VirtualTextureTileLoader::workerLoop() {
    while (true) {
        LoadRequest request;

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            // Wait for work or shutdown
            queueCondition.wait(lock, [this] {
                return !running || !requestQueue.empty();
            });

            if (!running && requestQueue.empty()) {
                return;
            }

            if (requestQueue.empty()) {
                continue;
            }

            request = requestQueue.top();
            requestQueue.pop();

            // Check if this tile was cancelled
            uint32_t packed = request.id.pack();
            if (queuedTiles.find(packed) == queuedTiles.end()) {
                continue; // Tile was cancelled, skip it
            }
            queuedTiles.erase(packed);
        }

        // Load the tile (outside of lock)
        LoadedTile tile;
        if (loadTileFromDisk(request.id, tile)) {
            totalBytesLoaded += tile.pixels.size();

            // Add to loaded list
            {
                std::lock_guard<std::mutex> lock(loadedMutex);
                loadedTiles.push_back(std::move(tile));
            }

            // Invoke callback if set
            if (loadedCallback) {
                loadedCallback(tile);
            }
        }
    }
}

bool VirtualTextureTileLoader::loadTileFromDisk(TileId id, LoadedTile& tile) {
    // Try loading DDS first (compressed format), then fall back to PNG
    std::string ddsPath = getTilePath(id, true);
    std::string pngPath = getTilePath(id, false);

    // Try DDS first
    if (std::filesystem::exists(ddsPath)) {
        DDSLoader::Image dds = DDSLoader::load(ddsPath);
        if (dds.isValid()) {
            tile.id = id;
            tile.width = dds.width;
            tile.height = dds.height;
            tile.pixels = std::move(dds.data);

            // Map DDS format to our tile format
            switch (dds.format) {
                case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
                    tile.format = TileFormat::BC1;
                    break;
                case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
                    tile.format = TileFormat::BC1_SRGB;
                    break;
                case VK_FORMAT_BC4_UNORM_BLOCK:
                    tile.format = TileFormat::BC4;
                    break;
                case VK_FORMAT_BC5_UNORM_BLOCK:
                    tile.format = TileFormat::BC5;
                    break;
                case VK_FORMAT_BC7_UNORM_BLOCK:
                    tile.format = TileFormat::BC7;
                    break;
                case VK_FORMAT_BC7_SRGB_BLOCK:
                    tile.format = TileFormat::BC7_SRGB;
                    break;
                default:
                    tile.format = TileFormat::BC1_SRGB; // Default assumption
                    break;
            }

            return true;
        }
    }

    // Try PNG
    std::vector<unsigned char> png;
    unsigned width, height;

    unsigned error = lodepng::decode(png, width, height, pngPath);
    if (error == 0) {
        tile.id = id;
        tile.width = width;
        tile.height = height;
        tile.pixels = std::move(png);
        tile.format = TileFormat::RGBA8;
        return true;
    }

    // Neither format found - use a fallback/placeholder
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "Could not load tile (tried %s and %s)", ddsPath.c_str(), pngPath.c_str());

    // Create a placeholder tile (pink/magenta checkerboard)
    tile.id = id;
    tile.width = 128;
    tile.height = 128;
    tile.format = TileFormat::RGBA8;
    tile.pixels.resize(tile.width * tile.height * 4);

    for (uint32_t y = 0; y < tile.height; ++y) {
        for (uint32_t x = 0; x < tile.width; ++x) {
            size_t idx = (y * tile.width + x) * 4;
            bool checker = ((x / 16) + (y / 16)) % 2 == 0;
            tile.pixels[idx + 0] = checker ? 255 : 128;  // R
            tile.pixels[idx + 1] = checker ? 0 : 0;      // G
            tile.pixels[idx + 2] = checker ? 255 : 128;  // B
            tile.pixels[idx + 3] = 255;                   // A
        }
    }
    return true;
}

std::string VirtualTextureTileLoader::getTilePath(TileId id, bool dds) const {
    // Format: basePath/mip{level}/tile_{x}_{y}.{dds|png}
    char path[512];
    snprintf(path, sizeof(path), "%s/mip%u/tile_%u_%u.%s",
             basePath.c_str(), id.mipLevel, id.x, id.y,
             dds ? "dds" : "png");
    return std::string(path);
}

} // namespace VirtualTexture
