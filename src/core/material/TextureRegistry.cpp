#include "TextureRegistry.h"
#include <SDL3/SDL_log.h>

void TextureRegistry::init(VkImageView whiteView, VkSampler whiteSampler,
                           VkImageView blackView, VkSampler blackSampler,
                           VkImageView normalView, VkSampler normalSampler) {
    entries_.clear();
    freeList_.clear();
    activeCount_ = 0;

    // Reserve placeholder slots at well-known indices
    entries_.push_back({whiteView, whiteSampler, true});   // 0: white
    entries_.push_back({blackView, blackSampler, true});    // 1: black
    entries_.push_back({normalView, normalSampler, true});  // 2: flat normal
    activeCount_ = FIRST_USER_INDEX;

    dirty_ = true;
    initialized_ = true;

    SDL_Log("TextureRegistry: Initialized with %u placeholder textures", FIRST_USER_INDEX);
}

TextureRegistry::Handle TextureRegistry::registerTexture(VkImageView view, VkSampler sampler) {
    if (!initialized_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TextureRegistry: Cannot register texture before init()");
        return Handle{};
    }

    uint32_t index;
    if (!freeList_.empty()) {
        index = freeList_.back();
        freeList_.pop_back();
        entries_[index] = {view, sampler, true};
    } else {
        index = static_cast<uint32_t>(entries_.size());
        entries_.push_back({view, sampler, true});
    }

    activeCount_++;
    dirty_ = true;
    return Handle{index};
}

void TextureRegistry::unregisterTexture(Handle handle) {
    if (!handle.isValid() || handle.index >= entries_.size()) {
        return;
    }

    if (handle.index < FIRST_USER_INDEX) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "TextureRegistry: Cannot unregister placeholder texture at index %u", handle.index);
        return;
    }

    if (!entries_[handle.index].active) {
        return;
    }

    entries_[handle.index].active = false;
    entries_[handle.index].view = VK_NULL_HANDLE;
    entries_[handle.index].sampler = VK_NULL_HANDLE;
    freeList_.push_back(handle.index);
    activeCount_--;
    dirty_ = true;
}

VkImageView TextureRegistry::getImageView(uint32_t index) const {
    if (index < entries_.size() && entries_[index].active) {
        return entries_[index].view;
    }
    // Fall back to white placeholder
    if (!entries_.empty()) {
        return entries_[PLACEHOLDER_WHITE].view;
    }
    return VK_NULL_HANDLE;
}

VkSampler TextureRegistry::getSampler(uint32_t index) const {
    if (index < entries_.size() && entries_[index].active) {
        return entries_[index].sampler;
    }
    // Fall back to white placeholder
    if (!entries_.empty()) {
        return entries_[PLACEHOLDER_WHITE].sampler;
    }
    return VK_NULL_HANDLE;
}
