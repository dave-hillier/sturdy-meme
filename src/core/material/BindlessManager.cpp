#include "BindlessManager.h"
#include "MaterialRegistry.h"
#include "../vulkan/VulkanContext.h"
#include <SDL3/SDL_log.h>

bool BindlessManager::init(VulkanContext& context, uint32_t maxTextures, uint32_t framesInFlight) {
    if (!context.hasDescriptorIndexing()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "BindlessManager: Descriptor indexing not available, skipping init");
        return false;
    }

    const auto& device = context.getRaiiDevice();

    // Cap to device limit
    maxTextures_ = std::min(maxTextures, context.getMaxBindlessTextures());
    framesInFlight_ = framesInFlight;

    SDL_Log("BindlessManager: Initializing with max %u textures, %u materials, %u frames",
            maxTextures_, maxMaterials_, framesInFlight_);

    if (!createTextureSetLayout(device)) return false;
    if (!createMaterialSetLayout(device)) return false;
    if (!createDescriptorPool(device, framesInFlight)) return false;
    if (!allocateDescriptorSets(device, framesInFlight)) return false;
    if (!createMaterialBuffers(context.getAllocator(), framesInFlight)) return false;

    initialized_ = true;
    SDL_Log("BindlessManager: Initialized successfully");
    return true;
}

bool BindlessManager::createTextureSetLayout(const vk::raii::Device& device) {
    // Single binding: variable-count array of combined image samplers
    auto binding = vk::DescriptorSetLayoutBinding{}
        .setBinding(0)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(maxTextures_)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    auto bindingFlags = vk::DescriptorBindingFlags{
        vk::DescriptorBindingFlagBits::ePartiallyBound |
        vk::DescriptorBindingFlagBits::eUpdateAfterBind |
        vk::DescriptorBindingFlagBits::eVariableDescriptorCount
    };

    auto flagsInfo = vk::DescriptorSetLayoutBindingFlagsCreateInfo{}
        .setBindingCount(1)
        .setPBindingFlags(&bindingFlags);

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setFlags(vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool)
        .setBindingCount(1)
        .setPBindings(&binding)
        .setPNext(&flagsInfo);

    try {
        textureSetLayout_.emplace(device, layoutInfo);
        SDL_Log("BindlessManager: Created texture set layout (max %u textures)", maxTextures_);
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "BindlessManager: Failed to create texture set layout: %s", e.what());
        return false;
    }
}

bool BindlessManager::createMaterialSetLayout(const vk::raii::Device& device) {
    // Single binding: SSBO for material data array
    auto binding = vk::DescriptorSetLayoutBinding{}
        .setBinding(0)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindingCount(1)
        .setPBindings(&binding);

    try {
        materialSetLayout_.emplace(device, layoutInfo);
        SDL_Log("BindlessManager: Created material set layout");
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "BindlessManager: Failed to create material set layout: %s", e.what());
        return false;
    }
}

bool BindlessManager::createDescriptorPool(const vk::raii::Device& device, uint32_t framesInFlight) {
    // Pool sizes: textures (combined image samplers) + material SSBOs
    std::array<vk::DescriptorPoolSize, 2> poolSizes = {{
        {vk::DescriptorType::eCombinedImageSampler, maxTextures_ * framesInFlight},
        {vk::DescriptorType::eStorageBuffer, framesInFlight}
    }};

    auto poolInfo = vk::DescriptorPoolCreateInfo{}
        .setFlags(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind)
        .setMaxSets(framesInFlight * 2)  // texture set + material set per frame
        .setPoolSizeCount(static_cast<uint32_t>(poolSizes.size()))
        .setPPoolSizes(poolSizes.data());

    try {
        descriptorPool_.emplace(device, poolInfo);
        return true;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "BindlessManager: Failed to create descriptor pool: %s", e.what());
        return false;
    }
}

