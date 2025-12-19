// BCn Texture Compression Utilities
// BC1: RGB compression (4 bpp) - good for albedo textures
// BC4: Single channel compression (4 bpp) - good for grayscale
// BC5: Two channel compression (8 bpp) - good for normal maps
// BC7: High quality RGBA compression (8 bpp) - best quality

#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace BCCompress {

// Block size constants
constexpr uint32_t BLOCK_SIZE = 4;
constexpr uint32_t BC1_BLOCK_BYTES = 8;
constexpr uint32_t BC4_BLOCK_BYTES = 8;
constexpr uint32_t BC5_BLOCK_BYTES = 16;
constexpr uint32_t BC7_BLOCK_BYTES = 16;

// Color utilities
inline uint16_t packRGB565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline void unpackRGB565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
    g = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
    b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
}

// Simple color distance for endpoint selection
inline int colorDistance(const uint8_t* c1, const uint8_t* c2) {
    int dr = static_cast<int>(c1[0]) - static_cast<int>(c2[0]);
    int dg = static_cast<int>(c1[1]) - static_cast<int>(c2[1]);
    int db = static_cast<int>(c1[2]) - static_cast<int>(c2[2]);
    return dr * dr + dg * dg + db * db;
}

// Compress a 4x4 block to BC1 format
// pixels: 16 pixels, 4 bytes each (RGBA)
// output: 8 bytes BC1 compressed block
inline void compressBlockBC1(const uint8_t* pixels, uint8_t* output) {
    // Find min/max colors in the block
    uint8_t minColor[3] = {255, 255, 255};
    uint8_t maxColor[3] = {0, 0, 0};

    for (int i = 0; i < 16; i++) {
        const uint8_t* p = pixels + i * 4;
        for (int c = 0; c < 3; c++) {
            minColor[c] = std::min(minColor[c], p[c]);
            maxColor[c] = std::max(maxColor[c], p[c]);
        }
    }

    // Inset the bounding box to improve quality
    uint8_t inset[3];
    for (int c = 0; c < 3; c++) {
        int range = maxColor[c] - minColor[c];
        int insetVal = range >> 4;
        minColor[c] = static_cast<uint8_t>(std::min(255, minColor[c] + insetVal));
        maxColor[c] = static_cast<uint8_t>(std::max(0, maxColor[c] - insetVal));
    }

    uint16_t c0 = packRGB565(maxColor[0], maxColor[1], maxColor[2]);
    uint16_t c1 = packRGB565(minColor[0], minColor[1], minColor[2]);

    // Ensure c0 > c1 for 4-color mode
    if (c0 < c1) {
        std::swap(c0, c1);
        std::swap(maxColor[0], minColor[0]);
        std::swap(maxColor[1], minColor[1]);
        std::swap(maxColor[2], minColor[2]);
    }

    // Write endpoint colors
    output[0] = c0 & 0xFF;
    output[1] = (c0 >> 8) & 0xFF;
    output[2] = c1 & 0xFF;
    output[3] = (c1 >> 8) & 0xFF;

    // Calculate palette colors (4-color mode: c0 > c1)
    uint8_t palette[4][3];
    for (int c = 0; c < 3; c++) {
        palette[0][c] = maxColor[c];
        palette[1][c] = minColor[c];
        palette[2][c] = static_cast<uint8_t>((2 * maxColor[c] + minColor[c] + 1) / 3);
        palette[3][c] = static_cast<uint8_t>((maxColor[c] + 2 * minColor[c] + 1) / 3);
    }

    // Encode indices
    uint32_t indices = 0;
    for (int i = 0; i < 16; i++) {
        const uint8_t* p = pixels + i * 4;

        // Find closest palette color
        int bestIdx = 0;
        int bestDist = colorDistance(p, palette[0]);
        for (int j = 1; j < 4; j++) {
            int dist = colorDistance(p, palette[j]);
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = j;
            }
        }

        indices |= (bestIdx << (i * 2));
    }

    output[4] = indices & 0xFF;
    output[5] = (indices >> 8) & 0xFF;
    output[6] = (indices >> 16) & 0xFF;
    output[7] = (indices >> 24) & 0xFF;
}

