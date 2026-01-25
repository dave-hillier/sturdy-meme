#pragma once

// Install signal handlers to print crash backtraces to console
// Call this early in main() before any other initialization
void installCrashHandler();
