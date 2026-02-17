#include "TwoPassCuller.h"
#include "ShaderLoader.h"
#include "ImageBuilder.h"
#include "shaders/bindings.h"

#include <SDL3/SDL_log.h>
#include <cstring>

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<TwoPassCuller> TwoPassCuller::create(const InitInfo& info) {
    auto culler = std::make_unique<TwoPassCuller>(ConstructToken{});
    if (!culler->initInternal(info)) {
        return nullptr;
    }
    return culler;
}

std::unique_ptr<TwoPassCuller> TwoPassCuller::create(const InitContext& ctx,
                                                       uint32_t maxClusters,
                                                       uint32_t maxDrawCommands) {
    InitInfo info{};
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.maxClusters = maxClusters;
    info.maxDrawCommands = maxDrawCommands;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

TwoPassCuller::~TwoPassCuller() {
    cleanup();
}

// ============================================================================
// Initialization
// ============================================================================

bool TwoPassCuller::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    maxClusters_ = info.maxClusters;
    maxDrawCommands_ = info.maxDrawCommands;
    maxDAGLevels_ = info.maxDAGLevels;
    hasDrawIndirectCount_ = info.hasDrawIndirectCount;
    raiiDevice_ = info.raiiDevice;

    if (!createBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create buffers");
        return false;
    }

    if (!createPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create pipeline");
        return false;
    }

    if (!createLODSelectPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create LOD selection pipeline");
        return false;
    }

    SDL_Log("TwoPassCuller: Initialized (maxClusters=%u, maxDrawCommands=%u, maxDAGLevels=%u)",
            maxClusters_, maxDrawCommands_, maxDAGLevels_);
    return true;
}

void TwoPassCuller::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_);
    destroyDescriptorSets();
    destroyLODSelectPipeline();
    destroyPipeline();
    destroyBuffers();
}

// ============================================================================
// Buffers
// ============================================================================

