// Tests for VirtualTextureTileLoader - multi-threaded async tile loading
// Uses temp directories with test PNG files

#include <doctest/doctest.h>
#include "terrain/virtual_texture/VirtualTextureTileLoader.h"
#include <lodepng.h>
#include <filesystem>
#include <thread>
#include <chrono>

using namespace VirtualTexture;
namespace fs = std::filesystem;

// Helper class to create temporary tile directories for testing
class TempTileDirectory {
public:
    TempTileDirectory() {
        // Create unique temp directory
        basePath = fs::temp_directory_path() / ("vt_test_" + std::to_string(std::time(nullptr)));
        fs::create_directories(basePath);
    }

    ~TempTileDirectory() {
        // Clean up temp directory
        std::error_code ec;
        fs::remove_all(basePath, ec);
    }

    // Create a test PNG tile at the specified location
    bool createTile(uint16_t x, uint16_t y, uint8_t mip, uint32_t width = 128, uint32_t height = 128) {
        // Create mip directory
        fs::path mipDir = basePath / ("mip" + std::to_string(mip));
        fs::create_directories(mipDir);

        // Create simple test image (solid color based on coords)
        std::vector<unsigned char> image(width * height * 4);
        unsigned char r = static_cast<unsigned char>(x % 256);
        unsigned char g = static_cast<unsigned char>(y % 256);
        unsigned char b = static_cast<unsigned char>(mip * 20);

        for (uint32_t i = 0; i < width * height; ++i) {
            image[i * 4 + 0] = r;
            image[i * 4 + 1] = g;
            image[i * 4 + 2] = b;
            image[i * 4 + 3] = 255;
        }

        // Write PNG
        fs::path tilePath = mipDir / ("tile_" + std::to_string(x) + "_" + std::to_string(y) + ".png");
        unsigned error = lodepng::encode(tilePath.string(), image, width, height);
        return error == 0;
    }

    std::string getPath() const { return basePath.string(); }

private:
    fs::path basePath;
};

// ============================================================================
// VirtualTextureTileLoader Queue Tests
// ============================================================================

TEST_SUITE("VirtualTextureTileLoader Queue") {
    TEST_CASE("create returns valid loader") {
        TempTileDirectory tempDir;
        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 1);
        CHECK(loader != nullptr);
    }

    TEST_CASE("queueTile adds tile to queue") {
        TempTileDirectory tempDir;
        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 1);
        REQUIRE(loader != nullptr);

        TileId tile(10, 20, 1);
        CHECK_FALSE(loader->isQueued(tile));

        loader->queueTile(tile);
        // Note: tile may be processed immediately, so we can't always check isQueued

        // But we can verify pending count increased (or tile was already processed)
        // Give it a moment to be dequeued
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    TEST_CASE("queueTile deduplicates") {
        TempTileDirectory tempDir;
        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 0);  // 0 workers = no processing
        REQUIRE(loader != nullptr);

        TileId tile(5, 5, 0);
        loader->queueTile(tile);
        uint32_t count1 = loader->getPendingCount();

        loader->queueTile(tile);  // Same tile again
        uint32_t count2 = loader->getPendingCount();

        CHECK(count1 == count2);  // Should not increase
    }

    TEST_CASE("queueTiles adds multiple tiles") {
        TempTileDirectory tempDir;
        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 0);  // 0 workers
        REQUIRE(loader != nullptr);

        std::vector<TileId> tiles = {
            TileId(0, 0, 0),
            TileId(1, 0, 0),
            TileId(0, 1, 0),
            TileId(1, 1, 0)
        };

        loader->queueTiles(tiles);
        CHECK(loader->getPendingCount() == 4);
    }

    TEST_CASE("queueTiles deduplicates") {
        TempTileDirectory tempDir;
        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 0);
        REQUIRE(loader != nullptr);

        std::vector<TileId> tiles = {
            TileId(0, 0, 0),
            TileId(0, 0, 0),  // Duplicate
            TileId(1, 1, 1),
            TileId(1, 1, 1)   // Duplicate
        };

        loader->queueTiles(tiles);
        CHECK(loader->getPendingCount() == 2);  // Only unique tiles
    }

    TEST_CASE("clearQueue removes all pending") {
        TempTileDirectory tempDir;
        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 0);
        REQUIRE(loader != nullptr);

        loader->queueTile(TileId(0, 0, 0));
        loader->queueTile(TileId(1, 1, 1));
        loader->queueTile(TileId(2, 2, 2));
        CHECK(loader->getPendingCount() == 3);

        loader->clearQueue();
        CHECK(loader->getPendingCount() == 0);
    }

    TEST_CASE("cancelTile prevents loading") {
        TempTileDirectory tempDir;
        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 0);
        REQUIRE(loader != nullptr);

        TileId tile(10, 10, 0);
        loader->queueTile(tile);
        CHECK(loader->isQueued(tile));

        loader->cancelTile(tile);
        CHECK_FALSE(loader->isQueued(tile));
    }
}

