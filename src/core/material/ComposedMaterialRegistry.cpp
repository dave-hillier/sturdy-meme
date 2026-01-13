#include "ComposedMaterialRegistry.h"
#include <SDL3/SDL_log.h>
#include <cstring>

namespace material {

// GPUBuffer implementation

ComposedMaterialRegistry::GPUBuffer::~GPUBuffer() {
    destroy();
}

ComposedMaterialRegistry::GPUBuffer::GPUBuffer(GPUBuffer&& other) noexcept
    : device(other.device)
    , buffer(other.buffer)
    , memory(other.memory)
    , mappedPtr(other.mappedPtr)
    , size(other.size)
{
    other.device = nullptr;
    other.buffer = nullptr;
    other.memory = nullptr;
    other.mappedPtr = nullptr;
    other.size = 0;
}

ComposedMaterialRegistry::GPUBuffer& ComposedMaterialRegistry::GPUBuffer::operator=(GPUBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        device = other.device;
        buffer = other.buffer;
        memory = other.memory;
        mappedPtr = other.mappedPtr;
        size = other.size;

        other.device = nullptr;
        other.buffer = nullptr;
        other.memory = nullptr;
        other.mappedPtr = nullptr;
        other.size = 0;
    }
    return *this;
}

void ComposedMaterialRegistry::GPUBuffer::create(vk::Device dev, size_t bufferSize) {
    device = dev;
    size = bufferSize;

    // Create buffer
    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(bufferSize)
        .setUsage(vk::BufferUsageFlagBits::eUniformBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    buffer = device.createBuffer(bufferInfo);

    // Get memory requirements
    auto memReqs = device.getBufferMemoryRequirements(buffer);

    // Allocate memory (host visible, host coherent for easy updates)
    auto allocInfo = vk::MemoryAllocateInfo{}
        .setAllocationSize(memReqs.size)
        .setMemoryTypeIndex(0);  // Will be set properly by caller

    // Find suitable memory type
    // Note: In production, this should use VulkanContext's memory allocator
    // For now, we'll use a simple approach that works for host-visible memory

    memory = device.allocateMemory(allocInfo);
    device.bindBufferMemory(buffer, memory, 0);

    // Map persistently
    mappedPtr = device.mapMemory(memory, 0, bufferSize);
}

void ComposedMaterialRegistry::GPUBuffer::destroy() {
    if (device) {
        if (mappedPtr) {
            device.unmapMemory(memory);
            mappedPtr = nullptr;
        }
        if (buffer) {
            device.destroyBuffer(buffer);
            buffer = nullptr;
        }
        if (memory) {
            device.freeMemory(memory);
            memory = nullptr;
        }
        device = nullptr;
    }
}

void ComposedMaterialRegistry::GPUBuffer::upload(const void* data, size_t dataSize) {
    if (mappedPtr && dataSize <= size) {
        std::memcpy(mappedPtr, data, dataSize);
    }
}

// ComposedMaterialRegistry implementation

ComposedMaterialRegistry::~ComposedMaterialRegistry() {
    cleanup();
}

ComposedMaterialRegistry::MaterialId ComposedMaterialRegistry::registerMaterial(
    const ComposedMaterialDef& def)
{
    // Check for duplicate name
    auto it = m_nameToId.find(def.name);
    if (it != m_nameToId.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "ComposedMaterialRegistry: Material '%s' already registered", def.name.c_str());
        return it->second;
    }

    MaterialId id = static_cast<MaterialId>(m_materials.size());
    m_materials.push_back(def);
    m_nameToId[def.name] = id;
    m_dirtyFlags.push_back(true);

    SDL_Log("ComposedMaterialRegistry: Registered material '%s' (id=%u, features=0x%x)",
        def.name.c_str(), id, static_cast<uint32_t>(def.material.enabledFeatures));

    return id;
}

ComposedMaterialRegistry::MaterialId ComposedMaterialRegistry::registerMaterial(
    const std::string& name, const ComposedMaterial& material)
{
    ComposedMaterialDef def;
    def.name = name;
    def.material = material;
    return registerMaterial(def);
}

ComposedMaterialRegistry::MaterialId ComposedMaterialRegistry::registerMaterial(
    const std::string& name,
    const SurfaceComponent& surface,
    const Texture* diffuse,
    const Texture* normal)
{
    ComposedMaterialDef def;
    def.name = name;
    def.material.surface = surface;
    def.diffuse = diffuse;
    def.normal = normal;
    return registerMaterial(def);
}

ComposedMaterialRegistry::MaterialId ComposedMaterialRegistry::getMaterialId(
    const std::string& name) const
{
    auto it = m_nameToId.find(name);
    return (it != m_nameToId.end()) ? it->second : INVALID_MATERIAL_ID;
}