bool BindlessManager::allocateDescriptorSets(const vk::raii::Device& device, uint32_t framesInFlight) {
    textureDescSets_.resize(framesInFlight);
    materialDescSets_.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        // Allocate texture descriptor set with variable count
        uint32_t variableCount = maxTextures_;
        auto variableCountInfo = vk::DescriptorSetVariableDescriptorCountAllocateInfo{}
            .setDescriptorSetCount(1)
            .setPDescriptorCounts(&variableCount);

        vk::DescriptorSetLayout textureLayout = **textureSetLayout_;
        auto textureAllocInfo = vk::DescriptorSetAllocateInfo{}
            .setDescriptorPool(**descriptorPool_)
            .setDescriptorSetCount(1)
            .setPSetLayouts(&textureLayout)
            .setPNext(&variableCountInfo);

        try {
            auto sets = device.allocateDescriptorSets(textureAllocInfo);
            textureDescSets_[i] = sets[0];
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "BindlessManager: Failed to allocate texture descriptor set %u: %s", i, e.what());
            return false;
        }

        // Allocate material descriptor set (standard, no variable count)
        vk::DescriptorSetLayout materialLayout = **materialSetLayout_;
        auto materialAllocInfo = vk::DescriptorSetAllocateInfo{}
            .setDescriptorPool(**descriptorPool_)
            .setDescriptorSetCount(1)
            .setPSetLayouts(&materialLayout);

        try {
            auto sets = device.allocateDescriptorSets(materialAllocInfo);
            materialDescSets_[i] = sets[0];
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "BindlessManager: Failed to allocate material descriptor set %u: %s", i, e.what());
            return false;
        }
    }

    SDL_Log("BindlessManager: Allocated %u texture + %u material descriptor sets",
            framesInFlight, framesInFlight);
    return true;
}

bool BindlessManager::createMaterialBuffers(VmaAllocator allocator, uint32_t framesInFlight) {
    materialBuffers_.resize(framesInFlight);
    materialBufferMaps_.resize(framesInFlight, nullptr);

    VkDeviceSize bufferSize = sizeof(GPUMaterialData) * maxMaterials_;

    for (uint32_t i = 0; i < framesInFlight; i++) {
        auto bufferInfo = vk::BufferCreateInfo{}
            .setSize(bufferSize)
            .setUsage(vk::BufferUsageFlagBits::eStorageBuffer);

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        if (!VmaBuffer::create(allocator, bufferInfo, allocInfo, materialBuffers_[i])) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "BindlessManager: Failed to create material buffer %u", i);
            return false;
        }

        // Persistently map the buffer
        materialBufferMaps_[i] = materialBuffers_[i].map();
        if (!materialBufferMaps_[i]) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "BindlessManager: Failed to map material buffer %u", i);
            return false;
        }
    }

    SDL_Log("BindlessManager: Created %u material buffers (%zu bytes each)",
            framesInFlight, static_cast<size_t>(bufferSize));
    return true;
}

