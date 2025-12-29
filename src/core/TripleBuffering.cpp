#include "TripleBuffering.h"
#include <SDL3/SDL_log.h>

bool TripleBuffering::init(VkDevice device, uint32_t frameCount) {
    if (frameCount == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TripleBuffering::init: frameCount must be > 0");
        return false;
    }

    destroy();

    // Use FrameBuffered's resize with a factory function
    frames_.resize(frameCount, [device](uint32_t i) -> FrameSyncPrimitives {
        FrameSyncPrimitives primitives;

        if (!ManagedSemaphore::create(device, primitives.imageAvailable)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "TripleBuffering::init: failed to create imageAvailable[%u]", i);
            return primitives;
        }

        if (!ManagedSemaphore::create(device, primitives.renderFinished)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "TripleBuffering::init: failed to create renderFinished[%u]", i);
            return primitives;
        }

        // Create fences in signaled state so first frame doesn't wait forever
        if (!ManagedFence::createSignaled(device, primitives.inFlightFence)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "TripleBuffering::init: failed to create inFlightFence[%u]", i);
            return primitives;
        }

        return primitives;
    });

    // Verify all primitives were created successfully
    for (uint32_t i = 0; i < frameCount; ++i) {
        const auto& p = frames_[i];
        if (!p.imageAvailable.get() || !p.renderFinished.get() || !p.inFlightFence.get()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "TripleBuffering::init: incomplete primitives for frame %u", i);
            destroy();
            return false;
        }
    }

    SDL_Log("TripleBuffering: initialized with %u frames in flight", frameCount);
    return true;
}

void TripleBuffering::destroy() {
    // RAII handles cleanup - just clear the FrameBuffered container
    frames_.clear();
}
