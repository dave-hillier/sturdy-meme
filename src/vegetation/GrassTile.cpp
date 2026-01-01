#include "GrassTile.h"
#include <SDL3/SDL.h>

GrassTile::~GrassTile() {
    destroy();
}

GrassTile::GrassTile(GrassTile&& other) noexcept
    : allocator_(other.allocator_)
    , coord_(other.coord_)
    , instanceBuffers_(std::move(other.instanceBuffers_))
    , indirectBuffers_(std::move(other.indirectBuffers_))
{
    other.allocator_ = VK_NULL_HANDLE;
}

GrassTile& GrassTile::operator=(GrassTile&& other) noexcept {
    if (this != &other) {
        destroy();

        allocator_ = other.allocator_;
        coord_ = other.coord_;
        instanceBuffers_ = std::move(other.instanceBuffers_);
        indirectBuffers_ = std::move(other.indirectBuffers_);

        other.allocator_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool GrassTile::init(VmaAllocator allocator, TileCoord coord, uint32_t bufferSetCount) {
    allocator_ = allocator;
    coord_ = coord;

    // Instance buffer size: MAX_INSTANCES_PER_TILE * sizeof(GrassInstance)
    // GrassInstance is 3 * vec4 = 48 bytes
    constexpr VkDeviceSize instanceSize = 48; // sizeof(GrassInstance)
    VkDeviceSize instanceBufferSize = instanceSize * GrassConstants::MAX_INSTANCES_PER_TILE;
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);

    // Create instance buffers (storage buffer for compute, vertex buffer for render)
    BufferUtils::DoubleBufferedBufferBuilder instanceBuilder;
    if (!instanceBuilder.setAllocator(allocator)
             .setSetCount(bufferSetCount)
             .setSize(instanceBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .build(instanceBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GrassTile: Failed to create instance buffers for tile (%d, %d)",
            coord_.x, coord_.z);
        return false;
    }

    // Create indirect buffers
    BufferUtils::DoubleBufferedBufferBuilder indirectBuilder;
    if (!indirectBuilder.setAllocator(allocator)
             .setSetCount(bufferSetCount)
             .setSize(indirectBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .build(indirectBuffers_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GrassTile: Failed to create indirect buffers for tile (%d, %d)",
            coord_.x, coord_.z);
        BufferUtils::destroyBuffers(allocator, instanceBuffers_);
        return false;
    }

    SDL_Log("GrassTile: Created tile at (%d, %d), world origin (%.1f, %.1f)",
        coord_.x, coord_.z, getWorldOrigin().x, getWorldOrigin().y);

    return true;
}

void GrassTile::destroy() {
    if (allocator_ == VK_NULL_HANDLE) return;

    BufferUtils::destroyBuffers(allocator_, instanceBuffers_);
    BufferUtils::destroyBuffers(allocator_, indirectBuffers_);

    allocator_ = VK_NULL_HANDLE;
}
