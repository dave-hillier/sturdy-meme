// DDS (DirectDraw Surface) File Format Reader/Writer
// Supports BC1, BC4, BC5, and BC7 compressed textures

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>

namespace DDS {

// DDS file magic number
constexpr uint32_t DDS_MAGIC = 0x20534444; // "DDS "

// Pixel format flags
constexpr uint32_t DDPF_FOURCC = 0x00000004;

// Header flags
constexpr uint32_t DDSD_CAPS        = 0x00000001;
constexpr uint32_t DDSD_HEIGHT      = 0x00000002;
constexpr uint32_t DDSD_WIDTH       = 0x00000004;
constexpr uint32_t DDSD_PIXELFORMAT = 0x00001000;
constexpr uint32_t DDSD_MIPMAPCOUNT = 0x00020000;
constexpr uint32_t DDSD_LINEARSIZE  = 0x00080000;

// Caps flags
constexpr uint32_t DDSCAPS_TEXTURE = 0x00001000;

// FourCC codes
constexpr uint32_t FOURCC_DXT1 = 0x31545844; // "DXT1" - BC1
constexpr uint32_t FOURCC_ATI1 = 0x31495441; // "ATI1" - BC4
constexpr uint32_t FOURCC_ATI2 = 0x32495441; // "ATI2" - BC5
constexpr uint32_t FOURCC_DX10 = 0x30315844; // "DX10" - Extended header

// DXGI formats (for DX10 extended header)
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

// Resource dimension
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

// BC format enumeration
enum class Format {
    BC1,
    BC1_SRGB,
    BC4,
    BC5,
    BC7,
    BC7_SRGB,
    UNKNOWN
};

// Get bytes per block for format
inline uint32_t getBytesPerBlock(Format format) {
    switch (format) {
        case Format::BC1:
        case Format::BC1_SRGB:
        case Format::BC4:
            return 8;
        case Format::BC5:
        case Format::BC7:
        case Format::BC7_SRGB:
            return 16;
        default:
            return 0;
    }
}

// Calculate data size for a mip level
inline uint32_t calculateMipSize(uint32_t width, uint32_t height, Format format) {
    uint32_t blockWidth = (width + 3) / 4;
    uint32_t blockHeight = (height + 3) / 4;
    return blockWidth * blockHeight * getBytesPerBlock(format);
}

// Loaded DDS image
struct Image {
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    Format format;
    std::vector<uint8_t> data;

    bool isValid() const { return !data.empty() && format != Format::UNKNOWN; }
};

// Write a DDS file
inline bool write(const std::string& path, uint32_t width, uint32_t height,
                  Format format, const void* data, uint32_t dataSize) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Write magic
    uint32_t magic = DDS_MAGIC;
    file.write(reinterpret_cast<const char*>(&magic), 4);

    // Prepare header
    Header header{};
    header.size = 124;
    header.flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE;
    header.height = height;
    header.width = width;
    header.pitchOrLinearSize = dataSize;
    header.depth = 1;
    header.mipMapCount = 1;
    header.caps = DDSCAPS_TEXTURE;

    // Pixel format
    header.pixelFormat.size = 32;
    header.pixelFormat.flags = DDPF_FOURCC;

    bool useDX10 = (format == Format::BC7 || format == Format::BC7_SRGB);

    if (useDX10) {
        header.pixelFormat.fourCC = FOURCC_DX10;
    } else {
        switch (format) {
            case Format::BC1:
            case Format::BC1_SRGB:
                header.pixelFormat.fourCC = FOURCC_DXT1;
                break;
            case Format::BC4:
                header.pixelFormat.fourCC = FOURCC_ATI1;
                break;
            case Format::BC5:
                header.pixelFormat.fourCC = FOURCC_ATI2;
                break;
            default:
                return false;
        }
    }

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write DX10 header if needed
    if (useDX10) {
        HeaderDX10 dx10Header{};
        dx10Header.resourceDimension = ResourceDimension::TEXTURE2D;
        dx10Header.arraySize = 1;

        switch (format) {
            case Format::BC7:
                dx10Header.dxgiFormat = DXGIFormat::BC7_UNORM;
                break;
            case Format::BC7_SRGB:
                dx10Header.dxgiFormat = DXGIFormat::BC7_UNORM_SRGB;
                break;
            default:
                break;
        }

        file.write(reinterpret_cast<const char*>(&dx10Header), sizeof(dx10Header));
    }

    // Write data
    file.write(reinterpret_cast<const char*>(data), dataSize);

    return file.good();
}

// Read a DDS file
inline Image read(const std::string& path) {
    Image result{};
    result.format = Format::UNKNOWN;

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
                result.format = Format::BC1;
                break;
            case DXGIFormat::BC1_UNORM_SRGB:
                result.format = Format::BC1_SRGB;
                break;
            case DXGIFormat::BC4_UNORM:
            case DXGIFormat::BC4_SNORM:
                result.format = Format::BC4;
                break;
            case DXGIFormat::BC5_UNORM:
            case DXGIFormat::BC5_SNORM:
                result.format = Format::BC5;
                break;
            case DXGIFormat::BC7_UNORM:
                result.format = Format::BC7;
                break;
            case DXGIFormat::BC7_UNORM_SRGB:
                result.format = Format::BC7_SRGB;
                break;
            default:
                return result;
        }
    } else if (header.pixelFormat.flags & DDPF_FOURCC) {
        switch (header.pixelFormat.fourCC) {
            case FOURCC_DXT1:
                result.format = Format::BC1;
                break;
            case FOURCC_ATI1:
                result.format = Format::BC4;
                break;
            case FOURCC_ATI2:
                result.format = Format::BC5;
                break;
            default:
                return result;
        }
    } else {
        return result;
    }

    // Calculate total data size (all mip levels)
    uint32_t totalSize = 0;
    uint32_t mipWidth = result.width;
    uint32_t mipHeight = result.height;
    for (uint32_t i = 0; i < result.mipLevels; i++) {
        totalSize += calculateMipSize(mipWidth, mipHeight, result.format);
        mipWidth = std::max(1u, mipWidth / 2);
        mipHeight = std::max(1u, mipHeight / 2);
    }

    // Read data
    result.data.resize(totalSize);
    file.read(reinterpret_cast<char*>(result.data.data()), totalSize);

    if (!file.good()) {
        result.data.clear();
        result.format = Format::UNKNOWN;
    }

    return result;
}

// Get Vulkan format for DDS format
inline uint32_t getVulkanFormat(Format format) {
    // VkFormat values
    constexpr uint32_t VK_FORMAT_BC1_RGB_UNORM_BLOCK = 131;
    constexpr uint32_t VK_FORMAT_BC1_RGB_SRGB_BLOCK = 132;
    constexpr uint32_t VK_FORMAT_BC4_UNORM_BLOCK = 139;
    constexpr uint32_t VK_FORMAT_BC5_UNORM_BLOCK = 141;
    constexpr uint32_t VK_FORMAT_BC7_UNORM_BLOCK = 145;
    constexpr uint32_t VK_FORMAT_BC7_SRGB_BLOCK = 146;

    switch (format) {
        case Format::BC1:      return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case Format::BC1_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
        case Format::BC4:      return VK_FORMAT_BC4_UNORM_BLOCK;
        case Format::BC5:      return VK_FORMAT_BC5_UNORM_BLOCK;
        case Format::BC7:      return VK_FORMAT_BC7_UNORM_BLOCK;
        case Format::BC7_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
        default:               return 0;
    }
}

} // namespace DDS