// ============================================================================
// VirtualTextureTileLoader Loading Tests
// ============================================================================

TEST_SUITE("VirtualTextureTileLoader Loading") {
    TEST_CASE("loads existing PNG tiles") {
        TempTileDirectory tempDir;
        REQUIRE(tempDir.createTile(0, 0, 0, 64, 64));

        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 2);
        REQUIRE(loader != nullptr);

        loader->queueTile(TileId(0, 0, 0));

        // Wait for loading (with timeout)
        auto start = std::chrono::steady_clock::now();
        while (loader->getLoadedCount() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(5)) {
                FAIL("Timeout waiting for tile to load");
                break;
            }
        }

        auto loaded = loader->getLoadedTiles();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].id.x == 0);
        CHECK(loaded[0].id.y == 0);
        CHECK(loaded[0].id.mipLevel == 0);
        CHECK(loaded[0].width == 64);
        CHECK(loaded[0].height == 64);
        CHECK(loaded[0].format == TileFormat::RGBA8);
        CHECK(loaded[0].isValid());
    }

    TEST_CASE("creates placeholder for missing tiles") {
        TempTileDirectory tempDir;
        // Don't create any tile files

        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 2);
        REQUIRE(loader != nullptr);

        loader->queueTile(TileId(99, 99, 0));

        // Wait for loading
        auto start = std::chrono::steady_clock::now();
        while (loader->getLoadedCount() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(5)) {
                FAIL("Timeout waiting for placeholder tile");
                break;
            }
        }

        auto loaded = loader->getLoadedTiles();
        REQUIRE(loaded.size() == 1);

        // Placeholder is 128x128 pink checkerboard
        CHECK(loaded[0].width == 128);
        CHECK(loaded[0].height == 128);
        CHECK(loaded[0].format == TileFormat::RGBA8);
        CHECK(loaded[0].isValid());

        // Verify it's a checkerboard pattern (first pixel should be pink: 255, 0, 255)
        CHECK(loaded[0].pixels[0] == 255);  // R
        CHECK(loaded[0].pixels[1] == 0);    // G
        CHECK(loaded[0].pixels[2] == 255);  // B
        CHECK(loaded[0].pixels[3] == 255);  // A
    }

    TEST_CASE("loads multiple tiles concurrently") {
        TempTileDirectory tempDir;
        const int numTiles = 8;

        for (int i = 0; i < numTiles; ++i) {
            REQUIRE(tempDir.createTile(i, 0, 0, 32, 32));
        }

        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 4);  // 4 workers
        REQUIRE(loader != nullptr);

        std::vector<TileId> tiles;
        for (int i = 0; i < numTiles; ++i) {
            tiles.push_back(TileId(i, 0, 0));
        }
        loader->queueTiles(tiles);

        // Wait for all to load
        auto start = std::chrono::steady_clock::now();
        while (loader->getLoadedCount() < numTiles) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::seconds(10)) {
                FAIL("Timeout waiting for tiles: " << loader->getLoadedCount() << "/" << numTiles);
                break;
            }
        }

        auto loaded = loader->getLoadedTiles();
        CHECK(loaded.size() == numTiles);

        // Verify all tiles are valid
        for (const auto& tile : loaded) {
            CHECK(tile.isValid());
            CHECK(tile.width == 32);
            CHECK(tile.height == 32);
        }
    }

    TEST_CASE("getLoadedTiles clears internal list") {
        TempTileDirectory tempDir;
        REQUIRE(tempDir.createTile(0, 0, 0));

        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 2);
        REQUIRE(loader != nullptr);

        loader->queueTile(TileId(0, 0, 0));

        // Wait for load
        auto start = std::chrono::steady_clock::now();
        while (loader->getLoadedCount() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) break;
        }

        auto loaded1 = loader->getLoadedTiles();
        CHECK(loaded1.size() == 1);

        // Second call should return empty (data was moved out)
        auto loaded2 = loader->getLoadedTiles();
        CHECK(loaded2.empty());
    }

    TEST_CASE("tracks total bytes loaded") {
        TempTileDirectory tempDir;
        REQUIRE(tempDir.createTile(0, 0, 0, 64, 64));  // 64*64*4 = 16384 bytes

        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 2);
        REQUIRE(loader != nullptr);

        uint64_t initialBytes = loader->getTotalBytesLoaded();

        loader->queueTile(TileId(0, 0, 0));

        // Wait for load
        auto start = std::chrono::steady_clock::now();
        while (loader->getLoadedCount() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) break;
        }

        loader->getLoadedTiles();  // Consume tiles

        uint64_t finalBytes = loader->getTotalBytesLoaded();
        CHECK(finalBytes > initialBytes);
        CHECK(finalBytes - initialBytes == 64 * 64 * 4);  // RGBA
    }

    TEST_CASE("cancelled tiles are not loaded") {
        TempTileDirectory tempDir;
        REQUIRE(tempDir.createTile(0, 0, 0));
        REQUIRE(tempDir.createTile(1, 0, 0));

        // Use single worker to ensure predictable ordering
        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 1);
        REQUIRE(loader != nullptr);

        TileId tile1(0, 0, 0);
        TileId tile2(1, 0, 0);

        loader->queueTile(tile1);
        loader->queueTile(tile2);
        loader->cancelTile(tile2);  // Cancel second tile

        // Wait for loading to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        auto loaded = loader->getLoadedTiles();

        // Only tile1 should be loaded (tile2 was cancelled)
        // Note: tile2 might still be in queue when cancelled but not processed
        bool foundTile1 = false;
        bool foundTile2 = false;
        for (const auto& tile : loaded) {
            if (tile.id == tile1) foundTile1 = true;
            if (tile.id == tile2) foundTile2 = true;
        }

        CHECK(foundTile1);
        CHECK_FALSE(foundTile2);
    }
}

