#include "HiZSystem.h"
#include "ShaderLoader.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include "core/ImageBuilder.h"
#include <SDL3/SDL_log.h>
#include <array>
#include <cstring>
#include <algorithm>

using namespace vk;

std::unique_ptr<HiZSystem> HiZSystem::create(const InitInfo& info) {
    auto system = std::unique_ptr<HiZSystem>(new HiZSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<HiZSystem> HiZSystem::create(const InitContext& ctx, VkFormat depthFormat_) {
    InitInfo info;
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.extent = ctx.extent;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.depthFormat = depthFormat_;
    return create(info);
}

HiZSystem::~HiZSystem() {
    cleanup();
}

bool HiZSystem::initInternal(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    depthFormat = info.depthFormat;

    if (!createHiZPyramid()) {
        SDL_Log("HiZSystem: Failed to create Hi-Z pyramid");
        return false;
    }

    if (!createPyramidPipeline()) {
        SDL_Log("HiZSystem: Failed to create pyramid pipeline");
        return false;
    }

    if (!createCullingPipeline()) {
        SDL_Log("HiZSystem: Failed to create culling pipeline");
        return false;
    }

    if (!createBuffers()) {
        SDL_Log("HiZSystem: Failed to create buffers");
        return false;
    }

    if (!createDescriptorSets()) {
        SDL_Log("HiZSystem: Failed to create descriptor sets");
        return false;
    }

    SDL_Log("HiZSystem: Initialized with %u mip levels", mipLevelCount);
    return true;
}

void HiZSystem::cleanup() {
    if (device == VK_NULL_HANDLE) return;

    destroyDescriptorSets();
    destroyBuffers();
    destroyPipelines();
    destroyHiZPyramid();

    device = VK_NULL_HANDLE;
}

void HiZSystem::resize(VkExtent2D newExtent) {
    if (newExtent.width == extent.width && newExtent.height == extent.height) {
        return;
    }

    vkDeviceWaitIdle(device);

    extent = newExtent;

    // Recreate Hi-Z pyramid with new size
    destroyHiZPyramid();
    createHiZPyramid();

    // Recreate descriptor sets (they reference the pyramid)
    destroyDescriptorSets();
    createDescriptorSets();
}

bool HiZSystem::createHiZPyramid() {
    mipLevelCount = calculateMipLevels(extent);

    // Create Hi-Z pyramid using MipChainBuilder
    if (!MipChainBuilder(device, allocator)
            .setExtent(extent)
            .setFormat(HIZ_FORMAT)
            .asStorageImage()  // sampled + storage for compute mip generation
            .build(hiZPyramid)) {
        SDL_Log("HiZSystem: Failed to create Hi-Z pyramid");
        return false;
    }
    mipLevelCount = hiZPyramid.mipLevelCount;

    // Create sampler for Hi-Z reads with mipmap support
    SamplerCreateInfo samplerInfo{
        {},                                  // flags
        Filter::eNearest,
        Filter::eNearest,
        SamplerMipmapMode::eNearest,
        SamplerAddressMode::eClampToEdge,
        SamplerAddressMode::eClampToEdge,
        SamplerAddressMode::eClampToEdge,
        0.0f,                                // mipLodBias
        VK_FALSE, 1.0f,                      // anisotropyEnable, maxAnisotropy
        VK_FALSE, {},                        // compareEnable, compareOp
        0.0f,                                // minLod
        static_cast<float>(mipLevelCount),   // maxLod
        BorderColor::eFloatOpaqueWhite
    };

    auto vkSamplerInfo = static_cast<VkSamplerCreateInfo>(samplerInfo);
    if (!ManagedSampler::create(device, vkSamplerInfo, hiZSampler)) {
        SDL_Log("HiZSystem: Failed to create Hi-Z sampler");
        return false;
    }

    return true;
}

void HiZSystem::destroyHiZPyramid() {
    // RAII wrapper handles sampler cleanup
    hiZSampler = ManagedSampler();

    // MipChainBuilder::Result handles cleanup via RAII
    hiZPyramid.reset();
    mipLevelCount = 0;
}

bool HiZSystem::createPyramidPipeline() {
    // Descriptor set layout for pyramid generation
    // Binding 0: Source depth buffer (sampler2D)
    // Binding 1: Source Hi-Z mip (sampler2D) - for subsequent passes
    // Binding 2: Destination Hi-Z mip (storage image)
    auto layoutBuilder = DescriptorManager::LayoutBuilder(device)
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 0: Source depth
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT)  // 1: Source Hi-Z mip
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT);         // 2: Destination Hi-Z mip

    if (!layoutBuilder.buildManaged(pyramidDescSetLayout)) {
        SDL_Log("HiZSystem: Failed to create pyramid descriptor set layout");
        return false;
    }

    // Push constant range
    PushConstantRange pushConstantRange{
        ShaderStageFlagBits::eCompute,
        0,
        sizeof(HiZPyramidPushConstants)
    };

    // Pipeline layout
    auto vkPushConstantRange = static_cast<VkPushConstantRange>(pushConstantRange);
    if (!DescriptorManager::createManagedPipelineLayout(
            device, pyramidDescSetLayout.get(), pyramidPipelineLayout, {vkPushConstantRange})) {
        SDL_Log("HiZSystem: Failed to create pyramid pipeline layout");
        return false;
    }

    // Load compute shader
    auto shaderModule = ShaderLoader::loadShaderModule(
        device, shaderPath + "/hiz_downsample.comp.spv");
    if (!shaderModule) {
        SDL_Log("HiZSystem: Failed to load hiz_downsample.comp.spv");
        return false;
    }

    PipelineShaderStageCreateInfo stageInfo{
        {},                              // flags
        ShaderStageFlagBits::eCompute,
        *shaderModule,
        "main"
    };

    ComputePipelineCreateInfo pipelineInfo{
        {},                              // flags
        stageInfo,
        pyramidPipelineLayout.get()
    };

    auto vkPipelineInfo = static_cast<VkComputePipelineCreateInfo>(pipelineInfo);
    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, vkPipelineInfo, pyramidPipeline);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (!success) {
        SDL_Log("HiZSystem: Failed to create pyramid compute pipeline");
        return false;
    }

    return true;
}