bool TwoPassCuller::createBuffers() {
    VkDeviceSize indirectSize = maxDrawCommands_ * sizeof(VkDrawIndexedIndirectCommand);
    VkDeviceSize countSize = sizeof(uint32_t);
    VkDeviceSize visibleSize = maxClusters_ * sizeof(uint32_t);
    VkDeviceSize uniformSize = sizeof(ClusterCullUniforms);

    auto builder = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(framesInFlight_);

    // Indirect command buffers (GPU-written, GPU-read for vkCmdDrawIndexedIndirectCount)
    bool ok = builder
        .setSize(indirectSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        .setAllocationFlags(0)
        .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
        .build(pass1IndirectBuffers_);
    if (!ok) return false;

    ok = builder.build(pass2IndirectBuffers_);
    if (!ok) return false;

    // Per-draw data buffers (parallel to indirect commands, read by raster shader via gl_DrawID)
    // Each entry: { uint instanceId, uint triangleOffset } = 8 bytes
    VkDeviceSize drawDataSize = maxDrawCommands_ * 2 * sizeof(uint32_t);
    ok = builder
        .setSize(drawDataSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
        .build(pass1DrawDataBuffers_);
    if (!ok) return false;

    ok = builder.build(pass2DrawDataBuffers_);
    if (!ok) return false;

    // Draw count buffers (atomic counter, GPU-written)
    ok = builder
        .setSize(countSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .build(pass1DrawCountBuffers_);
    if (!ok) return false;

    ok = builder.build(pass2DrawCountBuffers_);
    if (!ok) return false;

    // Visible cluster ID buffers
    ok = builder
        .setSize(visibleSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
        .build(visibleClusterBuffers_);
    if (!ok) return false;

    ok = builder.build(prevVisibleClusterBuffers_);
    if (!ok) return false;

    // Visible count buffers
    ok = builder
        .setSize(countSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .build(visibleCountBuffers_);
    if (!ok) return false;

    ok = builder.build(prevVisibleCountBuffers_);
    if (!ok) return false;

    // Uniform buffers (CPU-written each frame)
    ok = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(framesInFlight_)
        .setSize(uniformSize)
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(uniformBuffers_);
    if (!ok) return false;

    // LOD selection buffers
    ok = builder
        .setSize(visibleSize)  // same capacity as visible cluster buffers
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
        .setAllocationFlags(0)
        .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
        .build(selectedClusterBuffers_);
    if (!ok) return false;

    ok = builder
        .setSize(countSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .build(selectedCountBuffers_);
    if (!ok) return false;

    VkDeviceSize lodSelectUniformSize = sizeof(ClusterSelectUniforms);
    ok = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(framesInFlight_)
        .setSize(lodSelectUniformSize)
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(lodSelectUniformBuffers_);
    if (!ok) return false;

    // Top-down DAG traversal: ping-pong node buffers
    // Each buffer holds cluster indices for one level of the DAG
    ok = builder
        .setSize(visibleSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setAllocationFlags(0)
        .setMemoryUsage(VMA_MEMORY_USAGE_GPU_ONLY)
        .build(nodeBufferA_);
    if (!ok) return false;

    ok = builder.build(nodeBufferB_);
    if (!ok) return false;

    // Node count buffers (atomic counters for each ping-pong side)
    ok = builder
        .setSize(countSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .build(nodeCountA_);
    if (!ok) return false;

    ok = builder.build(nodeCountB_);
    if (!ok) return false;

    return true;
}

void TwoPassCuller::destroyBuffers() {
    BufferUtils::destroyBuffers(allocator_, pass1IndirectBuffers_);
    BufferUtils::destroyBuffers(allocator_, pass1DrawCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, pass1DrawDataBuffers_);
    BufferUtils::destroyBuffers(allocator_, pass2IndirectBuffers_);
    BufferUtils::destroyBuffers(allocator_, pass2DrawCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, pass2DrawDataBuffers_);
    BufferUtils::destroyBuffers(allocator_, visibleClusterBuffers_);
    BufferUtils::destroyBuffers(allocator_, visibleCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, prevVisibleClusterBuffers_);
    BufferUtils::destroyBuffers(allocator_, prevVisibleCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, uniformBuffers_);
    BufferUtils::destroyBuffers(allocator_, selectedClusterBuffers_);
    BufferUtils::destroyBuffers(allocator_, selectedCountBuffers_);
    BufferUtils::destroyBuffers(allocator_, lodSelectUniformBuffers_);
    BufferUtils::destroyBuffers(allocator_, nodeBufferA_);
    BufferUtils::destroyBuffers(allocator_, nodeBufferB_);
    BufferUtils::destroyBuffers(allocator_, nodeCountA_);
    BufferUtils::destroyBuffers(allocator_, nodeCountB_);
}

// ============================================================================
// Pipeline
// ============================================================================

bool TwoPassCuller::createPipeline() {
    if (!raiiDevice_) return false;

    // Descriptor set layout matching cluster_cull.comp bindings (0-10)
    std::array<vk::DescriptorSetLayoutBinding, 11> bindings;
    bindings[0] = vk::DescriptorSetLayoutBinding{}.setBinding(0)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // clusters
    bindings[1] = vk::DescriptorSetLayoutBinding{}.setBinding(1)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // instances
    bindings[2] = vk::DescriptorSetLayoutBinding{}.setBinding(2)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // indirect commands
    bindings[3] = vk::DescriptorSetLayoutBinding{}.setBinding(3)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // draw count
    bindings[4] = vk::DescriptorSetLayoutBinding{}.setBinding(4)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // visible clusters
    bindings[5] = vk::DescriptorSetLayoutBinding{}.setBinding(5)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // visible count
    bindings[6] = vk::DescriptorSetLayoutBinding{}.setBinding(6)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // cull uniforms
    bindings[7] = vk::DescriptorSetLayoutBinding{}.setBinding(7)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // Hi-Z pyramid
    bindings[8] = vk::DescriptorSetLayoutBinding{}.setBinding(8)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // prev visible clusters
    bindings[9] = vk::DescriptorSetLayoutBinding{}.setBinding(9)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // prev visible count
    bindings[10] = vk::DescriptorSetLayoutBinding{}.setBinding(BINDING_CLUSTER_CULL_DRAW_DATA)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // per-draw data output

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);

    try {
        descSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create desc set layout: %s", e.what());
        return false;
    }

    vk::DescriptorSetLayout vkDescLayout(**descSetLayout_);

    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}.setSetLayouts(vkDescLayout);

    vk::Device vkDevice(device_);
    pipelineLayout_ = static_cast<VkPipelineLayout>(vkDevice.createPipelineLayout(pipelineLayoutInfo));

    // Load compute shader
    auto compModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/cluster_cull.comp.spv");
    if (!compModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to load cluster_cull.comp shader");
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*compModule)
        .setPName("main");

    auto computeInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(pipelineLayout_);

    auto result = vkDevice.createComputePipeline(nullptr, computeInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create compute pipeline");
        vkDevice.destroyShaderModule(*compModule);
        return false;
    }
    pipeline_ = static_cast<VkPipeline>(result.value);

    vkDevice.destroyShaderModule(*compModule);

    SDL_Log("TwoPassCuller: Compute pipeline created");
    return true;
}

void TwoPassCuller::destroyPipeline() {
    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = VK_NULL_HANDLE;
    }
    descSetLayout_.reset();
}

bool TwoPassCuller::createLODSelectPipeline() {
    if (!raiiDevice_) return false;

    // Descriptor set layout matching cluster_select.comp bindings (0-8)
    std::array<vk::DescriptorSetLayoutBinding, 9> bindings;
    bindings[0] = vk::DescriptorSetLayoutBinding{}.setBinding(0)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // clusters
    bindings[1] = vk::DescriptorSetLayoutBinding{}.setBinding(1)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // instances
    bindings[2] = vk::DescriptorSetLayoutBinding{}.setBinding(2)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // selected clusters output
    bindings[3] = vk::DescriptorSetLayoutBinding{}.setBinding(3)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // selected count
    bindings[4] = vk::DescriptorSetLayoutBinding{}.setBinding(4)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // select uniforms
    bindings[5] = vk::DescriptorSetLayoutBinding{}.setBinding(5)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // input nodes
    bindings[6] = vk::DescriptorSetLayoutBinding{}.setBinding(6)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // input node count
    bindings[7] = vk::DescriptorSetLayoutBinding{}.setBinding(7)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // output nodes
    bindings[8] = vk::DescriptorSetLayoutBinding{}.setBinding(8)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer).setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eCompute);  // output node count

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);

    try {
        lodSelectDescSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create LOD select desc set layout: %s", e.what());
        return false;
    }

    vk::DescriptorSetLayout vkDescLayout(**lodSelectDescSetLayout_);
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}.setSetLayouts(vkDescLayout);

    vk::Device vkDevice(device_);
    lodSelectPipelineLayout_ = static_cast<VkPipelineLayout>(vkDevice.createPipelineLayout(pipelineLayoutInfo));

    // Load compute shader
    auto compModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/cluster_select.comp.spv");
    if (!compModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to load cluster_select.comp shader");
        return false;
    }

    auto stageInfo = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eCompute)
        .setModule(*compModule)
        .setPName("main");

    auto computeInfo = vk::ComputePipelineCreateInfo{}
        .setStage(stageInfo)
        .setLayout(lodSelectPipelineLayout_);

    auto result = vkDevice.createComputePipeline(nullptr, computeInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create LOD select compute pipeline");
        vkDevice.destroyShaderModule(*compModule);
        return false;
    }
    lodSelectPipeline_ = static_cast<VkPipeline>(result.value);

    vkDevice.destroyShaderModule(*compModule);

    SDL_Log("TwoPassCuller: LOD selection compute pipeline created");
    return true;
}

