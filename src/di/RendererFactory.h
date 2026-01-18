#pragma once

#include <fruit/fruit.h>
#include <memory>
#include <SDL3/SDL.h>

// Forward declarations
class Renderer;
class VulkanContext;

namespace di {

/**
 * RendererFactory - Creates Renderer using Fruit DI for dependency management
 *
 * This factory provides a bridge between the existing Renderer::create() pattern
 * and the Fruit DI system. It allows gradual migration:
 *
 * 1. Immediate: Use RendererFactory::create() which internally uses DI
 *    to manage infrastructure, then wires it into Renderer.
 *
 * 2. Future: Refactor Renderer to accept injected dependencies directly,
 *    making it fully DI-managed.
 *
 * Benefits:
 * - Unified initialization through DI container
 * - Consistent configuration through AppConfig
 * - Testable - can provide mock implementations
 * - Extensible - easy to add new dependencies
 */
class RendererFactory {
public:
    /**
     * Configuration for renderer creation via DI
     */
    struct Config {
        SDL_Window* window = nullptr;
        std::string resourcePath;

        // Optional feature toggles
        bool enableTerrain = true;
        bool enableWater = true;
        bool enableVegetation = true;

        // Performance settings
        uint32_t framesInFlight = 3;
        uint32_t threadCount = 0;  // 0 = auto-detect
    };

    /**
     * Create a Renderer using Fruit DI for dependency management.
     *
     * This method:
     * 1. Creates the Fruit injector with all dependencies
     * 2. Extracts VulkanContext and infrastructure from injector
     * 3. Creates Renderer with pre-initialized VulkanContext
     *
     * The injector lifetime is tied to the returned RendererHandle.
     */
    struct RendererHandle {
        std::unique_ptr<Renderer> renderer;

        // The injector keeps DI-managed objects alive
        // This is an opaque handle - we use type erasure
        std::shared_ptr<void> injectorHandle;

        // Convenience accessors
        Renderer* operator->() { return renderer.get(); }
        Renderer& operator*() { return *renderer; }
        explicit operator bool() const { return renderer != nullptr; }
    };

    static RendererHandle create(const Config& config);

    /**
     * Alternative: Create Renderer with an existing VulkanContext
     * (useful when VulkanContext is managed externally)
     */
    static std::unique_ptr<Renderer> createWithContext(
        std::unique_ptr<VulkanContext> vulkanContext,
        const std::string& resourcePath
    );
};

} // namespace di