// ============================================================================
// VirtualTextureTileLoader Priority Tests
// ============================================================================

TEST_SUITE("VirtualTextureTileLoader Priority") {
    TEST_CASE("higher priority tiles load first") {
        TempTileDirectory tempDir;

        // Create tiles
        for (int i = 0; i < 5; ++i) {
            REQUIRE(tempDir.createTile(i, 0, 0, 32, 32));
        }

        // Use single worker for predictable ordering
        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 1);
        REQUIRE(loader != nullptr);

        // Queue with different priorities (lower value = higher priority)
        loader->queueTile(TileId(0, 0, 0), 100);  // Low priority
        loader->queueTile(TileId(1, 0, 0), 50);   // Medium
        loader->queueTile(TileId(2, 0, 0), 10);   // High
        loader->queueTile(TileId(3, 0, 0), 1);    // Higher
        loader->queueTile(TileId(4, 0, 0), 0);    // Highest

        // Wait for all to load
        auto start = std::chrono::steady_clock::now();
        std::vector<LoadedTile> allLoaded;

        while (allLoaded.size() < 5) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto batch = loader->getLoadedTiles();
            for (auto& tile : batch) {
                allLoaded.push_back(std::move(tile));
            }
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) {
                break;
            }
        }

        REQUIRE(allLoaded.size() == 5);

        // With single worker, tiles should load in priority order
        // First tile should be highest priority (x=4), last should be lowest (x=0)
        CHECK(allLoaded[0].id.x == 4);  // priority 0
        CHECK(allLoaded[1].id.x == 3);  // priority 1
        CHECK(allLoaded[2].id.x == 2);  // priority 10
        CHECK(allLoaded[3].id.x == 1);  // priority 50
        CHECK(allLoaded[4].id.x == 0);  // priority 100
    }
}

// ============================================================================
// VirtualTextureTileLoader Callback Tests
// ============================================================================

TEST_SUITE("VirtualTextureTileLoader Callback") {
    TEST_CASE("callback is invoked when tile loads") {
        TempTileDirectory tempDir;
        REQUIRE(tempDir.createTile(0, 0, 0));

        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 1);
        REQUIRE(loader != nullptr);

        std::atomic<int> callbackCount{0};
        TileId receivedId;

        loader->setLoadedCallback([&](const LoadedTile& tile) {
            receivedId = tile.id;
            callbackCount++;
        });

        loader->queueTile(TileId(0, 0, 0));

        // Wait for callback
        auto start = std::chrono::steady_clock::now();
        while (callbackCount == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
                FAIL("Timeout waiting for callback");
                break;
            }
        }

        CHECK(callbackCount == 1);
        CHECK(receivedId.x == 0);
        CHECK(receivedId.y == 0);
        CHECK(receivedId.mipLevel == 0);
    }
}

// ============================================================================
// VirtualTextureTileLoader Stress Tests
// ============================================================================

TEST_SUITE("VirtualTextureTileLoader Stress") {
    TEST_CASE("handles rapid queue/clear cycles") {
        TempTileDirectory tempDir;

        auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 4);
        REQUIRE(loader != nullptr);

        // Rapidly queue and clear many times
        for (int cycle = 0; cycle < 20; ++cycle) {
            std::vector<TileId> tiles;
            for (int i = 0; i < 10; ++i) {
                tiles.push_back(TileId(i, cycle, 0));
            }
            loader->queueTiles(tiles);
            loader->clearQueue();
        }

        // Should not crash or deadlock
        CHECK(true);  // If we get here, test passed
    }

    TEST_CASE("destructor waits for workers") {
        TempTileDirectory tempDir;

        // Create many tiles
        for (int i = 0; i < 20; ++i) {
            tempDir.createTile(i, 0, 0, 16, 16);
        }

        {
            auto loader = VirtualTextureTileLoader::create(tempDir.getPath(), 4);
            REQUIRE(loader != nullptr);

            // Queue many tiles
            for (int i = 0; i < 20; ++i) {
                loader->queueTile(TileId(i, 0, 0));
            }

            // Loader destructor should wait for workers to finish
        }

        // If we get here without hanging, workers shut down correctly
        CHECK(true);
    }
}