void TwoPassCuller::destroyLODSelectPipeline() {
    if (lodSelectPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, lodSelectPipeline_, nullptr);
        lodSelectPipeline_ = VK_NULL_HANDLE;
    }
    if (lodSelectPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, lodSelectPipelineLayout_, nullptr);
        lodSelectPipelineLayout_ = VK_NULL_HANDLE;
    }
    lodSelectDescSetLayout_.reset();
}

void TwoPassCuller::setRootClusters(const std::vector<uint32_t>& rootIndices) {
    rootClusterIndices_ = rootIndices;
    SDL_Log("TwoPassCuller: Set %zu root clusters for DAG traversal",
            rootClusterIndices_.size());
}

void TwoPassCuller::recordLODSelection(VkCommandBuffer cmd, uint32_t frameIndex,
                                         uint32_t totalDAGClusters, uint32_t instanceCount) {
    // Update LOD selection uniforms
    ClusterSelectUniforms selectUniforms{};

    // Reuse the viewProjMatrix from the cull uniforms (already written this frame)
    void* cullMapped = uniformBuffers_.mappedPointers[frameIndex];
    if (cullMapped) {
        auto* cullUbo = static_cast<const ClusterCullUniforms*>(cullMapped);
        selectUniforms.viewProjMatrix = cullUbo->viewProjMatrix;
        selectUniforms.screenParams = cullUbo->screenParams;
    }

    selectUniforms.totalClusterCount = totalDAGClusters;
    selectUniforms.instanceCount = instanceCount;
    selectUniforms.errorThreshold = errorThreshold_;
    selectUniforms.maxSelectedClusters = maxClusters_;

    void* mapped = lodSelectUniformBuffers_.mappedPointers[frameIndex];
    if (mapped) {
        memcpy(mapped, &selectUniforms, sizeof(selectUniforms));
        vmaFlushAllocation(allocator_, lodSelectUniformBuffers_.allocations[frameIndex],
                           0, sizeof(selectUniforms));
    }

    // Clear selected count to 0 (accumulated across all passes)
    vkCmdFillBuffer(cmd, selectedCountBuffers_.buffers[frameIndex], 0, sizeof(uint32_t), 0);

    // Seed input buffer A with root cluster indices
    if (!rootClusterIndices_.empty()) {
        VkDeviceSize seedSize = rootClusterIndices_.size() * sizeof(uint32_t);
        vkCmdUpdateBuffer(cmd, nodeBufferA_.buffers[frameIndex], 0,
                          seedSize, rootClusterIndices_.data());

        // Write root count to nodeCountA
        uint32_t rootCount = static_cast<uint32_t>(rootClusterIndices_.size());
        vkCmdUpdateBuffer(cmd, nodeCountA_.buffers[frameIndex], 0,
                          sizeof(uint32_t), &rootCount);
    } else {
        // No roots — write 0 count
        vkCmdFillBuffer(cmd, nodeCountA_.buffers[frameIndex], 0, sizeof(uint32_t), 0);
    }

    // Barrier: transfer writes -> compute reads
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    // Bind LOD selection pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, lodSelectPipeline_);

    // Multi-pass top-down traversal: one dispatch per DAG level
    // Each pass reads from the input buffer and writes children to the output buffer.
    // Selected clusters are accumulated into selectedClusterBuffers_ across all passes.
    //
    // We dispatch ceil(maxClusters/64) workgroups each pass. Threads beyond
    // the actual node count early-exit via the inputNodeCount SSBO check.
    uint32_t workGroupSize = 64;
    uint32_t maxDispatch = (maxClusters_ + workGroupSize - 1) / workGroupSize;

    // Ping-pong: even levels read A/write B, odd levels read B/write A
    auto* inputBuffers = &nodeBufferA_;
    auto* inputCountBuffers = &nodeCountA_;
    auto* outputBuffers = &nodeBufferB_;
    auto* outputCountBuffers = &nodeCountB_;

    for (uint32_t level = 0; level < maxDAGLevels_; ++level) {
        // Clear output node count for this pass
        vkCmdFillBuffer(cmd, outputCountBuffers->buffers[frameIndex], 0, sizeof(uint32_t), 0);

        // Barrier: clear -> compute
        memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &memBarrier, 0, nullptr, 0, nullptr);

        // Descriptor sets would be bound here for (input, output) pair
        // Bindings 5-8 change each pass for the ping-pong:
        //   5 = inputBuffers, 6 = inputCountBuffers,
        //   7 = outputBuffers, 8 = outputCountBuffers

        // Dispatch
        vkCmdDispatch(cmd, maxDispatch, 1, 1);

        // Barrier: compute writes -> next pass compute reads + transfer (for clear)
        memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &memBarrier, 0, nullptr, 0, nullptr);

        // Swap ping-pong: output becomes input for next level
        std::swap(inputBuffers, outputBuffers);
        std::swap(inputCountBuffers, outputCountBuffers);
    }

    // Final barrier: compute write -> compute read (selected clusters -> cull pass)
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