bool HiZSystem::createCullingPipeline() {
    // Descriptor set layout for culling
    // Binding 0: Uniforms (UBO)
    // Binding 1: Object data (SSBO, read-only)
    // Binding 2: Indirect draw buffer (SSBO, write)
    // Binding 3: Draw count buffer (SSBO, atomic)
    // Binding 4: Hi-Z pyramid (sampler2D)
    auto layoutBuilder = DescriptorManager::LayoutBuilder(device)
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 0: Uniforms
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 1: Object data
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 2: Indirect draw buffer
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)         // 3: Draw count buffer
        .addCombinedImageSampler(VK_SHADER_STAGE_COMPUTE_BIT); // 4: Hi-Z pyramid

    if (!layoutBuilder.buildManaged(cullingDescSetLayout)) {
        SDL_Log("HiZSystem: Failed to create culling descriptor set layout");
        return false;
    }

    // Pipeline layout (no push constants needed)
    if (!DescriptorManager::createManagedPipelineLayout(
            device, cullingDescSetLayout.get(), cullingPipelineLayout)) {
        SDL_Log("HiZSystem: Failed to create culling pipeline layout");
        return false;
    }

    // Load compute shader
    auto shaderModule = ShaderLoader::loadShaderModule(
        device, shaderPath + "/hiz_culling.comp.spv");
    if (!shaderModule) {
        SDL_Log("HiZSystem: Failed to load hiz_culling.comp.spv");
        return false;
    }

    PipelineShaderStageCreateInfo stageInfo{
        {},                              // flags
        ShaderStageFlagBits::eCompute,
        *shaderModule,
        "main"
    };

    ComputePipelineCreateInfo pipelineInfo{
        {},                              // flags
        stageInfo,
        cullingPipelineLayout.get()
    };

    auto vkPipelineInfo = static_cast<VkComputePipelineCreateInfo>(pipelineInfo);
    bool success = ManagedPipeline::createCompute(device, VK_NULL_HANDLE, vkPipelineInfo, cullingPipeline);
    vkDestroyShaderModule(device, *shaderModule, nullptr);

    if (!success) {
        SDL_Log("HiZSystem: Failed to create culling compute pipeline");
        return false;
    }

    return true;
}

void HiZSystem::destroyPipelines() {
    // RAII wrappers handle cleanup automatically
    cullingPipeline = ManagedPipeline();
    cullingPipelineLayout = ManagedPipelineLayout();
    cullingDescSetLayout = ManagedDescriptorSetLayout();

    pyramidPipeline = ManagedPipeline();
    pyramidPipelineLayout = ManagedPipelineLayout();
    pyramidDescSetLayout = ManagedDescriptorSetLayout();
}