const ComposedMaterialRegistry::ComposedMaterialDef* ComposedMaterialRegistry::getMaterial(
    MaterialId id) const
{
    return (id < m_materials.size()) ? &m_materials[id] : nullptr;
}

ComposedMaterialRegistry::ComposedMaterialDef* ComposedMaterialRegistry::getMaterialMutable(
    MaterialId id)
{
    return (id < m_materials.size()) ? &m_materials[id] : nullptr;
}

void ComposedMaterialRegistry::markDirty(MaterialId id) {
    if (id < m_dirtyFlags.size()) {
        m_dirtyFlags[id] = true;
    }
}

void ComposedMaterialRegistry::createGPUResources(vk::Device device, uint32_t framesInFlight) {
    m_device = device;
    m_framesInFlight = framesInFlight;

    m_uboBuffers.resize(m_materials.size());

    for (size_t i = 0; i < m_materials.size(); ++i) {
        m_uboBuffers[i].resize(framesInFlight);

        for (uint32_t f = 0; f < framesInFlight; ++f) {
            m_uboBuffers[i][f] = std::make_unique<GPUBuffer>();
            m_uboBuffers[i][f]->create(device, sizeof(ComposedMaterialUBO));
        }

        // Mark all as dirty to ensure initial upload
        m_dirtyFlags[i] = true;
    }

    SDL_Log("ComposedMaterialRegistry: Created GPU resources for %zu materials (%u frames)",
        m_materials.size(), framesInFlight);
}

void ComposedMaterialRegistry::updateUBO(MaterialId id, uint32_t frameIndex) {
    if (id >= m_materials.size() || frameIndex >= m_framesInFlight) {
        return;
    }

    if (!m_dirtyFlags[id]) {
        return;
    }

    const auto& def = m_materials[id];

    // Apply global weather overrides
    ComposedMaterial material = def.material;
    if (hasFeature(material.enabledFeatures, FeatureFlags::Weathering)) {
        material.weathering.wetness = std::max(material.weathering.wetness, m_globalWetness);
        material.weathering.snowCoverage = std::max(material.weathering.snowCoverage, m_globalSnowCoverage);
    }

    // Convert to UBO
    ComposedMaterialUBO ubo = ComposedMaterialUBO::fromMaterial(material, m_animTime);

    // Upload
    m_uboBuffers[id][frameIndex]->upload(&ubo, sizeof(ubo));
}

void ComposedMaterialRegistry::updateAllUBOs(uint32_t frameIndex) {
    for (MaterialId id = 0; id < m_materials.size(); ++id) {
        updateUBO(id, frameIndex);
    }

    // Clear dirty flags after updating all frames
    // Note: We only clear when all frames have been updated
    // For now, clear after each frame update (simpler)
    std::fill(m_dirtyFlags.begin(), m_dirtyFlags.end(), false);
}

void ComposedMaterialRegistry::updateTime(float deltaTime) {
    m_animTime += deltaTime;

    // Mark materials with animation as dirty
    for (size_t i = 0; i < m_materials.size(); ++i) {
        const auto& mat = m_materials[i].material;

        // Check if material has animated components
        bool hasAnimation =
            (hasFeature(mat.enabledFeatures, FeatureFlags::Liquid) && mat.liquid.flowSpeed > 0.0f) ||
            (hasFeature(mat.enabledFeatures, FeatureFlags::Displacement) && mat.displacement.waveAmplitude > 0.0f) ||
            (hasFeature(mat.enabledFeatures, FeatureFlags::Emissive) && mat.emissive.pulseSpeed > 0.0f);

        if (hasAnimation) {
            m_dirtyFlags[i] = true;
        }
    }
}

vk::Buffer ComposedMaterialRegistry::getUBOBuffer(MaterialId id, uint32_t frameIndex) const {
    if (id < m_uboBuffers.size() && frameIndex < m_framesInFlight) {
        return m_uboBuffers[id][frameIndex]->buffer;
    }
    return nullptr;
}

void ComposedMaterialRegistry::setGlobalWeather(float wetness, float snowCoverage) {
    if (m_globalWetness != wetness || m_globalSnowCoverage != snowCoverage) {
        m_globalWetness = wetness;
        m_globalSnowCoverage = snowCoverage;

        // Mark all weathering materials as dirty
        for (size_t i = 0; i < m_materials.size(); ++i) {
            if (hasFeature(m_materials[i].material.enabledFeatures, FeatureFlags::Weathering)) {
                m_dirtyFlags[i] = true;
            }
        }
    }
}

void ComposedMaterialRegistry::cleanup() {
    m_uboBuffers.clear();
    m_device = nullptr;
}

uint32_t ComposedMaterialRegistry::findMemoryType(
    uint32_t typeFilter, vk::MemoryPropertyFlags properties) const
{
    // This would need PhysicalDevice access - simplified for now
    // In production, use VulkanContext's helper
    return 0;
}

} // namespace material