VkBuffer TwoPassCuller::getSelectedClusterBuffer(uint32_t frameIndex) const {
    return selectedClusterBuffers_.buffers[frameIndex];
}

VkBuffer TwoPassCuller::getSelectedCountBuffer(uint32_t frameIndex) const {
    return selectedCountBuffers_.buffers[frameIndex];
}

void TwoPassCuller::setExternalBuffers(VkBuffer clusterBuffer, VkDeviceSize clusterSize,
                                        const std::vector<VkBuffer>& instanceBuffers,
                                        VkDeviceSize instanceSize) {
    externalClusterBuffer_ = clusterBuffer;
    externalClusterSize_ = clusterSize;
    externalInstanceBuffers_ = instanceBuffers;
    externalInstanceSize_ = instanceSize;

    // (Re)create descriptor sets now that we have all buffers
    destroyDescriptorSets();
    createDescriptorSets();
}

bool TwoPassCuller::createDescriptorSets() {
    if (!descSetLayout_ || externalClusterBuffer_ == VK_NULL_HANDLE ||
        externalInstanceBuffers_.empty()) {
        return true;  // Not ready yet — will be called from setExternalBuffers
    }

    VkDescriptorSetLayout rawLayout = **descSetLayout_;
    VkDeviceSize drawDataSize = maxDrawCommands_ * 2 * sizeof(uint32_t);

    // Allocate pass 1 and pass 2 descriptor sets (one per frame)
    pass1DescSets_ = descriptorPool_->allocate(rawLayout, framesInFlight_);
    pass2DescSets_ = descriptorPool_->allocate(rawLayout, framesInFlight_);

    if (pass1DescSets_.size() != framesInFlight_ || pass2DescSets_.size() != framesInFlight_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to allocate cull descriptor sets");
        return false;
    }

    // Create Hi-Z sampler (nearest, clamp) for pass 2
    if (!hiZSampler_) {
        auto samplerInfo = vk::SamplerCreateInfo{}
            .setMagFilter(vk::Filter::eNearest)
            .setMinFilter(vk::Filter::eNearest)
            .setMipmapMode(vk::SamplerMipmapMode::eNearest)
            .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
            .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
            .setMaxLod(16.0f);
        hiZSampler_.emplace(*raiiDevice_, samplerInfo);
    }

    // Create 1x1 placeholder for Hi-Z in pass 1 (not used but must be valid)
    if (placeholderHiZView_ == VK_NULL_HANDLE) {
        bool built = ImageBuilder(allocator_)
            .setExtent(1, 1)
            .setFormat(VK_FORMAT_R32_SFLOAT)
            .setUsage(VK_IMAGE_USAGE_SAMPLED_BIT)
            .setGpuOnly()
            .build(device_, placeholderHiZImage_, placeholderHiZView_, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!built) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "TwoPassCuller: Failed to create placeholder Hi-Z image");
        }
    }

    VkSampler rawHiZSampler = hiZSampler_ ? static_cast<VkSampler>(**hiZSampler_) : VK_NULL_HANDLE;

    for (uint32_t i = 0; i < framesInFlight_; ++i) {
        // Common buffer infos shared between pass 1 and pass 2
        VkDescriptorBufferInfo clusterInfo{externalClusterBuffer_, 0, externalClusterSize_};
        VkDescriptorBufferInfo instanceInfo{
            (i < externalInstanceBuffers_.size()) ? externalInstanceBuffers_[i] : externalInstanceBuffers_[0],
            0, externalInstanceSize_};
        VkDescriptorBufferInfo uniformInfo{uniformBuffers_.buffers[i], 0, sizeof(ClusterCullUniforms)};
        VkDescriptorBufferInfo prevVisClusterInfo{prevVisibleClusterBuffers_.buffers[i], 0, maxClusters_ * sizeof(uint32_t)};
        VkDescriptorBufferInfo prevVisCountInfo{prevVisibleCountBuffers_.buffers[i], 0, sizeof(uint32_t)};

        // Placeholder Hi-Z image for pass 1 (not used, enableHiZ=0)
        VkDescriptorImageInfo hiZInfo{rawHiZSampler, placeholderHiZView_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        // Pass 1 descriptor set
        {
            VkDescriptorBufferInfo indirectInfo{pass1IndirectBuffers_.buffers[i], 0, maxDrawCommands_ * sizeof(VkDrawIndexedIndirectCommand)};
            VkDescriptorBufferInfo drawCountInfo{pass1DrawCountBuffers_.buffers[i], 0, sizeof(uint32_t)};
            VkDescriptorBufferInfo visClusterInfo{visibleClusterBuffers_.buffers[i], 0, maxClusters_ * sizeof(uint32_t)};
            VkDescriptorBufferInfo visCountInfo{visibleCountBuffers_.buffers[i], 0, sizeof(uint32_t)};
            VkDescriptorBufferInfo drawDataInfo{pass1DrawDataBuffers_.buffers[i], 0, drawDataSize};

            std::array<VkWriteDescriptorSet, 11> writes{};
            for (auto& w : writes) w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

            writes[0].dstSet = pass1DescSets_[i]; writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].pBufferInfo = &clusterInfo;

            writes[1].dstSet = pass1DescSets_[i]; writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].pBufferInfo = &instanceInfo;

            writes[2].dstSet = pass1DescSets_[i]; writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1; writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].pBufferInfo = &indirectInfo;

            writes[3].dstSet = pass1DescSets_[i]; writes[3].dstBinding = 3;
            writes[3].descriptorCount = 1; writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[3].pBufferInfo = &drawCountInfo;

            writes[4].dstSet = pass1DescSets_[i]; writes[4].dstBinding = 4;
            writes[4].descriptorCount = 1; writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[4].pBufferInfo = &visClusterInfo;

            writes[5].dstSet = pass1DescSets_[i]; writes[5].dstBinding = 5;
            writes[5].descriptorCount = 1; writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[5].pBufferInfo = &visCountInfo;

            writes[6].dstSet = pass1DescSets_[i]; writes[6].dstBinding = 6;
            writes[6].descriptorCount = 1; writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[6].pBufferInfo = &uniformInfo;

            writes[7].dstSet = pass1DescSets_[i]; writes[7].dstBinding = 7;
            writes[7].descriptorCount = 1; writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[7].pImageInfo = &hiZInfo;

            writes[8].dstSet = pass1DescSets_[i]; writes[8].dstBinding = 8;
            writes[8].descriptorCount = 1; writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[8].pBufferInfo = &prevVisClusterInfo;

            writes[9].dstSet = pass1DescSets_[i]; writes[9].dstBinding = 9;
            writes[9].descriptorCount = 1; writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[9].pBufferInfo = &prevVisCountInfo;

            writes[10].dstSet = pass1DescSets_[i]; writes[10].dstBinding = BINDING_CLUSTER_CULL_DRAW_DATA;
            writes[10].descriptorCount = 1; writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[10].pBufferInfo = &drawDataInfo;

            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        // Pass 2 descriptor set (same layout, different output buffers, Hi-Z enabled)
        {
            VkDescriptorBufferInfo indirectInfo{pass2IndirectBuffers_.buffers[i], 0, maxDrawCommands_ * sizeof(VkDrawIndexedIndirectCommand)};
            VkDescriptorBufferInfo drawCountInfo{pass2DrawCountBuffers_.buffers[i], 0, sizeof(uint32_t)};
            VkDescriptorBufferInfo visClusterInfo{visibleClusterBuffers_.buffers[i], 0, maxClusters_ * sizeof(uint32_t)};
            VkDescriptorBufferInfo visCountInfo{visibleCountBuffers_.buffers[i], 0, sizeof(uint32_t)};
            VkDescriptorBufferInfo drawDataInfo{pass2DrawDataBuffers_.buffers[i], 0, drawDataSize};

            std::array<VkWriteDescriptorSet, 11> writes{};
            for (auto& w : writes) w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

            writes[0].dstSet = pass2DescSets_[i]; writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].pBufferInfo = &clusterInfo;

            writes[1].dstSet = pass2DescSets_[i]; writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].pBufferInfo = &instanceInfo;

            writes[2].dstSet = pass2DescSets_[i]; writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1; writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].pBufferInfo = &indirectInfo;

            writes[3].dstSet = pass2DescSets_[i]; writes[3].dstBinding = 3;
            writes[3].descriptorCount = 1; writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[3].pBufferInfo = &drawCountInfo;

            writes[4].dstSet = pass2DescSets_[i]; writes[4].dstBinding = 4;
            writes[4].descriptorCount = 1; writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[4].pBufferInfo = &visClusterInfo;

            writes[5].dstSet = pass2DescSets_[i]; writes[5].dstBinding = 5;
            writes[5].descriptorCount = 1; writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[5].pBufferInfo = &visCountInfo;

            writes[6].dstSet = pass2DescSets_[i]; writes[6].dstBinding = 6;
            writes[6].descriptorCount = 1; writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[6].pBufferInfo = &uniformInfo;

            writes[7].dstSet = pass2DescSets_[i]; writes[7].dstBinding = 7;
            writes[7].descriptorCount = 1; writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[7].pImageInfo = &hiZInfo;  // Will be updated per-frame with real Hi-Z

            writes[8].dstSet = pass2DescSets_[i]; writes[8].dstBinding = 8;
            writes[8].descriptorCount = 1; writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[8].pBufferInfo = &prevVisClusterInfo;

            writes[9].dstSet = pass2DescSets_[i]; writes[9].dstBinding = 9;
            writes[9].descriptorCount = 1; writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[9].pBufferInfo = &prevVisCountInfo;

            writes[10].dstSet = pass2DescSets_[i]; writes[10].dstBinding = BINDING_CLUSTER_CULL_DRAW_DATA;
            writes[10].descriptorCount = 1; writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[10].pBufferInfo = &drawDataInfo;

            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    SDL_Log("TwoPassCuller: Descriptor sets created (%u frames)", framesInFlight_);
    return true;
}

