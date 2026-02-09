#include "MetalLayerFix.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_log.h>

#import <QuartzCore/QuartzCore.h>
#import <AppKit/AppKit.h>

static NSWindow* getCocoaWindow(SDL_Window* window) {
    if (!window) return nil;
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (!props) return nil;
    return (__bridge NSWindow*)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
}

void ensureMetalLayerOpaque(SDL_Window* window) {
    NSWindow* nsWindow = getCocoaWindow(window);
    if (!nsWindow) return;

    [nsWindow setOpaque:YES];
    [nsWindow setBackgroundColor:[NSColor blackColor]];

    // Disable snapshot restoration at swapchain creation time
    disableWindowSnapshotRestoration(window);
}

void disableWindowSnapshotRestoration(SDL_Window* window) {
    NSWindow* nsWindow = getCocoaWindow(window);
    if (!nsWindow) return;

    // Prevent macOS from caching window content for state restoration
    // This stops the Window Server from creating a backing store snapshot
    // that can appear as a ghost image behind the Metal layer
    [nsWindow setRestorable:NO];
    [nsWindow disableSnapshotRestoration];

    SDL_Log("MetalLayerFix: Disabled window snapshot restoration");
}

void invalidateWindowBackingStore(SDL_Window* window) {
    NSWindow* nsWindow = getCocoaWindow(window);
    if (!nsWindow) return;

    NSView* contentView = [nsWindow contentView];
    if (!contentView) return;

    // Force the window to completely redisplay, clearing any cached content
    // This removes Window Server cached snapshots that appear as ghost images
    [nsWindow setBackgroundColor:[NSColor blackColor]];

    // Mark the entire content view as needing redisplay
    [contentView setNeedsDisplay:YES];

    // Force immediate display update
    [nsWindow display];

    // Invalidate the window shadow (which can cache old content)
    [nsWindow invalidateShadow];

    // Force the window to update its backing store
    [nsWindow update];

    SDL_Log("MetalLayerFix: Invalidated window backing store on focus gain");
}
