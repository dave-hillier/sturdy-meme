#pragma once

#include <vulkan/vulkan.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include "MaterialComponents.h"
#include "ComposedMaterialUBO.h"

class DescriptorManager;
class Texture;
class VulkanContext;

namespace material {

/**
 * ComposedMaterialRegistry - Material management with composable components
 *
 * Extends the MaterialRegistry pattern to support composed materials with
 * multiple components (liquid, weathering, subsurface, etc.). Uses RAII
 * for GPU resource management.
 *
 * Design principles:
 * - Composition over inheritance
 * - RAII for all GPU resources via unique_ptr
 * - Support for both simple and composed materials
 * - Efficient UBO management with per-frame updates
 *
 * Usage:
 *   ComposedMaterialRegistry registry;
 *   auto wetRock = registry.registerMaterial("wet_rock",
 *       ComposedMaterial()
 *           .withSurface(SurfaceComponent::dielectric(glm::vec3(0.5f), 0.8f))
 *           .withLiquid(LiquidComponent::wetSurface(0.7f))
 *   );
 *   registry.createGPUResources(context, framesInFlight);
 *   ...
 *   registry.updateUBO(materialId, frameIndex);
 */
class ComposedMaterialRegistry {
public:
    using MaterialId = uint32_t;
    static constexpr MaterialId INVALID_MATERIAL_ID = ~0u;

    /**
     * ComposedMaterialDef - Definition of a composed material
     */
    struct ComposedMaterialDef {
        std::string name;
        ComposedMaterial material;

        // Optional textures (override component scalar values)
        const Texture* diffuse = nullptr;
        const Texture* normal = nullptr;
        const Texture* roughness = nullptr;
        const Texture* metallic = nullptr;
        const Texture* ao = nullptr;
        const Texture* height = nullptr;
        const Texture* emissive = nullptr;

        // Liquid-specific textures
        const Texture* flowMap = nullptr;
        const Texture* foamTexture = nullptr;
    };

    ComposedMaterialRegistry() = default;
    ~ComposedMaterialRegistry();

    // Non-copyable, movable
    ComposedMaterialRegistry(const ComposedMaterialRegistry&) = delete;
    ComposedMaterialRegistry& operator=(const ComposedMaterialRegistry&) = delete;
    ComposedMaterialRegistry(ComposedMaterialRegistry&&) noexcept = default;
    ComposedMaterialRegistry& operator=(ComposedMaterialRegistry&&) noexcept = default;

    /**
     * Register a composed material
     * @param def Material definition with components and textures
     * @return Unique material ID
     */
    MaterialId registerMaterial(const ComposedMaterialDef& def);

    /**
     * Register a material with just a ComposedMaterial (no textures)
     */
    MaterialId registerMaterial(const std::string& name, const ComposedMaterial& material);

    /**
     * Register a simple surface-only material
     */
    MaterialId registerMaterial(const std::string& name,
                                 const SurfaceComponent& surface,
                                 const Texture* diffuse = nullptr,
                                 const Texture* normal = nullptr);

    /**
     * Get material ID by name
     */
    MaterialId getMaterialId(const std::string& name) const;

    /**
     * Get material definition (const)
     */
    const ComposedMaterialDef* getMaterial(MaterialId id) const;

    /**
     * Get mutable material for runtime modification
     * Note: Call markDirty(id) after modifying
     */
    ComposedMaterialDef* getMaterialMutable(MaterialId id);

    /**
     * Mark a material as needing UBO update
     */
    void markDirty(MaterialId id);

    /**
     * Create GPU resources (UBOs, buffers)
     * Must be called after all materials are registered
     */
    void createGPUResources(vk::Device device, uint32_t framesInFlight);

    /**
     * Update UBO for a specific material/frame
     * Uploads material data to GPU if dirty
     */
    void updateUBO(MaterialId id, uint32_t frameIndex);

    /**
     * Update all dirty UBOs for the current frame
     */
    void updateAllUBOs(uint32_t frameIndex);

    /**
     * Update animation time for all materials
     */
    void updateTime(float deltaTime);

    /**
     * Get UBO buffer for binding
     */
    vk::Buffer getUBOBuffer(MaterialId id, uint32_t frameIndex) const;

    /**
     * Get number of registered materials
     */
    size_t getMaterialCount() const { return m_materials.size(); }

    /**
     * Check if GPU resources have been created
     */
    bool hasGPUResources() const { return !m_uboBuffers.empty(); }

    /**
     * Set global weather parameters (affects weathering components)
     */
    void setGlobalWeather(float wetness, float snowCoverage);

    /**
     * Cleanup GPU resources (called automatically in destructor)
     */
    void cleanup();

private:
    /**
     * RAII wrapper for Vulkan buffer + memory
     */
    struct GPUBuffer {
        vk::Device device;
        vk::Buffer buffer;
        vk::DeviceMemory memory;
        void* mappedPtr = nullptr;
        size_t size = 0;

        GPUBuffer() = default;
        ~GPUBuffer();

        // Non-copyable
        GPUBuffer(const GPUBuffer&) = delete;
        GPUBuffer& operator=(const GPUBuffer&) = delete;

        // Movable
        GPUBuffer(GPUBuffer&& other) noexcept;
        GPUBuffer& operator=(GPUBuffer&& other) noexcept;

        void create(vk::Device dev, size_t bufferSize);
        void destroy();
        void upload(const void* data, size_t dataSize);
    };

    // Material storage
    std::vector<ComposedMaterialDef> m_materials;
    std::unordered_map<std::string, MaterialId> m_nameToId;

    // GPU resources: m_uboBuffers[materialId][frameIndex]
    std::vector<std::vector<std::unique_ptr<GPUBuffer>>> m_uboBuffers;

    // Dirty tracking: m_dirtyFlags[materialId]
    std::vector<bool> m_dirtyFlags;

    // Global state
    float m_globalWetness = 0.0f;
    float m_globalSnowCoverage = 0.0f;
    float m_animTime = 0.0f;

    vk::Device m_device;
    uint32_t m_framesInFlight = 0;

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
};

} // namespace material