// Compress a 4x4 block to BC4 format (single channel)
// pixels: 16 single-channel values
// output: 8 bytes BC4 compressed block
inline void compressBlockBC4(const uint8_t* pixels, uint8_t* output) {
    // Find min/max values
    uint8_t minVal = 255;
    uint8_t maxVal = 0;

    for (int i = 0; i < 16; i++) {
        minVal = std::min(minVal, pixels[i]);
        maxVal = std::max(maxVal, pixels[i]);
    }

    // Write endpoints
    output[0] = maxVal;
    output[1] = minVal;

    // Calculate palette (8 values when alpha0 > alpha1)
    uint8_t palette[8];
    palette[0] = maxVal;
    palette[1] = minVal;

    if (maxVal > minVal) {
        for (int i = 1; i < 7; i++) {
            palette[i + 1] = static_cast<uint8_t>(((7 - i) * maxVal + i * minVal + 3) / 7);
        }
    } else {
        for (int i = 1; i < 5; i++) {
            palette[i + 1] = static_cast<uint8_t>(((5 - i) * maxVal + i * minVal + 2) / 5);
        }
        palette[6] = 0;
        palette[7] = 255;
    }

    // Encode 3-bit indices (48 bits = 6 bytes)
    uint64_t indices = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t val = pixels[i];

        // Find closest palette value
        int bestIdx = 0;
        int bestDist = std::abs(static_cast<int>(val) - static_cast<int>(palette[0]));
        for (int j = 1; j < 8; j++) {
            int dist = std::abs(static_cast<int>(val) - static_cast<int>(palette[j]));
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = j;
            }
        }

        indices |= (static_cast<uint64_t>(bestIdx) << (i * 3));
    }

    // Write 6 bytes of indices
    for (int i = 0; i < 6; i++) {
        output[2 + i] = static_cast<uint8_t>((indices >> (i * 8)) & 0xFF);
    }
}

// Compress a 4x4 block to BC5 format (two channels, e.g., for normal maps)
// pixels: 16 pixels, 4 bytes each (RGBA, uses R and G)
// output: 16 bytes BC5 compressed block
inline void compressBlockBC5(const uint8_t* pixels, uint8_t* output) {
    // Extract R and G channels
    uint8_t redChannel[16];
    uint8_t greenChannel[16];

    for (int i = 0; i < 16; i++) {
        redChannel[i] = pixels[i * 4 + 0];
        greenChannel[i] = pixels[i * 4 + 1];
    }

    // Compress each channel with BC4
    compressBlockBC4(redChannel, output);
    compressBlockBC4(greenChannel, output + 8);
}

// BC7 Mode 6 compression (high quality RGBA)
// This is a simplified single-mode encoder for quality
inline void compressBlockBC7Mode6(const uint8_t* pixels, uint8_t* output) {
    // Mode 6: 4 subsets, 7 bits color, 7 bits alpha, 4-bit indices
    // For simplicity, we use a basic BC7 mode 6 encoding

    // Find RGBA endpoints
    uint8_t minColor[4] = {255, 255, 255, 255};
    uint8_t maxColor[4] = {0, 0, 0, 0};

    for (int i = 0; i < 16; i++) {
        const uint8_t* p = pixels + i * 4;
        for (int c = 0; c < 4; c++) {
            minColor[c] = std::min(minColor[c], p[c]);
            maxColor[c] = std::max(maxColor[c], p[c]);
        }
    }

    // Clear output
    std::memset(output, 0, 16);

    // Mode 6: bit 6 set (0b01000000 = 0x40)
    output[0] = 0x40;

    // Quantize endpoints to 7 bits
    uint8_t ep0[4], ep1[4];
    for (int c = 0; c < 4; c++) {
        ep0[c] = maxColor[c] >> 1;
        ep1[c] = minColor[c] >> 1;
    }

    // Pack endpoints (7 bits each, 4 channels, 2 endpoints = 56 bits)
    // Bit layout for Mode 6:
    // Bits 0-6: mode (6 = 0b0100000)
    // Bits 7-13: R0, Bits 14-20: R1
    // Bits 21-27: G0, Bits 28-34: G1
    // Bits 35-41: B0, Bits 42-48: B1
    // Bits 49-55: A0, Bits 56-62: A1
    // Bits 63: P-bit
    // Bits 64-127: 4-bit indices

    uint64_t lo = 0x40; // Mode 6
    lo |= (static_cast<uint64_t>(ep0[0] & 0x7F) << 7);
    lo |= (static_cast<uint64_t>(ep1[0] & 0x7F) << 14);
    lo |= (static_cast<uint64_t>(ep0[1] & 0x7F) << 21);
    lo |= (static_cast<uint64_t>(ep1[1] & 0x7F) << 28);
    lo |= (static_cast<uint64_t>(ep0[2] & 0x7F) << 35);
    lo |= (static_cast<uint64_t>(ep1[2] & 0x7F) << 42);
    lo |= (static_cast<uint64_t>(ep0[3] & 0x7F) << 49);
    lo |= (static_cast<uint64_t>(ep1[3] & 0x7F) << 56);

    // P-bit at bit 63
    lo |= (static_cast<uint64_t>(1) << 63);

    // Calculate palette (16 values with linear interpolation)
    int palette[16][4];
    for (int c = 0; c < 4; c++) {
        int e0 = (ep0[c] << 1) | 1;
        int e1 = (ep1[c] << 1) | 1;
        for (int i = 0; i < 16; i++) {
            palette[i][c] = ((15 - i) * e0 + i * e1 + 7) / 15;
        }
    }

    // Find best indices for each pixel
    uint64_t hi = 0;
    for (int i = 0; i < 16; i++) {
        const uint8_t* p = pixels + i * 4;

        int bestIdx = 0;
        int bestDist = INT32_MAX;
        for (int j = 0; j < 16; j++) {
            int dist = 0;
            for (int c = 0; c < 4; c++) {
                int d = static_cast<int>(p[c]) - palette[j][c];
                dist += d * d;
            }
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = j;
            }
        }

        hi |= (static_cast<uint64_t>(bestIdx) << (i * 4));
    }

    // Write output
    std::memcpy(output, &lo, 8);
    std::memcpy(output + 8, &hi, 8);
}

