#pragma once

#include "interfaces/IPerformanceControl.h"
#include <functional>

struct PerformanceToggles;

/**
 * PerformanceControlSubsystem - Implements IPerformanceControl
 * Manages performance toggles and sync callback.
 */
class PerformanceControlSubsystem : public IPerformanceControl {
public:
    using SyncCallback = std::function<void()>;

    PerformanceControlSubsystem(PerformanceToggles& toggles, SyncCallback syncCallback)
        : toggles_(toggles)
        , syncCallback_(syncCallback) {}

    PerformanceToggles& getPerformanceToggles() override { return toggles_; }
    const PerformanceToggles& getPerformanceToggles() const override { return toggles_; }
    void syncPerformanceToggles() override { if (syncCallback_) syncCallback_(); }

    // Set the sync callback (allows deferred initialization)
    void setSyncCallback(SyncCallback callback) { syncCallback_ = callback; }

private:
    PerformanceToggles& toggles_;
    SyncCallback syncCallback_;
};
