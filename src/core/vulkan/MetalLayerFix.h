#pragma once

struct SDL_Window;

// On macOS, force the Metal layer to be fully opaque and disable
// window snapshot restoration to prevent ghost frame caching.
void ensureMetalLayerOpaque(SDL_Window* window);

// On macOS, force the window to invalidate its backing store and redisplay.
// Called on focus gain to clear any cached window snapshots.
void invalidateWindowBackingStore(SDL_Window* window);

// On macOS, disable the window's restorable state and snapshot caching.
// Called once during init to prevent macOS from caching window content.
void disableWindowSnapshotRestoration(SDL_Window* window);