// High-level compression functions

struct CompressedImage {
    std::vector<uint8_t> data;
    uint32_t width;
    uint32_t height;
    uint32_t blockWidth;  // Width in blocks
    uint32_t blockHeight; // Height in blocks
};

enum class BCFormat {
    BC1,  // RGB, 4 bpp
    BC4,  // R, 4 bpp
    BC5,  // RG, 8 bpp (normal maps)
    BC7   // RGBA, 8 bpp (high quality)
};

// Get bytes per block for format
inline uint32_t getBytesPerBlock(BCFormat format) {
    switch (format) {
        case BCFormat::BC1: return BC1_BLOCK_BYTES;
        case BCFormat::BC4: return BC4_BLOCK_BYTES;
        case BCFormat::BC5: return BC5_BLOCK_BYTES;
        case BCFormat::BC7: return BC7_BLOCK_BYTES;
        default: return 0;
    }
}

// Compress RGBA image to BCn format
// Input: RGBA pixels (4 bytes per pixel)
// Returns: Compressed image data
inline CompressedImage compressImage(const uint8_t* pixels, uint32_t width, uint32_t height, BCFormat format) {
    CompressedImage result;
    result.width = width;
    result.height = height;
    result.blockWidth = (width + 3) / 4;
    result.blockHeight = (height + 3) / 4;

    uint32_t bytesPerBlock = getBytesPerBlock(format);
    result.data.resize(result.blockWidth * result.blockHeight * bytesPerBlock);

    uint8_t blockPixels[16 * 4]; // 4x4 block, RGBA

    for (uint32_t by = 0; by < result.blockHeight; by++) {
        for (uint32_t bx = 0; bx < result.blockWidth; bx++) {
            // Extract 4x4 block (with clamping at edges)
            for (uint32_t py = 0; py < 4; py++) {
                for (uint32_t px = 0; px < 4; px++) {
                    uint32_t srcX = std::min(bx * 4 + px, width - 1);
                    uint32_t srcY = std::min(by * 4 + py, height - 1);
                    const uint8_t* src = pixels + (srcY * width + srcX) * 4;
                    uint8_t* dst = blockPixels + (py * 4 + px) * 4;
                    std::memcpy(dst, src, 4);
                }
            }

            // Compress block
            uint8_t* output = result.data.data() + (by * result.blockWidth + bx) * bytesPerBlock;

            switch (format) {
                case BCFormat::BC1:
                    compressBlockBC1(blockPixels, output);
                    break;
                case BCFormat::BC4: {
                    // Extract red channel only
                    uint8_t redChannel[16];
                    for (int i = 0; i < 16; i++) {
                        redChannel[i] = blockPixels[i * 4];
                    }
                    compressBlockBC4(redChannel, output);
                    break;
                }
                case BCFormat::BC5:
                    compressBlockBC5(blockPixels, output);
                    break;
                case BCFormat::BC7:
                    compressBlockBC7Mode6(blockPixels, output);
                    break;
            }
        }
    }

    return result;
}

// Decompress BC1 block for verification/preview
inline void decompressBlockBC1(const uint8_t* input, uint8_t* pixels) {
    uint16_t c0 = input[0] | (input[1] << 8);
    uint16_t c1 = input[2] | (input[3] << 8);
    uint32_t indices = input[4] | (input[5] << 8) | (input[6] << 16) | (input[7] << 24);

    uint8_t palette[4][4];
    unpackRGB565(c0, palette[0][0], palette[0][1], palette[0][2]);
    unpackRGB565(c1, palette[1][0], palette[1][1], palette[1][2]);
    palette[0][3] = palette[1][3] = 255;

    if (c0 > c1) {
        for (int c = 0; c < 3; c++) {
            palette[2][c] = static_cast<uint8_t>((2 * palette[0][c] + palette[1][c] + 1) / 3);
            palette[3][c] = static_cast<uint8_t>((palette[0][c] + 2 * palette[1][c] + 1) / 3);
        }
        palette[2][3] = palette[3][3] = 255;
    } else {
        for (int c = 0; c < 3; c++) {
            palette[2][c] = static_cast<uint8_t>((palette[0][c] + palette[1][c]) / 2);
        }
        palette[2][3] = 255;
        palette[3][0] = palette[3][1] = palette[3][2] = 0;
        palette[3][3] = 0; // Transparent black
    }

    for (int i = 0; i < 16; i++) {
        int idx = (indices >> (i * 2)) & 3;
        std::memcpy(pixels + i * 4, palette[idx], 4);
    }
}

} // namespace BCCompress
