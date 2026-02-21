#include "GPUInference.h"
#include "../core/ShaderLoader.h"
#include <SDL3/SDL_log.h>
#include <cstring>
#include <array>

namespace ml {

GPUInference::~GPUInference() {
    destroy();
}

bool GPUInference::init(VkDevice device, VmaAllocator allocator, const Config& cfg) {
    device_ = device;
    allocator_ = allocator;
    config_ = cfg;

    // Create GPU buffers
    size_t latentBufSize = cfg.maxNPCs * cfg.latentDim * sizeof(float);
    size_t obsBufSize = cfg.maxNPCs * cfg.obsDim * sizeof(float);
    size_t actionBufSize = cfg.maxNPCs * cfg.actionDim * sizeof(float);

    if (!createBuffer(latentBuffer_, latentBufSize,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU)) return false;
    if (!createBuffer(obsBuffer_, obsBufSize,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU)) return false;
    if (!createBuffer(actionBuffer_, actionBufSize,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VMA_MEMORY_USAGE_GPU_TO_CPU)) return false;

    // Descriptor set layout: 5 storage buffers
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t i = 0; i < 5; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 5;
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUInference: failed to create descriptor set layout");
        return false;
    }

    // Descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 5;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUInference: failed to create descriptor pool");
        return false;
    }

    if (!createDescriptorSet()) return false;

    // Push constant range
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(InferencePushConstants);

    // Pipeline layout
    VkPipelineLayoutCreateInfo plInfo{};
    plInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plInfo.setLayoutCount = 1;
    plInfo.pSetLayouts = &descriptorSetLayout_;
    plInfo.pushConstantRangeCount = 1;
    plInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(device_, &plInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUInference: failed to create pipeline layout");
        return false;
    }

    // Load compute shader
    auto shaderModule = ShaderLoader::loadShaderModule(device_, cfg.shaderPath);
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GPUInference: failed to load shader %s", cfg.shaderPath.c_str());
        return false;
    }

    // Specialization constants
    struct SpecData {
        uint32_t numNPCs;
        uint32_t latentDim;
        uint32_t obsDim;
        uint32_t actionDim;
        uint32_t maxHidden;
    } specData = {cfg.maxNPCs, cfg.latentDim, cfg.obsDim, cfg.actionDim, cfg.maxHiddenSize};

    std::array<VkSpecializationMapEntry, 5> specEntries{};
    for (uint32_t i = 0; i < 5; ++i) {
        specEntries[i].constantID = i;
        specEntries[i].offset = i * sizeof(uint32_t);
        specEntries[i].size = sizeof(uint32_t);
    }

    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 5;
    specInfo.pMapEntries = specEntries.data();
    specInfo.dataSize = sizeof(specData);
    specInfo.pData = &specData;

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = *shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.stage.pSpecializationInfo = &specInfo;
    pipelineInfo.layout = pipelineLayout_;

    VkResult result = vkCreateComputePipelines(device_, VK_NULL_HANDLE,
                                                1, &pipelineInfo, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, *shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GPUInference: failed to create compute pipeline");
        return false;
    }

    initialized_ = true;
    SDL_Log("GPUInference: initialized (maxNPCs=%u, latent=%u, obs=%u, action=%u)",
            cfg.maxNPCs, cfg.latentDim, cfg.obsDim, cfg.actionDim);
    return true;
}

bool GPUInference::uploadWeights(const calm::LowLevelController& llc) {
    if (!initialized_) return false;

    std::vector<float> packedWeights;
    std::vector<GPULayerMeta> layerMetas;
    if (!packWeights(llc, packedWeights, layerMetas)) return false;

    size_t weightSize = packedWeights.size() * sizeof(float);
    if (!createBuffer(weightBuffer_, weightSize,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU)) return false;
    uploadToBuffer(weightBuffer_, packedWeights.data(), weightSize);

    // Pack layer metadata as flat uint32 array (5 per layer)
    std::vector<uint32_t> metaFlat;
    for (const auto& m : layerMetas) {
        metaFlat.push_back(m.weightOffset);
        metaFlat.push_back(m.biasOffset);
        metaFlat.push_back(m.inFeatures);
        metaFlat.push_back(m.outFeatures);
        metaFlat.push_back(m.activation);
    }
    size_t metaSize = metaFlat.size() * sizeof(uint32_t);
    if (!createBuffer(layerMetaBuffer_, metaSize,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VMA_MEMORY_USAGE_CPU_TO_GPU)) return false;
    uploadToBuffer(layerMetaBuffer_, metaFlat.data(), metaSize);

    updateDescriptorSet();

    SDL_Log("GPUInference: uploaded weights (%zu layers, %zu floats)",
            layerMetas.size(), packedWeights.size());
    return true;
}

void GPUInference::uploadInputs(const std::vector<float>& latents,
                                 const std::vector<float>& observations,
                                 uint32_t npcCount) {
    if (!initialized_) return;
    uploadToBuffer(latentBuffer_, latents.data(),
                   npcCount * config_.latentDim * sizeof(float));
    uploadToBuffer(obsBuffer_, observations.data(),
                   npcCount * config_.obsDim * sizeof(float));
}

void GPUInference::recordDispatch(VkCommandBuffer cmd, uint32_t npcCount) {
    if (!initialized_ || pipeline_ == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pushConstants_), &pushConstants_);

    vkCmdDispatch(cmd, npcCount, 1, 1);

    // Memory barrier: compute writes -> host reads
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void GPUInference::readBackActions(std::vector<float>& actions, uint32_t npcCount) {
    if (!initialized_) return;
    size_t totalFloats = npcCount * config_.actionDim;
    actions.resize(totalFloats);
    readFromBuffer(actionBuffer_, actions.data(), totalFloats * sizeof(float));
}