bool HiZSystem::createBuffers() {
    VkDeviceSize objectBufferSize = sizeof(CullObjectData) * MAX_OBJECTS;

    // Create object data buffer using VulkanResourceFactory
    if (!VulkanResourceFactory::createStorageBufferHostReadable(allocator, objectBufferSize, objectDataBuffer_)) {
        SDL_Log("HiZSystem: Failed to create object data buffer");
        return false;
    }
    objectBufferCapacity = MAX_OBJECTS;

    // Create indirect draw buffers (per frame)
    VkDeviceSize indirectBufferSize = sizeof(::DrawIndexedIndirectCommand) * MAX_OBJECTS;
    bool success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(framesInFlight)
        .setSize(indirectBufferSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)
        .setAllocationFlags(0)  // GPU-only
        .build(indirectDrawBuffers);

    if (!success) {
        SDL_Log("HiZSystem: Failed to create indirect draw buffers");
        return false;
    }

    // Create draw count buffers (per frame)
    success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(framesInFlight)
        .setSize(sizeof(uint32_t))
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(drawCountBuffers);

    if (!success) {
        SDL_Log("HiZSystem: Failed to create draw count buffers");
        return false;
    }

    // Create uniform buffers (per frame)
    success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(framesInFlight)
        .setSize(sizeof(HiZCullUniforms))
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .build(uniformBuffers);

    if (!success) {
        SDL_Log("HiZSystem: Failed to create uniform buffers");
        return false;
    }

    return true;
}

void HiZSystem::destroyBuffers() {
    BufferUtils::destroyBuffers(allocator, uniformBuffers);
    BufferUtils::destroyBuffers(allocator, drawCountBuffers);
    BufferUtils::destroyBuffers(allocator, indirectDrawBuffers);

    // RAII-managed object data buffer (via reset)
    objectDataBuffer_.reset();
}

bool HiZSystem::createDescriptorSets() {
    // Allocate pyramid descriptor sets (one per mip level) using managed pool
    pyramidDescSets = descriptorPool->allocate(pyramidDescSetLayout.get(), mipLevelCount);
    if (pyramidDescSets.size() != mipLevelCount) {
        SDL_Log("HiZSystem: Failed to allocate pyramid descriptor sets");
        return false;
    }

    // Allocate culling descriptor sets (one per frame) using managed pool
    cullingDescSets = descriptorPool->allocate(cullingDescSetLayout.get(), framesInFlight);
    if (cullingDescSets.size() != framesInFlight) {
        SDL_Log("HiZSystem: Failed to allocate culling descriptor sets");
        return false;
    }

    for (uint32_t i = 0; i < framesInFlight; ++i) {
        // Update culling descriptor set using SetWriter
        DescriptorManager::SetWriter(device, cullingDescSets[i])
            .writeBuffer(0, uniformBuffers.buffers[i], 0, sizeof(HiZCullUniforms))
            .writeBuffer(1, objectDataBuffer_.get(), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, indirectDrawBuffers.buffers[i], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(3, drawCountBuffers.buffers[i], 0, sizeof(uint32_t), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeImage(4, hiZPyramid.fullView.get(), hiZSampler.get())
            .update();
    }

    return true;
}

void HiZSystem::destroyDescriptorSets() {
    // Descriptor sets are freed when the pool is destroyed/reset
    pyramidDescSets.clear();
    cullingDescSets.clear();
}

void HiZSystem::setDepthBuffer(VkImageView depthView, VkSampler depthSampler) {
    sourceDepthView = depthView;
    sourceDepthSampler = depthSampler;

    // Update pyramid descriptor sets with the depth buffer
    if (pyramidDescSets.empty() || hiZPyramid.mipViews.empty()) {
        return;
    }

    for (uint32_t mip = 0; mip < mipLevelCount; ++mip) {
        VkImageView srcMipView = mip > 0 ? hiZPyramid.mipViews[mip - 1].get() : hiZPyramid.mipViews[0].get();

        DescriptorManager::SetWriter(device, pyramidDescSets[mip])
            .writeImage(0, sourceDepthView, sourceDepthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
            .writeImage(1, srcMipView, hiZSampler.get())
            .writeStorageImage(2, hiZPyramid.mipViews[mip].get())
            .update();
    }
}

void HiZSystem::updateUniforms(uint32_t frameIndex, const glm::mat4& view, const glm::mat4& proj,
                                const glm::vec3& cameraPos, float nearPlane, float farPlane) {
    HiZCullUniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.viewProjMatrix = proj * view;
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    uniforms.screenParams = glm::vec4(
        static_cast<float>(extent.width),
        static_cast<float>(extent.height),
        1.0f / static_cast<float>(extent.width),
        1.0f / static_cast<float>(extent.height)
    );
    uniforms.depthParams = glm::vec4(nearPlane, farPlane, static_cast<float>(mipLevelCount), 0.0f);
    uniforms.objectCount = objectCount;
    uniforms.enableHiZ = hiZEnabled ? 1 : 0;

    // Extract frustum planes
    extractFrustumPlanes(uniforms.viewProjMatrix, uniforms.frustumPlanes);

    // Copy to GPU
    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(HiZCullUniforms));
}

void HiZSystem::updateObjectData(const std::vector<CullObjectData>& objects) {
    objectCount = static_cast<uint32_t>(objects.size());

    if (objectCount == 0) {
        return;
    }

    // Ensure buffer is large enough
    if (objectCount > objectBufferCapacity) {
        SDL_Log("HiZSystem: Object count %u exceeds capacity %u", objectCount, objectBufferCapacity);
        objectCount = objectBufferCapacity;
    }

    // Map and copy data
    void* mappedData = objectDataBuffer_.map();
    memcpy(mappedData, objects.data(), sizeof(CullObjectData) * objectCount);
    objectDataBuffer_.unmap();
}

void HiZSystem::recordPyramidGeneration(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (sourceDepthView == VK_NULL_HANDLE) {
        return;
    }

    // Transition Hi-Z pyramid to general for writing
    Barriers::prepareImageForCompute(cmd, hiZPyramid.image.get(), mipLevelCount, 1);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pyramidPipeline.get());

    // Generate each mip level
    uint32_t srcWidth = extent.width;
    uint32_t srcHeight = extent.height;

    for (uint32_t mip = 0; mip < mipLevelCount; ++mip) {
        uint32_t dstWidth = std::max(1u, srcWidth >> 1);
        uint32_t dstHeight = std::max(1u, srcHeight >> 1);

        // First mip reads from depth buffer, subsequent mips read from previous level
        if (mip == 0) {
            dstWidth = srcWidth;
            dstHeight = srcHeight;
        }

        // Push constants
        HiZPyramidPushConstants pushConstants = {
            srcWidth, srcHeight,
            dstWidth, dstHeight,
            mip > 0 ? mip - 1 : 0,
            mip == 0 ? 1u : 0u
        };

        vkCmdPushConstants(cmd, pyramidPipelineLayout.get(), VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pushConstants), &pushConstants);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pyramidPipelineLayout.get(), 0, 1, &pyramidDescSets[mip], 0, nullptr);

        // Dispatch
        uint32_t groupsX = (dstWidth + 7) / 8;
        uint32_t groupsY = (dstHeight + 7) / 8;
        vkCmdDispatch(cmd, groupsX, groupsY, 1);

        // Barrier between mip levels
        if (mip < mipLevelCount - 1) {
            Barriers::transitionImage(cmd, hiZPyramid.image.get(),
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, mip, 1);
        }

        srcWidth = dstWidth;
        srcHeight = dstHeight;
    }

    // Transition entire pyramid to shader read for culling
    Barriers::imageComputeToSampling(cmd, hiZPyramid.image.get(),
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, mipLevelCount, 1);
}

