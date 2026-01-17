#include "TimelineSemaphore.h"
#include <SDL3/SDL_log.h>

bool TimelineSemaphore::init(const vk::raii::Device& device, uint64_t initialValue) {
    if (semaphore_.has_value()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "TimelineSemaphore: already initialized, destroying existing semaphore");
        semaphore_.reset();
    }

    device_ = &device;
    pendingSignalValue_ = initialValue;

    // Create timeline semaphore type info
    auto typeInfo = vk::SemaphoreTypeCreateInfo{}
        .setSemaphoreType(vk::SemaphoreType::eTimeline)
        .setInitialValue(initialValue);

    auto createInfo = vk::SemaphoreCreateInfo{}
        .setPNext(&typeInfo);

    try {
        semaphore_.emplace(device, createInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TimelineSemaphore: failed to create semaphore: %s", e.what());
        device_ = nullptr;
        return false;
    }

    return true;
}

uint64_t TimelineSemaphore::getCounterValue() const {
    if (!semaphore_) {
        return 0;
    }

    // vkGetSemaphoreCounterValue is non-blocking - call on semaphore object
    return semaphore_->getCounterValue();
}

vk::Result TimelineSemaphore::wait(uint64_t value, uint64_t timeoutNs) const {
    if (!semaphore_) {
        return vk::Result::eErrorUnknown;
    }

    auto waitInfo = vk::SemaphoreWaitInfo{}
        .setSemaphores(**semaphore_)
        .setValues(value);

    return device_->waitSemaphores(waitInfo, timeoutNs);
}

bool TimelineSemaphore::signal(uint64_t value) {
    if (!semaphore_) {
        return false;
    }

    auto signalInfo = vk::SemaphoreSignalInfo{}
        .setSemaphore(**semaphore_)
        .setValue(value);

    try {
        device_->signalSemaphore(signalInfo);
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TimelineSemaphore: failed to signal semaphore: %s", e.what());
        return false;
    }
}

vk::SemaphoreSubmitInfo TimelineSemaphore::createSubmitInfo(
    uint64_t value, vk::PipelineStageFlags2 stageMask) const
{
    return vk::SemaphoreSubmitInfo{}
        .setSemaphore(semaphore_ ? **semaphore_ : vk::Semaphore{})
        .setValue(value)
        .setStageMask(stageMask);
}

vk::TimelineSemaphoreSubmitInfo TimelineSemaphore::createTimelineSubmitInfo(
    const uint64_t* waitValues, uint32_t waitCount,
    const uint64_t* signalValues, uint32_t signalCount)
{
    return vk::TimelineSemaphoreSubmitInfo{}
        .setWaitSemaphoreValueCount(waitCount)
        .setPWaitSemaphoreValues(waitValues)
        .setSignalSemaphoreValueCount(signalCount)
        .setPSignalSemaphoreValues(signalValues);
}
