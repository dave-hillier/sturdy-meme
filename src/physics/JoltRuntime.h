#pragma once

#include <memory>

// JoltRuntime RAII wrapper for global Jolt state
// Uses weak_ptr to allow multiple PhysicsWorld instances to share runtime
struct JoltRuntime {
    JoltRuntime();
    ~JoltRuntime();

    // Get or create the shared runtime (thread-safe, ref-counted)
    static std::shared_ptr<JoltRuntime> acquire();

    // Non-copyable, non-movable
    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;
    JoltRuntime(JoltRuntime&&) = delete;
    JoltRuntime& operator=(JoltRuntime&&) = delete;
};
