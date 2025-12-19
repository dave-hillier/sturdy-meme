// DDS (DirectDraw Surface) File Format Loader
// Supports BC1, BC4, BC5, and BC7 compressed textures

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>

namespace DDSLoader {

// DDS file magic number
constexpr uint32_t DDS_MAGIC = 0x20534444; // "DDS "

// Pixel format flags
constexpr uint32_t DDPF_FOURCC = 0x00000004;

// FourCC codes
constexpr uint32_t FOURCC_DXT1 = 0x31545844; // "DXT1" - BC1
constexpr uint32_t FOURCC_ATI1 = 0x31495441; // "ATI1" - BC4
constexpr uint32_t FOURCC_ATI2 = 0x32495441; // "ATI2" - BC5
constexpr uint32_t FOURCC_DX10 = 0x30315844; // "DX10" - Extended header

// DXGI formats
enum class DXGIFormat : uint32_t {
    UNKNOWN                = 0,
    BC1_UNORM              = 71,
    BC1_UNORM_SRGB         = 72,
    BC4_UNORM              = 80,
    BC4_SNORM              = 81,
    BC5_UNORM              = 83,
    BC5_SNORM              = 84,
    BC7_UNORM              = 98,
    BC7_UNORM_SRGB         = 99,
};

enum class ResourceDimension : uint32_t {
    UNKNOWN   = 0,
    BUFFER    = 1,
    TEXTURE1D = 2,
    TEXTURE2D = 3,
    TEXTURE3D = 4,
};

#pragma pack(push, 1)

struct PixelFormat {
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

struct Header {
    uint32_t size;
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    PixelFormat pixelFormat;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

struct HeaderDX10 {
    DXGIFormat dxgiFormat;
    ResourceDimension resourceDimension;
    uint32_t miscFlag;
    uint32_t arraySize;
    uint32_t miscFlags2;
};

#pragma pack(pop)

// Loaded DDS image
struct Image {
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    VkFormat format;
    uint32_t blockSize;  // Bytes per 4x4 block
    std::vector<uint8_t> data;

    bool isValid() const { return !data.empty() && format != VK_FORMAT_UNDEFINED; }

    // Calculate size of a mip level
    uint32_t getMipSize(uint32_t level) const {
        uint32_t mipWidth = std::max(1u, width >> level);
        uint32_t mipHeight = std::max(1u, height >> level);
        uint32_t blockWidth = (mipWidth + 3) / 4;
        uint32_t blockHeight = (mipHeight + 3) / 4;
        return blockWidth * blockHeight * blockSize;
    }

    // Get offset to a mip level in data
    uint32_t getMipOffset(uint32_t level) const {
        uint32_t offset = 0;
        for (uint32_t i = 0; i < level; i++) {
            offset += getMipSize(i);
        }
        return offset;
    }

    // Get dimensions of a mip level
    void getMipDimensions(uint32_t level, uint32_t& w, uint32_t& h) const {
        w = std::max(1u, width >> level);
        h = std::max(1u, height >> level);
    }
};

// Get block size for Vulkan BCn format
inline uint32_t getBlockSize(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return 8;
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return 16;
        default:
            return 0;
    }
}

// Check if format is a BCn compressed format
inline bool isBCFormat(VkFormat format) {
    return getBlockSize(format) > 0;
}

// Read a DDS file
inline Image load(const std::string& path) {
    Image result{};
    result.format = VK_FORMAT_UNDEFINED;
    result.blockSize = 0;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return result;
    }

    // Read and verify magic
    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != DDS_MAGIC) {
        return result;
    }

    // Read header
    Header header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (header.size != 124 || header.pixelFormat.size != 32) {
        return result;
    }

    result.width = header.width;
    result.height = header.height;
    result.mipLevels = std::max(1u, header.mipMapCount);

    // Determine format
    bool hasDX10 = (header.pixelFormat.flags & DDPF_FOURCC) &&
                   (header.pixelFormat.fourCC == FOURCC_DX10);

    if (hasDX10) {
        HeaderDX10 dx10Header;
        file.read(reinterpret_cast<char*>(&dx10Header), sizeof(dx10Header));

        switch (dx10Header.dxgiFormat) {
            case DXGIFormat::BC1_UNORM:
                result.format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
                break;
            case DXGIFormat::BC1_UNORM_SRGB:
                result.format = VK_FORMAT_BC1_RGB_SRGB_BLOCK;
                break;
            case DXGIFormat::BC4_UNORM:
                result.format = VK_FORMAT_BC4_UNORM_BLOCK;
                break;
            case DXGIFormat::BC4_SNORM:
                result.format = VK_FORMAT_BC4_SNORM_BLOCK;
                break;
            case DXGIFormat::BC5_UNORM:
                result.format = VK_FORMAT_BC5_UNORM_BLOCK;
                break;
            case DXGIFormat::BC5_SNORM:
                result.format = VK_FORMAT_BC5_SNORM_BLOCK;
                break;
            case DXGIFormat::BC7_UNORM:
                result.format = VK_FORMAT_BC7_UNORM_BLOCK;
                break;
            case DXGIFormat::BC7_UNORM_SRGB:
                result.format = VK_FORMAT_BC7_SRGB_BLOCK;
                break;
            default:
                return result;
        }
    } else if (header.pixelFormat.flags & DDPF_FOURCC) {
        switch (header.pixelFormat.fourCC) {
            case FOURCC_DXT1:
                result.format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
                break;
            case FOURCC_ATI1:
                result.format = VK_FORMAT_BC4_UNORM_BLOCK;
                break;
            case FOURCC_ATI2:
                result.format = VK_FORMAT_BC5_UNORM_BLOCK;
                break;
            default:
                return result;
        }
    } else {
        return result;
    }

    result.blockSize = getBlockSize(result.format);

    // Calculate total data size (all mip levels)
    uint32_t totalSize = 0;
    uint32_t mipWidth = result.width;
    uint32_t mipHeight = result.height;
    for (uint32_t i = 0; i < result.mipLevels; i++) {
        uint32_t blockWidth = (mipWidth + 3) / 4;
        uint32_t blockHeight = (mipHeight + 3) / 4;
        totalSize += blockWidth * blockHeight * result.blockSize;
        mipWidth = std::max(1u, mipWidth / 2);
        mipHeight = std::max(1u, mipHeight / 2);
    }

    // Read data
    result.data.resize(totalSize);
    file.read(reinterpret_cast<char*>(result.data.data()), totalSize);

    if (!file.good()) {
        result.data.clear();
        result.format = VK_FORMAT_UNDEFINED;
    }

    return result;
}

} // namespace DDSLoader