void TwoPassCuller::destroyDescriptorSets() {
    pass1DescSets_.clear();
    pass2DescSets_.clear();
    lodSelectDescSetsAB_.clear();
    lodSelectDescSetsBA_.clear();
    if (placeholderHiZView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, placeholderHiZView_, nullptr);
        placeholderHiZView_ = VK_NULL_HANDLE;
    }
    placeholderHiZImage_.reset();
    hiZSampler_.reset();
}

// ============================================================================
// Per-frame operations
// ============================================================================

void TwoPassCuller::updateUniforms(uint32_t frameIndex,
                                     const glm::mat4& view, const glm::mat4& proj,
                                     const glm::vec3& cameraPos,
                                     const glm::vec4 frustumPlanes[6],
                                     uint32_t clusterCount, uint32_t instanceCount,
                                     float nearPlane, float farPlane, uint32_t hiZMipLevels) {
    ClusterCullUniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.viewProjMatrix = proj * view;
    for (int i = 0; i < 6; ++i) {
        uniforms.frustumPlanes[i] = frustumPlanes[i];
    }
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    uniforms.screenParams = glm::vec4(0.0f);  // Set by caller based on render target size
    uniforms.depthParams = glm::vec4(nearPlane, farPlane, static_cast<float>(hiZMipLevels), 0.0f);
    uniforms.clusterCount = clusterCount;
    uniforms.instanceCount = instanceCount;
    uniforms.enableHiZ = 0;  // Pass 1 doesn't use Hi-Z
    uniforms.maxDrawCommands = maxDrawCommands_;

    void* mapped = uniformBuffers_.mappedPointers[frameIndex];
    if (mapped) {
        memcpy(mapped, &uniforms, sizeof(uniforms));
        vmaFlushAllocation(allocator_, uniformBuffers_.allocations[frameIndex],
                           0, sizeof(uniforms));
    }
}

