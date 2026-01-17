#include "JoltRuntime.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>

#include <SDL3/SDL_log.h>
#include <cstdarg>
#include <mutex>

// Callback for traces
static void TraceImpl(const char* inFMT, ...) {
    va_list args;
    va_start(args, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, args);
    va_end(args);
    SDL_Log("Jolt: %s", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
// Callback for asserts
static bool AssertFailedImpl(const char* inExpression, const char* inMessage, const char* inFile, uint32_t inLine) {
    SDL_Log("Jolt Assert: %s:%u: (%s) %s", inFile, inLine, inExpression, inMessage ? inMessage : "");
    return true; // Break into debugger
}
#endif

namespace {
    std::weak_ptr<JoltRuntime> g_joltRuntime;
    std::mutex g_joltMutex;
}

JoltRuntime::JoltRuntime() {
    // Register allocation hook
    JPH::RegisterDefaultAllocator();

    // Install callbacks
    JPH::Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

    // Create factory
    JPH::Factory::sInstance = new JPH::Factory();

    // Register all Jolt physics types
    JPH::RegisterTypes();

    SDL_Log("Jolt runtime initialized");
}

JoltRuntime::~JoltRuntime() {
    // Unregisters all types with the factory and cleans up the default material
    JPH::UnregisterTypes();

    // Destroy factory
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;

    SDL_Log("Jolt runtime shutdown");
}

std::shared_ptr<JoltRuntime> JoltRuntime::acquire() {
    std::lock_guard<std::mutex> lock(g_joltMutex);
    auto runtime = g_joltRuntime.lock();
    if (!runtime) {
        runtime = std::make_shared<JoltRuntime>();
        g_joltRuntime = runtime;
    }
    return runtime;
}
