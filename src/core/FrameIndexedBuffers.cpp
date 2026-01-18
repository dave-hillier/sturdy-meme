#include "FrameIndexedBuffers.h"
#include <SDL3/SDL_log.h>

namespace BufferUtils {

FrameIndexedBuffers::~FrameIndexedBuffers() {
    destroy();
}

FrameIndexedBuffers::FrameIndexedBuffers(FrameIndexedBuffers&& other) noexcept
    : buffers_(std::move(other.buffers_))
    , allocations_(std::move(other.allocations_))
    , frameCount_(other.frameCount_)
    , allocator_(other.allocator_) {
    other.frameCount_ = 0;
    other.allocator_ = VK_NULL_HANDLE;
}

FrameIndexedBuffers& FrameIndexedBuffers::operator=(FrameIndexedBuffers&& other) noexcept {
    if (this != &other) {
        destroy();
        buffers_ = std::move(other.buffers_);
        allocations_ = std::move(other.allocations_);
        frameCount_ = other.frameCount_;
        allocator_ = other.allocator_;
        other.frameCount_ = 0;
        other.allocator_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool FrameIndexedBuffers::resize(VmaAllocator allocator, uint32_t frameCount, vk::DeviceSize size,
                                  vk::BufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    destroy();

    if (!allocator || frameCount == 0 || size == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "FrameIndexedBuffers::resize: invalid params (allocator=%p, frames=%u, size=%zu)",
            allocator, frameCount, static_cast<size_t>(size));
        return false;
    }

    allocator_ = allocator;
    frameCount_ = frameCount;
    buffers_.resize(frameCount);
    allocations_.resize(frameCount);

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(size)
        .setUsage(usage)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;

    for (uint32_t i = 0; i < frameCount; ++i) {
        VkBuffer rawBuffer;
        VkResult result = vmaCreateBuffer(
            allocator,
            reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
            &allocInfo,
            &rawBuffer,
            &allocations_[i],
            nullptr);

        if (result != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "FrameIndexedBuffers::resize: failed to create buffer %u", i);
            destroy();
            return false;
        }
        buffers_[i] = vk::Buffer(rawBuffer);
    }

    return true;
}

void FrameIndexedBuffers::destroy() {
    if (allocator_) {
        for (uint32_t i = 0; i < buffers_.size(); ++i) {
            if (buffers_[i]) {
                vmaDestroyBuffer(allocator_, static_cast<VkBuffer>(buffers_[i]), allocations_[i]);
            }
        }
    }
    buffers_.clear();
    allocations_.clear();
    frameCount_ = 0;
}

vk::Buffer FrameIndexedBuffers::get(uint32_t frameIndex) const {
    if (buffers_.empty()) return vk::Buffer{};
    return buffers_[frameIndex % frameCount_];
}

VkBuffer FrameIndexedBuffers::getVk(uint32_t frameIndex) const {
    return static_cast<VkBuffer>(get(frameIndex));
}

}  // namespace BufferUtils