void TwoPassCuller::recordPass1(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Clear draw count to 0 and indirect command buffer (unused slots have indexCount=0)
    vkCmdFillBuffer(cmd, pass1DrawCountBuffers_.buffers[frameIndex], 0, sizeof(uint32_t), 0);
    vkCmdFillBuffer(cmd, visibleCountBuffers_.buffers[frameIndex], 0, sizeof(uint32_t), 0);
    VkDeviceSize indirectSize = maxDrawCommands_ * sizeof(VkDrawIndexedIndirectCommand);
    vkCmdFillBuffer(cmd, pass1IndirectBuffers_.buffers[frameIndex], 0, indirectSize, 0);

    // Barrier: transfer -> compute
    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    // Bind pipeline and descriptor sets
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);

    if (frameIndex < pass1DescSets_.size()) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout_, 0, 1, &pass1DescSets_[frameIndex], 0, nullptr);
    }

    uint32_t workGroupSize = 64;
    uint32_t dispatchCount = (maxClusters_ + workGroupSize - 1) / workGroupSize;
    vkCmdDispatch(cmd, dispatchCount, 1, 1);

    // Barrier: compute write -> indirect read + vertex shader read
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

void TwoPassCuller::recordPass2(VkCommandBuffer cmd, uint32_t frameIndex, VkImageView hiZView) {
    (void)hiZView;  // TODO: update pass2 descriptor set binding 7 with real Hi-Z view

    // Clear pass 2 draw count
    vkCmdFillBuffer(cmd, pass2DrawCountBuffers_.buffers[frameIndex], 0, sizeof(uint32_t), 0);

    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);

    if (frameIndex < pass2DescSets_.size()) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout_, 0, 1, &pass2DescSets_[frameIndex], 0, nullptr);
    }

    uint32_t workGroupSize = 64;
    uint32_t dispatchCount = (maxClusters_ + workGroupSize - 1) / workGroupSize;
    vkCmdDispatch(cmd, dispatchCount, 1, 1);

    // Barrier: compute -> indirect draw + vertex shader read
    memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0, 1, &memBarrier, 0, nullptr, 0, nullptr);
}