void BindlessManager::updateTextureDescriptors(vk::Device device, const TextureRegistry& registry,
                                               uint32_t frameIndex) {
    if (!initialized_ || frameIndex >= framesInFlight_) return;

    uint32_t count = registry.getArraySize();
    if (count == 0) return;
    if (count > maxTextures_) count = maxTextures_;

    // Build image info array for all registered textures
    std::vector<vk::DescriptorImageInfo> imageInfos(count);
    for (uint32_t i = 0; i < count; i++) {
        VkImageView view = registry.getImageView(i);
        VkSampler sampler = registry.getSampler(i);

        imageInfos[i] = vk::DescriptorImageInfo{}
            .setSampler(sampler)
            .setImageView(view)
            .setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    auto write = vk::WriteDescriptorSet{}
        .setDstSet(textureDescSets_[frameIndex])
        .setDstBinding(0)
        .setDstArrayElement(0)
        .setDescriptorCount(count)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setPImageInfo(imageInfos.data());

    device.updateDescriptorSets(write, {});
}

void BindlessManager::uploadMaterialData(vk::Device device, const MaterialRegistry& registry,
                                         uint32_t frameIndex) {
    if (!initialized_ || frameIndex >= framesInFlight_) return;

    uint32_t materialCount = static_cast<uint32_t>(registry.getMaterialCount());
    if (materialCount == 0) return;
    if (materialCount > maxMaterials_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "BindlessManager: Material count %u exceeds max %u, clamping",
            materialCount, maxMaterials_);
        materialCount = maxMaterials_;
    }

    auto* gpuData = static_cast<GPUMaterialData*>(materialBufferMaps_[frameIndex]);

    for (uint32_t i = 0; i < materialCount; i++) {
        const auto* mat = registry.getMaterial(i);
        if (!mat) continue;

        GPUMaterialData& gpu = gpuData[i];
        gpu.albedoIndex = mat->diffuseHandle.isValid()
            ? mat->diffuseHandle.index : TextureRegistry::PLACEHOLDER_WHITE;
        gpu.normalIndex = mat->normalHandle.isValid()
            ? mat->normalHandle.index : TextureRegistry::PLACEHOLDER_NORMAL;
        gpu.roughnessIndex = mat->roughnessHandle.isValid()
            ? mat->roughnessHandle.index : TextureRegistry::PLACEHOLDER_WHITE;
        gpu.metallicIndex = mat->metallicHandle.isValid()
            ? mat->metallicHandle.index : TextureRegistry::PLACEHOLDER_BLACK;
        gpu.aoIndex = mat->aoHandle.isValid()
            ? mat->aoHandle.index : TextureRegistry::PLACEHOLDER_WHITE;
        gpu.heightIndex = mat->heightHandle.isValid()
            ? mat->heightHandle.index : TextureRegistry::PLACEHOLDER_BLACK;
        gpu.emissiveIndex = TextureRegistry::PLACEHOLDER_BLACK;
        gpu._pad0 = 0;
        gpu.roughness = mat->roughness;
        gpu.metallic = mat->metallic;
        gpu.emissiveStrength = 0.0f;
        gpu.alphaCutoff = 0.0f;
    }

    // Update the material SSBO descriptor to point to this frame's buffer
    vk::DescriptorBufferInfo bufInfo{};
    bufInfo.buffer = materialBuffers_[frameIndex].get();
    bufInfo.offset = 0;
    bufInfo.range = sizeof(GPUMaterialData) * materialCount;

    auto write = vk::WriteDescriptorSet{}
        .setDstSet(materialDescSets_[frameIndex])
        .setDstBinding(0)
        .setDstArrayElement(0)
        .setDescriptorCount(1)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setPBufferInfo(&bufInfo);

    device.updateDescriptorSets(write, {});
}

void BindlessManager::bind(vk::CommandBuffer cmd, vk::PipelineLayout layout,
                           vk::PipelineBindPoint bindPoint, uint32_t frameIndex) const {
    if (!initialized_ || frameIndex >= framesInFlight_) return;

    // Bind texture set at set index 1
    cmd.bindDescriptorSets(bindPoint, layout, TEXTURE_SET_INDEX,
                          textureDescSets_[frameIndex], {});

    // Bind material set at set index 2
    cmd.bindDescriptorSets(bindPoint, layout, MATERIAL_SET_INDEX,
                          materialDescSets_[frameIndex], {});
}

void BindlessManager::cleanup() {
    // Unmap and destroy material buffers
    for (uint32_t i = 0; i < materialBufferMaps_.size(); i++) {
        if (materialBufferMaps_[i] && materialBuffers_[i]) {
            materialBuffers_[i].unmap();
            materialBufferMaps_[i] = nullptr;
        }
    }
    materialBuffers_.clear();
    materialBufferMaps_.clear();
    materialDescSets_.clear();
    textureDescSets_.clear();

    descriptorPool_.reset();
    materialSetLayout_.reset();
    textureSetLayout_.reset();

    initialized_ = false;
}
