// Tests for VirtualTextureTypes - pure data types and logic
// No Vulkan dependencies

#include <doctest/doctest.h>
#include "terrain/virtual_texture/VirtualTextureTypes.h"
#include <unordered_set>

using namespace VirtualTexture;

// ============================================================================
// TileId Tests
// ============================================================================

TEST_SUITE("TileId") {
    TEST_CASE("default constructor creates zero tile") {
        TileId id;
        CHECK(id.x == 0);
        CHECK(id.y == 0);
        CHECK(id.mipLevel == 0);
    }

    TEST_CASE("constructor sets fields correctly") {
        TileId id(100, 200, 5);
        CHECK(id.x == 100);
        CHECK(id.y == 200);
        CHECK(id.mipLevel == 5);
    }

    TEST_CASE("pack/unpack round-trip preserves values") {
        SUBCASE("zero values") {
            TileId original(0, 0, 0);
            uint32_t packed = original.pack();
            TileId unpacked = TileId::unpack(packed);
            CHECK(unpacked.x == 0);
            CHECK(unpacked.y == 0);
            CHECK(unpacked.mipLevel == 0);
        }

        SUBCASE("typical values") {
            TileId original(127, 255, 8);
            uint32_t packed = original.pack();
            TileId unpacked = TileId::unpack(packed);
            CHECK(unpacked.x == 127);
            CHECK(unpacked.y == 255);
            CHECK(unpacked.mipLevel == 8);
        }

        SUBCASE("max 10-bit coordinates") {
            // Pack uses 10 bits for x and y (0-1023 range)
            TileId original(1023, 1023, 15);
            uint32_t packed = original.pack();
            TileId unpacked = TileId::unpack(packed);
            CHECK(unpacked.x == 1023);
            CHECK(unpacked.y == 1023);
            CHECK(unpacked.mipLevel == 15);
        }

        SUBCASE("various mip levels") {
            for (uint8_t mip = 0; mip < 16; ++mip) {
                TileId original(50, 75, mip);
                TileId unpacked = TileId::unpack(original.pack());
                CHECK(unpacked.x == 50);
                CHECK(unpacked.y == 75);
                CHECK(unpacked.mipLevel == mip);
            }
        }
    }

    TEST_CASE("different tiles pack to different values") {
        TileId a(0, 0, 0);
        TileId b(1, 0, 0);
        TileId c(0, 1, 0);
        TileId d(0, 0, 1);

        CHECK(a.pack() != b.pack());
        CHECK(a.pack() != c.pack());
        CHECK(a.pack() != d.pack());
        CHECK(b.pack() != c.pack());
        CHECK(b.pack() != d.pack());
        CHECK(c.pack() != d.pack());
    }

    TEST_CASE("equality operator") {
        TileId a(5, 10, 2);
        TileId b(5, 10, 2);
        TileId c(5, 10, 3);

        CHECK(a == b);
        CHECK_FALSE(a == c);
        CHECK(a != c);
        CHECK_FALSE(a != b);
    }

    TEST_CASE("hash function works for unordered containers") {
        std::unordered_set<TileId> tileSet;

        TileId a(10, 20, 1);
        TileId b(10, 20, 1);
        TileId c(30, 40, 2);

        tileSet.insert(a);
        tileSet.insert(b);  // Should not increase size (duplicate)
        tileSet.insert(c);

        CHECK(tileSet.size() == 2);
        CHECK(tileSet.count(a) == 1);
        CHECK(tileSet.count(c) == 1);
    }
}

// ============================================================================
// VirtualTextureConfig Tests
// ============================================================================

TEST_SUITE("VirtualTextureConfig") {
    TEST_CASE("default values are sensible") {
        VirtualTextureConfig config;
        CHECK(config.virtualSizePixels == 65536);
        CHECK(config.tileSizePixels == 128);
        CHECK(config.cacheSizePixels == 4096);
        CHECK(config.borderPixels == 4);
        CHECK(config.maxMipLevels == 9);
    }

    TEST_CASE("getTilesPerAxis returns correct value") {
        VirtualTextureConfig config;
        config.virtualSizePixels = 1024;
        config.tileSizePixels = 128;
        CHECK(config.getTilesPerAxis() == 8);

        config.virtualSizePixels = 65536;
        config.tileSizePixels = 128;
        CHECK(config.getTilesPerAxis() == 512);

        config.virtualSizePixels = 4096;
        config.tileSizePixels = 256;
        CHECK(config.getTilesPerAxis() == 16);
    }

    TEST_CASE("getCacheTilesPerAxis returns correct value") {
        VirtualTextureConfig config;
        config.cacheSizePixels = 4096;
        config.tileSizePixels = 128;
        CHECK(config.getCacheTilesPerAxis() == 32);

        config.cacheSizePixels = 2048;
        config.tileSizePixels = 128;
        CHECK(config.getCacheTilesPerAxis() == 16);
    }

    TEST_CASE("getTotalCacheSlots returns cache tiles squared") {
        VirtualTextureConfig config;
        config.cacheSizePixels = 4096;
        config.tileSizePixels = 128;
        // 32 tiles per axis = 32 * 32 = 1024 slots
        CHECK(config.getTotalCacheSlots() == 1024);

        config.cacheSizePixels = 2048;
        config.tileSizePixels = 256;
        // 8 tiles per axis = 8 * 8 = 64 slots
        CHECK(config.getTotalCacheSlots() == 64);
    }

    TEST_CASE("getTilesAtMip halves for each mip level") {
        VirtualTextureConfig config;
        config.virtualSizePixels = 1024;
        config.tileSizePixels = 128;
        // Base: 8 tiles per axis

        CHECK(config.getTilesAtMip(0) == 8);   // Mip 0: full res
        CHECK(config.getTilesAtMip(1) == 4);   // Mip 1: half
        CHECK(config.getTilesAtMip(2) == 2);   // Mip 2: quarter
        CHECK(config.getTilesAtMip(3) == 1);   // Mip 3: 1 tile
        CHECK(config.getTilesAtMip(4) == 0);   // Beyond: 0
    }

    TEST_CASE("getWorldToVirtualScale calculates correctly") {
        VirtualTextureConfig config;
        config.virtualSizePixels = 65536;

        float terrainSize = 16384.0f;
        float scale = config.getWorldToVirtualScale(terrainSize);
        CHECK(scale == doctest::Approx(4.0f));  // 65536 / 16384 = 4

        terrainSize = 32768.0f;
        scale = config.getWorldToVirtualScale(terrainSize);
        CHECK(scale == doctest::Approx(2.0f));  // 65536 / 32768 = 2
    }
}

