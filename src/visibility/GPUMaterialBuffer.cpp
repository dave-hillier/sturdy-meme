#include "GPUMaterialBuffer.h"
#include "MaterialRegistry.h"

#include <SDL3/SDL_log.h>
#include <cstring>
#include <algorithm>

std::unique_ptr<GPUMaterialBuffer> GPUMaterialBuffer::create(const InitInfo& info) {
    auto buffer = std::make_unique<GPUMaterialBuffer>(ConstructToken{});
    if (!buffer->initInternal(info)) {
        return nullptr;
    }
    return buffer;
}

bool GPUMaterialBuffer::initInternal(const InitInfo& info) {
    allocator_ = info.allocator;
    maxMaterials_ = info.maxMaterials;

    VkDeviceSize bufferSize = maxMaterials_ * sizeof(GPUMaterial);

    if (!VmaBufferFactory::createStorageBufferHostWritable(allocator_, bufferSize, buffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GPUMaterialBuffer: Failed to create storage buffer (%u materials)",
                     maxMaterials_);
        return false;
    }

    // Map the buffer persistently
    VmaAllocationInfo allocInfo{};
    vmaGetAllocationInfo(allocator_, buffer_.get_deleter().allocation, &allocInfo);
    mappedPtr_ = allocInfo.pMappedData;

    if (!mappedPtr_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GPUMaterialBuffer: Buffer not mapped");
        return false;
    }

    // Zero-initialize
    memset(mappedPtr_, 0, bufferSize);

    SDL_Log("GPUMaterialBuffer: Created with capacity for %u materials (%zu bytes)",
            maxMaterials_, static_cast<size_t>(bufferSize));
    return true;
}

bool GPUMaterialBuffer::uploadMaterials(const std::vector<GPUMaterial>& materials) {
    if (!mappedPtr_) return false;

    uint32_t count = static_cast<uint32_t>(
        std::min(materials.size(), static_cast<size_t>(maxMaterials_)));

    VkDeviceSize bytes = count * sizeof(GPUMaterial);
    memcpy(mappedPtr_, materials.data(), bytes);
    vmaFlushAllocation(allocator_, buffer_.get_deleter().allocation, 0, bytes);

    materialCount_ = count;
    return true;
}

bool GPUMaterialBuffer::uploadFromRegistry(const MaterialRegistry& registry) {
    if (!mappedPtr_) return false;

    uint32_t count = static_cast<uint32_t>(
        std::min(registry.getMaterialCount(), static_cast<size_t>(maxMaterials_)));

    std::vector<GPUMaterial> materials;
    materials.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        const auto* def = registry.getMaterial(i);
        GPUMaterial gpuMat{};

        if (def) {
            // Base color defaults to white (texture will provide actual color)
            gpuMat.baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            gpuMat.roughness = def->roughness;
            gpuMat.metallic = def->metallic;
            gpuMat.normalScale = 1.0f;
            gpuMat.aoStrength = 1.0f;

            // Texture indices default to UINT32_MAX (no texture)
            // These will be set when a texture array is populated
            gpuMat.albedoTexIndex = ~0u;
            gpuMat.normalTexIndex = ~0u;
            gpuMat.roughnessMetallicTexIndex = ~0u;
            gpuMat.flags = 0;
        } else {
            // Default material (gray, mid-roughness)
            gpuMat.baseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
            gpuMat.roughness = 0.5f;
            gpuMat.metallic = 0.0f;
            gpuMat.normalScale = 1.0f;
            gpuMat.aoStrength = 1.0f;
            gpuMat.albedoTexIndex = ~0u;
            gpuMat.normalTexIndex = ~0u;
            gpuMat.roughnessMetallicTexIndex = ~0u;
            gpuMat.flags = 0;
        }

        materials.push_back(gpuMat);
    }

    return uploadMaterials(materials);
}

bool GPUMaterialBuffer::uploadFromRegistry(const MaterialRegistry& registry,
                                            const VisibilityBuffer& visBuf) {
    if (!mappedPtr_) return false;

    uint32_t count = static_cast<uint32_t>(
        std::min(registry.getMaterialCount(), static_cast<size_t>(maxMaterials_)));

    std::vector<GPUMaterial> materials;
    materials.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        const auto* def = registry.getMaterial(i);
        GPUMaterial gpuMat{};

        if (def) {
            gpuMat.baseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            gpuMat.roughness = def->roughness;
            gpuMat.metallic = def->metallic;
            gpuMat.normalScale = 1.0f;
            gpuMat.aoStrength = 1.0f;

            // Look up texture layers in the unified texture array
            gpuMat.albedoTexIndex = def->diffuse
                ? visBuf.getTextureLayerIndex(def->diffuse) : ~0u;
            gpuMat.normalTexIndex = def->normal
                ? visBuf.getTextureLayerIndex(def->normal) : ~0u;
            // Use roughness map as roughness-metallic packed texture
            // (glTF convention: green=roughness, blue=metallic)
            gpuMat.roughnessMetallicTexIndex = def->roughnessMap
                ? visBuf.getTextureLayerIndex(def->roughnessMap) : ~0u;
            gpuMat.flags = 0;
        } else {
            gpuMat.baseColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
            gpuMat.roughness = 0.5f;
            gpuMat.metallic = 0.0f;
            gpuMat.normalScale = 1.0f;
            gpuMat.aoStrength = 1.0f;
            gpuMat.albedoTexIndex = ~0u;
            gpuMat.normalTexIndex = ~0u;
            gpuMat.roughnessMetallicTexIndex = ~0u;
            gpuMat.flags = 0;
        }

        materials.push_back(gpuMat);
    }

    return uploadMaterials(materials);
}

bool GPUMaterialBuffer::setMaterial(uint32_t index, const GPUMaterial& material) {
    if (!mappedPtr_ || index >= maxMaterials_) return false;

    auto* dst = static_cast<GPUMaterial*>(mappedPtr_) + index;
    memcpy(dst, &material, sizeof(GPUMaterial));
    vmaFlushAllocation(allocator_, buffer_.get_deleter().allocation,
                       index * sizeof(GPUMaterial), sizeof(GPUMaterial));

    materialCount_ = std::max(materialCount_, index + 1);
    return true;
}