void TwoPassCuller::swapBuffers() {
    currentBufferIndex_ = 1 - currentBufferIndex_;
}

VkBuffer TwoPassCuller::getPass1IndirectBuffer(uint32_t frameIndex) const {
    return pass1IndirectBuffers_.buffers[frameIndex];
}

VkBuffer TwoPassCuller::getPass1DrawCountBuffer(uint32_t frameIndex) const {
    return pass1DrawCountBuffers_.buffers[frameIndex];
}

VkBuffer TwoPassCuller::getPass2IndirectBuffer(uint32_t frameIndex) const {
    return pass2IndirectBuffers_.buffers[frameIndex];
}

VkBuffer TwoPassCuller::getPass2DrawCountBuffer(uint32_t frameIndex) const {
    return pass2DrawCountBuffers_.buffers[frameIndex];
}

VkBuffer TwoPassCuller::getPass1DrawDataBuffer(uint32_t frameIndex) const {
    return pass1DrawDataBuffers_.buffers[frameIndex];
}

VkBuffer TwoPassCuller::getPass2DrawDataBuffer(uint32_t frameIndex) const {
    return pass2DrawDataBuffers_.buffers[frameIndex];
}

VkDeviceSize TwoPassCuller::getDrawDataBufferSize() const {
    return maxDrawCommands_ * 2 * sizeof(uint32_t);
}
