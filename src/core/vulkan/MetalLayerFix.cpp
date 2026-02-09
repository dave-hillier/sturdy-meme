#include "MetalLayerFix.h"

// No-op on non-Apple platforms
void ensureMetalLayerOpaque(SDL_Window*) {}
void invalidateWindowBackingStore(SDL_Window*) {}
void disableWindowSnapshotRestoration(SDL_Window*) {}