// ============================================================================
// PageTableEntry Tests
// ============================================================================

TEST_SUITE("PageTableEntry") {
    TEST_CASE("packRGBA8 packs correctly") {
        PageTableEntry entry;
        entry.cacheX = 0;
        entry.cacheY = 0;
        entry.valid = 0;
        CHECK(entry.packRGBA8() == 0);

        entry.cacheX = 5;
        entry.cacheY = 10;
        entry.valid = 1;
        // Expected: valid << 24 | cacheY << 8 | cacheX
        // = (1 << 24) | (10 << 8) | 5
        // = 0x01000A05
        CHECK(entry.packRGBA8() == 0x01000A05);
    }

    TEST_CASE("packRGBA8 handles max values") {
        PageTableEntry entry;
        entry.cacheX = 255;
        entry.cacheY = 255;
        entry.valid = 1;
        // = (1 << 24) | (255 << 8) | 255
        // = 0x0100FFFF
        CHECK(entry.packRGBA8() == 0x0100FFFF);
    }
}

// ============================================================================
// CacheSlot Tests
// ============================================================================

TEST_SUITE("CacheSlot") {
    TEST_CASE("default state is unoccupied") {
        CacheSlot slot;
        CHECK_FALSE(slot.occupied);
        CHECK(slot.lastUsedFrame == 0);
    }
}

// ============================================================================
// FeedbackEntry Tests
// ============================================================================

TEST_SUITE("FeedbackEntry") {
    TEST_CASE("getTileId unpacks correctly") {
        FeedbackEntry entry;
        TileId original(123, 456, 7);
        entry.tileIdPacked = original.pack();
        entry.priority = 100;

        TileId unpacked = entry.getTileId();
        CHECK(unpacked.x == 123);
        CHECK(unpacked.y == 456);
        CHECK(unpacked.mipLevel == 7);
    }
}

// ============================================================================
// LoadedTile Tests
// ============================================================================

TEST_SUITE("LoadedTile") {
    TEST_CASE("isValid checks all conditions") {
        LoadedTile tile;
        CHECK_FALSE(tile.isValid());  // Empty pixels, zero dimensions

        tile.pixels = {1, 2, 3, 4};
        CHECK_FALSE(tile.isValid());  // Still zero dimensions

        tile.width = 1;
        CHECK_FALSE(tile.isValid());  // height still zero

        tile.height = 1;
        CHECK(tile.isValid());  // Now valid
    }

    TEST_CASE("isValid fails with empty pixels") {
        LoadedTile tile;
        tile.width = 128;
        tile.height = 128;
        tile.pixels.clear();
        CHECK_FALSE(tile.isValid());
    }

    TEST_CASE("isCompressed returns correct value") {
        LoadedTile tile;

        tile.format = TileFormat::RGBA8;
        CHECK_FALSE(tile.isCompressed());

        tile.format = TileFormat::BC1;
        CHECK(tile.isCompressed());

        tile.format = TileFormat::BC7_SRGB;
        CHECK(tile.isCompressed());
    }

    TEST_CASE("getBlockSize returns correct sizes") {
        LoadedTile tile;

        // BC1, BC4: 8 bytes per 4x4 block
        tile.format = TileFormat::BC1;
        CHECK(tile.getBlockSize() == 8);

        tile.format = TileFormat::BC1_SRGB;
        CHECK(tile.getBlockSize() == 8);

        tile.format = TileFormat::BC4;
        CHECK(tile.getBlockSize() == 8);

        // BC5, BC7: 16 bytes per 4x4 block
        tile.format = TileFormat::BC5;
        CHECK(tile.getBlockSize() == 16);

        tile.format = TileFormat::BC7;
        CHECK(tile.getBlockSize() == 16);

        tile.format = TileFormat::BC7_SRGB;
        CHECK(tile.getBlockSize() == 16);

        // RGBA8: 0 (not block compressed)
        tile.format = TileFormat::RGBA8;
        CHECK(tile.getBlockSize() == 0);
    }
}