void GPUInference::destroy() {
    if (!initialized_ && device_ == VK_NULL_HANDLE) return;

    destroyBuffer(weightBuffer_);
    destroyBuffer(layerMetaBuffer_);
    destroyBuffer(latentBuffer_);
    destroyBuffer(obsBuffer_);
    destroyBuffer(actionBuffer_);

    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }

    initialized_ = false;
}

// --- Buffer helpers ---

bool GPUInference::createBuffer(GPUBuffer& buf, size_t size,
                                 VkBufferUsageFlags usage,
                                 VmaMemoryUsage memUsage) {
    if (buf.buffer != VK_NULL_HANDLE) destroyBuffer(buf);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memUsage;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &buf.buffer, &buf.allocation, nullptr) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GPUInference: failed to create buffer (size=%zu)", size);
        return false;
    }
    buf.size = size;
    return true;
}

void GPUInference::destroyBuffer(GPUBuffer& buf) {
    if (buf.buffer != VK_NULL_HANDLE && allocator_) {
        vmaDestroyBuffer(allocator_, buf.buffer, buf.allocation);
        buf = {};
    }
}

void GPUInference::uploadToBuffer(GPUBuffer& buf, const void* data, size_t size) {
    void* mapped = nullptr;
    vmaMapMemory(allocator_, buf.allocation, &mapped);
    std::memcpy(mapped, data, size);
    vmaUnmapMemory(allocator_, buf.allocation);
    vmaFlushAllocation(allocator_, buf.allocation, 0, size);
}

void GPUInference::readFromBuffer(const GPUBuffer& buf, void* data, size_t size) {
    vmaInvalidateAllocation(allocator_, buf.allocation, 0, size);
    void* mapped = nullptr;
    vmaMapMemory(allocator_, buf.allocation, &mapped);
    std::memcpy(data, mapped, size);
    vmaUnmapMemory(allocator_, buf.allocation);
}

bool GPUInference::createDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout_;

    return vkAllocateDescriptorSets(device_, &allocInfo, &descriptorSet_) == VK_SUCCESS;
}

void GPUInference::updateDescriptorSet() {
    struct BufInfo { GPUBuffer* buf; uint32_t binding; };
    BufInfo buffers[] = {
        {&weightBuffer_, 0}, {&layerMetaBuffer_, 1},
        {&latentBuffer_, 2}, {&obsBuffer_, 3}, {&actionBuffer_, 4},
    };

    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> bufInfos(5);

    for (int i = 0; i < 5; ++i) {
        auto* b = buffers[i].buf;
        if (b->buffer == VK_NULL_HANDLE) continue;

        bufInfos[i] = {};
        bufInfos[i].buffer = b->buffer;
        bufInfos[i].offset = 0;
        bufInfos[i].range = b->size;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet_;
        write.dstBinding = buffers[i].binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bufInfos[i];
        writes.push_back(write);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }
}

bool GPUInference::packWeights(const calm::LowLevelController& llc,
                                std::vector<float>& packedWeights,
                                std::vector<GPULayerMeta>& layerMetas) {
    packedWeights.clear();
    layerMetas.clear();

    const auto& scNet = llc.network();
    const auto& styleMLP = scNet.styleMLP();
    const auto& mainMLP = scNet.mainMLP();

    // Convert ml::Activation to GPU activation code (0=None, 1=ReLU, 2=Tanh)
    auto activationToGPU = [](Activation act) -> uint32_t {
        switch (act) {
            case Activation::ReLU: return 1;
            case Activation::Tanh: return 2;
            default: return 0;
        }
    };

    auto packNetwork = [&](const MLPNetwork& net) {
        for (size_t i = 0; i < net.numLayers(); ++i) {
            const auto& layer = net.layer(i);
            GPULayerMeta meta{};
            meta.weightOffset = static_cast<uint32_t>(packedWeights.size());
            const Tensor& w = layer.weights;
            for (size_t j = 0; j < w.size(); ++j) packedWeights.push_back(w[j]);
            meta.biasOffset = static_cast<uint32_t>(packedWeights.size());
            const Tensor& b = layer.bias;
            for (size_t j = 0; j < b.size(); ++j) packedWeights.push_back(b[j]);
            meta.inFeatures = static_cast<uint32_t>(layer.inFeatures);
            meta.outFeatures = static_cast<uint32_t>(layer.outFeatures);
            meta.activation = activationToGPU(net.activation(i));
            layerMetas.push_back(meta);
        }
    };

    packNetwork(styleMLP);
    uint32_t styleLayerCount = static_cast<uint32_t>(styleMLP.numLayers());

    packNetwork(mainMLP);
    const auto& muHead = llc.muHead();
    if (muHead.numLayers() > 0) packNetwork(muHead);
    uint32_t mainLayerCount = static_cast<uint32_t>(mainMLP.numLayers() + muHead.numLayers());

    pushConstants_.numLayers = static_cast<uint32_t>(layerMetas.size());
    pushConstants_.styleLayerCount = styleLayerCount;
    pushConstants_.mainLayerCount = mainLayerCount;
    pushConstants_.styleDim = (styleLayerCount > 0)
        ? layerMetas[styleLayerCount - 1].outFeatures : 0;

    return true;
}

} // namespace ml