void HiZSystem::recordCulling(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (objectCount == 0) {
        return;
    }

    // Reset draw count to zero
    Barriers::clearBufferForComputeReadWrite(cmd, drawCountBuffers.buffers[frameIndex]);

    // Bind culling pipeline and descriptor set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullingPipeline.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            cullingPipelineLayout.get(), 0, 1, &cullingDescSets[frameIndex], 0, nullptr);

    // Dispatch
    uint32_t groupCount = (objectCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    barrierCullingToIndirectDraw(cmd);
}

void HiZSystem::barrierCullingToIndirectDraw(VkCommandBuffer cmd) {
    Barriers::BarrierBatch(cmd)
        .setStages(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT)
        .memoryBarrier(VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT)
        .submit();
}

VkBuffer HiZSystem::getIndirectDrawBuffer(uint32_t frameIndex) const {
    return indirectDrawBuffers.buffers[frameIndex];
}

VkBuffer HiZSystem::getDrawCountBuffer(uint32_t frameIndex) const {
    return drawCountBuffers.buffers[frameIndex];
}

uint32_t HiZSystem::getVisibleCount(uint32_t frameIndex) const {
    if (drawCountBuffers.mappedPointers.empty()) {
        return 0;
    }
    return *static_cast<uint32_t*>(drawCountBuffers.mappedPointers[frameIndex]);
}

VkImageView HiZSystem::getHiZMipView(uint32_t mipLevel) const {
    if (mipLevel < hiZPyramid.mipViews.size()) {
        return hiZPyramid.mipViews[mipLevel].get();
    }
    return VK_NULL_HANDLE;
}

void HiZSystem::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    // Left plane
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );

    // Right plane
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );

    // Bottom plane
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );

    // Top plane
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );

    // Near plane
    planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]
    );

    // Far plane
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes[i]));
        if (len > 0.0001f) {
            planes[i] /= len;
        }
    }
}
