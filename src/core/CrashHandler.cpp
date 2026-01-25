#include "CrashHandler.h"
#include <SDL3/SDL.h>
#include <csignal>
#include <cstdlib>

#if defined(__APPLE__) || defined(__linux__)
#include <execinfo.h>
#include <cxxabi.h>
#include <dlfcn.h>
#endif

namespace {

const char* getSignalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (Segmentation fault)";
        case SIGABRT: return "SIGABRT (Abort)";
        case SIGFPE:  return "SIGFPE (Floating point exception)";
        case SIGILL:  return "SIGILL (Illegal instruction)";
        case SIGBUS:  return "SIGBUS (Bus error)";
        default:      return "Unknown signal";
    }
}

#if defined(__APPLE__) || defined(__linux__)
void printBacktrace() {
    constexpr int maxFrames = 64;
    void* frames[maxFrames];

    int frameCount = backtrace(frames, maxFrames);
    if (frameCount == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "  (no backtrace available)");
        return;
    }

    char** symbols = backtrace_symbols(frames, frameCount);
    if (!symbols) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "  (failed to get symbols)");
        return;
    }

    // Skip first few frames (signal handler internals)
    for (int i = 2; i < frameCount; ++i) {
        // Try to demangle C++ symbols
        Dl_info info;
        if (dladdr(frames[i], &info) && info.dli_sname) {
            int status = 0;
            char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);

            if (status == 0 && demangled) {
                ptrdiff_t offset = static_cast<char*>(frames[i]) - static_cast<char*>(info.dli_saddr);
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "  %2d: %s +%td",
                    i - 2, demangled, offset);
                free(demangled);
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "  %2d: %s", i - 2, symbols[i]);
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "  %2d: %s", i - 2, symbols[i]);
        }
    }

    free(symbols);
}
#else
void printBacktrace() {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "  (backtrace not available on this platform)");
}
#endif

void crashSignalHandler(int sig) {
    // Prevent recursive crashes
    signal(sig, SIG_DFL);

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "");
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "========================================");
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CRASH: %s", getSignalName(sig));
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "========================================");
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Backtrace:");

    printBacktrace();

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "========================================");
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "");

    // Re-raise signal to get default behavior (core dump, etc.)
    raise(sig);
}

} // anonymous namespace

void installCrashHandler() {
    signal(SIGSEGV, crashSignalHandler);
    signal(SIGABRT, crashSignalHandler);
    signal(SIGFPE,  crashSignalHandler);
    signal(SIGILL,  crashSignalHandler);
    signal(SIGBUS,  crashSignalHandler);

    SDL_Log("Crash handler installed");
}
