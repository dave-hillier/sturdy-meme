#include "GrassSystem.h"
#include "GrassTileManager.h"
#include "WindSystem.h"
#include "InitContext.h"
#include "CullCommon.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "ComputePipelineBuilder.h"
#include "DescriptorManager.h"
#include "UBOs.h"
#include "VmaResources.h"
#include "core/vulkan/BarrierHelpers.h"
#include "core/ImageBuilder.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <cstring>
#include <array>

// Forward declare UniformBufferObject size (needed for descriptor set update)
struct UniformBufferObject;

GrassSystem::GrassSystem(ConstructToken) {}

std::unique_ptr<GrassSystem> GrassSystem::create(const InitInfo& info) {
    auto system = std::make_unique<GrassSystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::optional<GrassSystem::Bundle> GrassSystem::createWithDependencies(
    const InitContext& ctx,
    vk::RenderPass hdrRenderPass,
    vk::RenderPass shadowRenderPass,
    uint32_t shadowMapSize
) {
    // Create wind system
    WindSystem::InitInfo windInfo{};
    windInfo.device = ctx.device;
    windInfo.allocator = ctx.allocator;
    windInfo.framesInFlight = ctx.framesInFlight;

    auto windSystem = WindSystem::create(windInfo);
    if (!windSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize WindSystem");
        return std::nullopt;
    }

    // Create grass system
    InitInfo grassInfo{};
    grassInfo.device = ctx.device;
    grassInfo.allocator = ctx.allocator;
    grassInfo.renderPass = hdrRenderPass;
    grassInfo.shadowRenderPass = shadowRenderPass;
    grassInfo.descriptorPool = ctx.descriptorPool;
    grassInfo.extent = ctx.extent;
    grassInfo.shadowMapSize = shadowMapSize;
    grassInfo.shaderPath = ctx.shaderPath;
    grassInfo.framesInFlight = ctx.framesInFlight;
    grassInfo.raiiDevice = ctx.raiiDevice;

    auto grassSystem = create(grassInfo);
    if (!grassSystem) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GrassSystem");
        return std::nullopt;
    }

    // Wire environment settings from wind to grass
    grassSystem->setEnvironmentSettings(&windSystem->getEnvironmentSettings());

    return Bundle{
        std::move(windSystem),
        std::move(grassSystem)
    };
}

GrassSystem::~GrassSystem() {
    cleanup();
}

bool GrassSystem::initInternal(const InitInfo& info) {
    SDL_Log("GrassSystem::init() starting, device=%p, pool=%p", (void*)(VkDevice)info.device, (void*)info.descriptorPool);
    shadowRenderPass_ = info.shadowRenderPass;
    shadowMapSize_ = info.shadowMapSize;

    // Store init info for accessors used during initialization
    device_ = info.device;
    allocator_ = info.allocator;
    renderPass_ = info.renderPass;
    descriptorPool_ = info.descriptorPool;
    extent_ = info.extent;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    raiiDevice_ = info.raiiDevice;

    if (!raiiDevice_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GrassSystem requires raiiDevice");
        return false;
    }

    // Pointer to the ParticleSystem being initialized (for hooks to access)
    ParticleSystem* initializingPS = nullptr;

    SystemLifecycleHelper::Hooks hooks{};
    hooks.createBuffers = [this]() { return createBuffers(); };
    hooks.createComputeDescriptorSetLayout = [this, &initializingPS]() {
        return createComputeDescriptorSetLayout(initializingPS->getComputePipelineHandles());
    };
    hooks.createComputePipeline = [this, &initializingPS]() {
        return createComputePipeline(initializingPS->getComputePipelineHandles());
    };
    hooks.createGraphicsDescriptorSetLayout = [this, &initializingPS]() {
        return createGraphicsDescriptorSetLayout(initializingPS->getGraphicsPipelineHandles());
    };
    hooks.createGraphicsPipeline = [this, &initializingPS]() {
        return createGraphicsPipeline(initializingPS->getGraphicsPipelineHandles());
    };
    hooks.createExtraPipelines = [this, &initializingPS]() {
        return createExtraPipelines(initializingPS->getComputePipelineHandles(),
                                     initializingPS->getGraphicsPipelineHandles());
    };
    hooks.createDescriptorSets = [this]() { return createDescriptorSets(); };
    hooks.destroyBuffers = [this](VmaAllocator allocator) { destroyBuffers(allocator); };

    particleSystem = ParticleSystem::create(info, hooks, info.framesInFlight, &initializingPS);

    if (!particleSystem) {
        return false;
    }

    SDL_Log("GrassSystem::init() - particleSystem created successfully");

    // Write compute descriptor sets now that particleSystem is fully initialized
    writeComputeDescriptorSets();
    SDL_Log("GrassSystem::init() - done writing compute descriptor sets");
    return true;
}

void GrassSystem::cleanup() {
    if (!device_) return;  // Not initialized

    // Reset RAII wrappers
    tiledComputePipeline_.reset();
    shadowPipeline_.reset();
    shadowPipelineLayout_.reset();
    shadowDescriptorSetLayout_.reset();
    displacementPipeline_.reset();
    displacementPipelineLayout_.reset();
    displacementDescriptorSetLayout_.reset();
    displacementSampler_.reset();

    if (displacementImageView_) {
        device_.destroyImageView(displacementImageView_);
        displacementImageView_ = nullptr;
    }
    if (displacementImage_ && allocator_) {
        vmaDestroyImage(allocator_, displacementImage_, displacementAllocation_);
        displacementImage_ = nullptr;
        displacementAllocation_ = VK_NULL_HANDLE;
    }

    particleSystem.reset();

    device_ = nullptr;
    raiiDevice_ = nullptr;
}

void GrassSystem::destroyBuffers(VmaAllocator alloc) {
    BufferUtils::destroyBuffers(alloc, displacementSourceBuffers);
    BufferUtils::destroyBuffers(alloc, displacementUniformBuffers);

    BufferUtils::destroyBuffers(alloc, instanceBuffers);
    BufferUtils::destroyBuffers(alloc, indirectBuffers);
    BufferUtils::destroyBuffers(alloc, uniformBuffers);
    BufferUtils::destroyBuffers(alloc, paramsBuffers);
}

bool GrassSystem::createBuffers() {
    VkDeviceSize instanceBufferSize = sizeof(GrassInstance) * GrassConstants::MAX_INSTANCES;
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
    VkDeviceSize cullingUniformSize = sizeof(CullingUniforms);
    VkDeviceSize grassParamsSize = sizeof(GrassParams);

    // Use framesInFlight for buffer set count to ensure proper triple buffering
    uint32_t bufferSetCount = getFramesInFlight();

    BufferUtils::DoubleBufferedBufferBuilder instanceBuilder;
    if (!instanceBuilder.setAllocator(getAllocator())
             .setSetCount(bufferSetCount)
             .setSize(instanceBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .build(instanceBuffers)) {
        SDL_Log("Failed to create grass instance buffers");
        return false;
    }

    BufferUtils::DoubleBufferedBufferBuilder indirectBuilder;
    if (!indirectBuilder.setAllocator(getAllocator())
             .setSetCount(bufferSetCount)
             .setSize(indirectBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .build(indirectBuffers)) {
        SDL_Log("Failed to create grass indirect buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(cullingUniformSize)
             .build(uniformBuffers)) {
        SDL_Log("Failed to create grass culling uniform buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder paramsBuilder;
    if (!paramsBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(grassParamsSize)
             .build(paramsBuffers)) {
        SDL_Log("Failed to create grass params buffers");
        return false;
    }

    return createDisplacementResources();
}

bool GrassSystem::createDisplacementResources() {
    // Create displacement texture (RG16F, using unified constant for size)
    {
        ManagedImage image;
        VkImageView rawView = VK_NULL_HANDLE;
        if (!ImageBuilder(getAllocator())
                .setExtent(GrassConstants::DISPLACEMENT_TEXTURE_SIZE, GrassConstants::DISPLACEMENT_TEXTURE_SIZE)
                .setFormat(VK_FORMAT_R16G16_SFLOAT)
                .setUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
                .build(static_cast<VkDevice>(getDevice()), image, rawView)) {
            SDL_Log("Failed to create displacement image");
            return false;
        }
        displacementImageView_ = rawView;
        VkImage rawImage = VK_NULL_HANDLE;
        image.releaseToRaw(rawImage, displacementAllocation_);
        displacementImage_ = rawImage;
    }

    // Create sampler for grass compute shader to sample displacement
    displacementSampler_ = SamplerFactory::createSamplerLinearClamp(*raiiDevice_);
    if (!displacementSampler_) {
        SDL_Log("Failed to create displacement sampler");
        return false;
    }

    VkDeviceSize sourceBufferSize = sizeof(DisplacementSource) * GrassConstants::MAX_DISPLACEMENT_SOURCES;
    VkDeviceSize uniformBufferSize = sizeof(DisplacementUniforms);

    BufferUtils::PerFrameBufferBuilder sourceBuilder;
    if (!sourceBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(sourceBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
             .build(displacementSourceBuffers)) {
        SDL_Log("Failed to create displacement source buffers");
        return false;
    }

    BufferUtils::PerFrameBufferBuilder uniformBuilder;
    if (!uniformBuilder.setAllocator(getAllocator())
             .setFrameCount(getFramesInFlight())
             .setSize(uniformBufferSize)
             .build(displacementUniformBuffers)) {
        SDL_Log("Failed to create displacement uniform buffers");
        return false;
    }

    return true;
}

bool GrassSystem::createDisplacementPipeline() {
    // Create descriptor set layout for displacement update compute shader
    // 0: Displacement map (storage image, read-write)
    // 1: Source buffer (SSBO)
    // 2: Displacement uniforms

    VkDescriptorSetLayout rawDescSetLayout = DescriptorManager::LayoutBuilder(getDevice())
        .addStorageImage(VK_SHADER_STAGE_COMPUTE_BIT)      // 0: Displacement map
        .addStorageBuffer(VK_SHADER_STAGE_COMPUTE_BIT)     // 1: Source buffer
        .addUniformBuffer(VK_SHADER_STAGE_COMPUTE_BIT)     // 2: Displacement uniforms
        .build();

    if (rawDescSetLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create displacement descriptor set layout");
        return false;
    }
    // Adopt raw handle into RAII wrapper
    displacementDescriptorSetLayout_.emplace(*raiiDevice_, rawDescSetLayout);

    VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(
        getDevice(), **displacementDescriptorSetLayout_);
    if (rawPipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create displacement pipeline layout");
        return false;
    }
    displacementPipelineLayout_.emplace(*raiiDevice_, rawPipelineLayout);

    if (!ComputePipelineBuilder(*raiiDevice_)
            .setShader(getShaderPath() + "/grass_displacement.comp.spv")
            .setPipelineLayout(**displacementPipelineLayout_)
            .buildInto(displacementPipeline_)) {
        SDL_Log("Failed to create displacement compute pipeline");
        return false;
    }

    // Allocate per-frame displacement descriptor sets (double-buffered) using managed pool
    auto rawSets = getDescriptorPool()->allocate(**displacementDescriptorSetLayout_, getFramesInFlight());
    if (rawSets.empty()) {
        SDL_Log("Failed to allocate displacement descriptor sets");
        return false;
    }
    displacementDescriptorSets_.resize(rawSets.size());
    for (size_t i = 0; i < rawSets.size(); ++i) {
        displacementDescriptorSets_[i] = rawSets[i];
    }

    // Update each per-frame descriptor set with image and per-frame buffers
    for (uint32_t i = 0; i < getFramesInFlight(); ++i) {
        DescriptorManager::SetWriter(getDevice(), displacementDescriptorSets_[i])
            .writeStorageImage(0, displacementImageView_)
            .writeBuffer(1, displacementSourceBuffers.buffers[i], 0, sizeof(DisplacementSource) * GrassConstants::MAX_DISPLACEMENT_SOURCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, displacementUniformBuffers.buffers[i], 0, sizeof(DisplacementUniforms))
            .update();
    }

    return true;
}

bool GrassSystem::createComputeDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles) {
    PipelineBuilder builder(getDevice());
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)    // instance buffer
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)       // indirect buffer
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)       // CullingUniforms
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)  // terrain heightmap
        .addDescriptorBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)  // displacement map
        .addDescriptorBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)  // tile array
        .addDescriptorBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)          // tile info
        .addDescriptorBinding(7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);         // GrassParams

    return builder.buildDescriptorSetLayout(handles.descriptorSetLayout);
}

bool GrassSystem::createComputePipeline(SystemLifecycleHelper::PipelineHandles& handles) {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/grass.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TiledGrassPushConstants));

    if (!builder.buildPipelineLayout({handles.descriptorSetLayout}, handles.pipelineLayout)) {
        return false;
    }

    return builder.buildComputePipeline(handles.pipelineLayout, handles.pipeline);
}

bool GrassSystem::createGraphicsDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles) {
    PipelineBuilder builder(getDevice());
    // Grass system descriptor set layout:
    // binding 0: UBO (main rendering uniforms) - DYNAMIC to avoid per-frame descriptor updates
    // binding 1: instance buffer (SSBO) - vertex shader only
    // binding 2: shadow map (sampler)
    // binding 3: wind UBO - vertex shader only
    // binding 4: light buffer (SSBO)
    // binding 5: snow mask texture (sampler)
    // binding 6: cloud shadow map (sampler)
    // binding 10: snow UBO
    // binding 11: cloud shadow UBO
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildDescriptorSetLayout(handles.descriptorSetLayout);
}

bool GrassSystem::createGraphicsPipeline(SystemLifecycleHelper::PipelineHandles& handles) {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/grass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
        .addShaderStage(getShaderPath() + "/grass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(TiledGrassPushConstants));

    // No vertex input - procedural geometry from instance buffer
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleStrip);

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)  // No culling for grass
        .setFrontFace(vk::FrontFace::eCounterClockwise);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(true)
        .setDepthWriteEnable(true)
        .setDepthCompareOp(vk::CompareOp::eLess);

    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachments(colorBlendAttachment);

    // Enable dynamic viewport and scissor for window resize handling
    std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    if (!builder.buildPipelineLayout({handles.descriptorSetLayout}, handles.pipelineLayout)) {
        return false;
    }

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setRenderPass(getRenderPass())
        .setSubpass(0);

    return builder.buildGraphicsPipeline(pipelineInfo, handles.pipelineLayout, handles.pipeline);
}

bool GrassSystem::createShadowPipeline() {
    PipelineBuilder layoutBuilder(getDevice());
    layoutBuilder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);

    VkDescriptorSetLayout rawDescSetLayout = VK_NULL_HANDLE;
    if (!layoutBuilder.buildDescriptorSetLayout(rawDescSetLayout)) {
        return false;
    }
    // Adopt raw handle into RAII wrapper
    shadowDescriptorSetLayout_.emplace(*raiiDevice_, rawDescSetLayout);

    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/grass_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
        .addShaderStage(getShaderPath() + "/grass_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants));

    // No vertex input - procedural geometry from instance buffer
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleStrip);

    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(shadowMapSize_))
        .setHeight(static_cast<float>(shadowMapSize_))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent({shadowMapSize_, shadowMapSize_});

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewports(viewport)
        .setScissors(scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)  // No culling for grass
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(true)
        .setDepthBiasConstantFactor(GrassConstants::SHADOW_DEPTH_BIAS_CONSTANT)
        .setDepthBiasSlopeFactor(GrassConstants::SHADOW_DEPTH_BIAS_SLOPE);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(true)
        .setDepthWriteEnable(true)
        .setDepthCompareOp(vk::CompareOp::eLess);

    // No color attachment for shadow pass
    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{};

    VkPipelineLayout rawPipelineLayout = VK_NULL_HANDLE;
    if (!builder.buildPipelineLayout({**shadowDescriptorSetLayout_}, rawPipelineLayout)) {
        return false;
    }
    shadowPipelineLayout_.emplace(*raiiDevice_, rawPipelineLayout);

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setRenderPass(shadowRenderPass_)
        .setSubpass(0);

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(pipelineInfo, **shadowPipelineLayout_, rawPipeline)) {
        return false;
    }
    shadowPipeline_.emplace(*raiiDevice_, rawPipeline);

    return true;
}

bool GrassSystem::createDescriptorSets() {
    // Note: Standard compute/graphics descriptor sets are allocated by ParticleSystem::init()
    // after all hooks complete. This hook only allocates GrassSystem-specific descriptor sets.
    // Compute descriptor set updates happen later in writeComputeDescriptorSets() called after init.

    SDL_Log("GrassSystem::createDescriptorSets - pool=%p, shadowLayout=%p", (void*)getDescriptorPool(), (void*)**shadowDescriptorSetLayout_);
    SDL_Log("GrassSystem::createDescriptorSets - about to allocate shadow sets");

    // Allocate shadow descriptor sets for all buffer sets (matches frames in flight)
    uint32_t bufferSetCount = getFramesInFlight();
    shadowDescriptorSets_.resize(bufferSetCount);

    for (uint32_t set = 0; set < bufferSetCount; set++) {
        SDL_Log("GrassSystem::createDescriptorSets - allocating shadow set %u", set);
        shadowDescriptorSets_[set] = getDescriptorPool()->allocateSingle(**shadowDescriptorSetLayout_);
        SDL_Log("GrassSystem::createDescriptorSets - allocated shadow set %u", set);
        if (!shadowDescriptorSets_[set]) {
            SDL_Log("Failed to allocate grass shadow descriptor set (set %u)", set);
            return false;
        }
    }

    return true;
}

void GrassSystem::writeComputeDescriptorSets() {
    // Write compute descriptor sets with instance and indirect buffers
    // Called after ParticleSystem is fully initialized and descriptor sets are allocated
    // Note: Tile cache resources are written later in updateDescriptorSets when available
    uint32_t bufferSetCount = particleSystem->getBufferSetCount();
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        // Use non-fluent pattern to avoid copy semantics bug with DescriptorManager::SetWriter
        DescriptorManager::SetWriter writer(getDevice(), particleSystem->getComputeDescriptorSet(set));
        writer.writeBuffer(0, instanceBuffers.buffers[set], 0, sizeof(GrassInstance) * GrassConstants::MAX_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(1, indirectBuffers.buffers[set], 0, sizeof(VkDrawIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(2, uniformBuffers.buffers[0], 0, sizeof(CullingUniforms));
        writer.writeBuffer(7, paramsBuffers.buffers[0], 0, sizeof(GrassParams));
        writer.update();
    }
}

bool GrassSystem::createExtraPipelines(SystemLifecycleHelper::PipelineHandles& computeHandles,
                                        SystemLifecycleHelper::PipelineHandles& graphicsHandles) {
    if (!createDisplacementPipeline()) return false;
    if (!createShadowPipeline()) return false;

    // Create tiled grass compute pipeline
    if (tiledModeEnabled_) {
        PipelineBuilder builder(getDevice());
        builder.addShaderStage(getShaderPath() + "/grass_tiled.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT)
            .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TiledGrassPushConstants));

        // Use existing compute descriptor set layout and pipeline layout
        VkPipeline rawTiledPipeline = VK_NULL_HANDLE;
        if (!builder.buildComputePipeline(computeHandles.pipelineLayout, rawTiledPipeline)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tiled grass compute pipeline");
            return false;
        }
        tiledComputePipeline_.emplace(*raiiDevice_, rawTiledPipeline);
        SDL_Log("GrassSystem: Created tiled grass compute pipeline");

        // Initialize tile manager
        GrassTileManager::InitInfo tileInfo{};
        tileInfo.device = getDevice();
        tileInfo.allocator = getAllocator();
        tileInfo.descriptorPool = getDescriptorPool();
        tileInfo.framesInFlight = getFramesInFlight();
        tileInfo.shaderPath = getShaderPath();
        tileInfo.computeDescriptorSetLayout = computeHandles.descriptorSetLayout;
        tileInfo.computePipelineLayout = computeHandles.pipelineLayout;
        tileInfo.computePipeline = **tiledComputePipeline_;
        tileInfo.graphicsDescriptorSetLayout = graphicsHandles.descriptorSetLayout;
        tileInfo.graphicsPipelineLayout = graphicsHandles.pipelineLayout;
        tileInfo.graphicsPipeline = graphicsHandles.pipeline;

        tileManager_ = std::make_unique<GrassTileManager>();
        if (!tileManager_->init(tileInfo)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize GrassTileManager");
            tileManager_.reset();
            tiledModeEnabled_ = false;
        }
    }

    return true;
}

void GrassSystem::updateDescriptorSets(vk::Device dev, const std::vector<vk::Buffer>& rendererUniformBuffers,
                                        vk::ImageView shadowMapView, vk::Sampler shadowSampler,
                                        const std::vector<vk::Buffer>& windBuffers,
                                        const std::vector<vk::Buffer>& lightBuffersParam,
                                        vk::ImageView terrainHeightMapViewParam, vk::Sampler terrainHeightMapSamplerParam,
                                        const std::vector<vk::Buffer>& snowBuffersParam,
                                        const std::vector<vk::Buffer>& cloudShadowBuffersParam,
                                        vk::ImageView cloudShadowMapView, vk::Sampler cloudShadowMapSampler,
                                        vk::ImageView tileArrayViewParam,
                                        vk::Sampler tileSamplerParam,
                                        const std::array<vk::Buffer, 3>& tileInfoBuffersParam,
                                        const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO) {
    // Store terrain heightmap info for compute descriptor set updates
    terrainHeightMapView_ = terrainHeightMapViewParam;
    terrainHeightMapSampler_ = terrainHeightMapSamplerParam;

    // Store tile cache resources (triple-buffered tile info)
    tileArrayView_ = tileArrayViewParam;
    tileSampler_ = tileSamplerParam;
    tileInfoBuffers_.resize(tileInfoBuffersParam.size());
    for (size_t i = 0; i < tileInfoBuffersParam.size(); ++i) {
        tileInfoBuffers_[i] = tileInfoBuffersParam[i];
    }

    // Store renderer uniform buffers (kept for backward compatibility)
    rendererUniformBuffers_ = rendererUniformBuffers;

    // Store dynamic renderer UBO reference for per-frame binding with dynamic offsets
    dynamicRendererUBO_ = dynamicRendererUBO;

    // Update compute descriptor sets with terrain heightmap, displacement, and tile cache
    // Note: Bindings 0, 1, 2 are already written in writeComputeDescriptorSets() - only write new bindings here
    // Note: tile info buffer (binding 6) is updated per-frame in recordResetAndCompute
    uint32_t bufferSetCount = particleSystem->getBufferSetCount();
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        // Use non-fluent pattern to avoid copy semantics bug with DescriptorManager::SetWriter
        DescriptorManager::SetWriter computeWriter(dev, particleSystem->getComputeDescriptorSet(set));
        computeWriter.writeImage(3, terrainHeightMapView_, terrainHeightMapSampler_);
        computeWriter.writeImage(4, displacementImageView_, **displacementSampler_);

        // Tile cache bindings (5 and 6) - for high-res terrain sampling
        if (tileArrayView_) {
            computeWriter.writeImage(5, tileArrayView_, tileSampler_);
        }
        // Write initial tile info buffer (frame 0) - will be updated per-frame
        if (!tileInfoBuffers_.empty() && tileInfoBuffers_[0]) {
            computeWriter.writeBuffer(6, tileInfoBuffers_[0], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        computeWriter.update();
    }

    // Update graphics and shadow descriptor sets for all buffer sets
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        // Graphics descriptor set - use non-fluent pattern
        DescriptorManager::SetWriter graphicsWriter(dev, particleSystem->getGraphicsDescriptorSet(set));
        // Use dynamic UBO if available (avoids per-frame descriptor updates)
        if (dynamicRendererUBO && dynamicRendererUBO->isValid()) {
            graphicsWriter.writeBuffer(0, dynamicRendererUBO->buffer, 0, dynamicRendererUBO->alignedSize,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
        } else {
            graphicsWriter.writeBuffer(0, rendererUniformBuffers[0], 0, 160,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);  // sizeof(UniformBufferObject)
        }
        graphicsWriter.writeBuffer(1, instanceBuffers.buffers[set], 0, sizeof(GrassInstance) * GrassConstants::MAX_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        graphicsWriter.writeImage(2, shadowMapView, shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        graphicsWriter.writeBuffer(3, windBuffers[0], 0, 32);  // sizeof(WindUniforms)
        graphicsWriter.writeBuffer(4, lightBuffersParam[0], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        graphicsWriter.writeImage(6, cloudShadowMapView, cloudShadowMapSampler);
        graphicsWriter.writeBuffer(10, snowBuffersParam[0], 0, sizeof(SnowUBO));
        graphicsWriter.writeBuffer(11, cloudShadowBuffersParam[0], 0, sizeof(CloudShadowUBO));
        graphicsWriter.update();

        // Shadow descriptor set - use non-fluent pattern
        DescriptorManager::SetWriter shadowWriter(dev, shadowDescriptorSets_[set]);
        shadowWriter.writeBuffer(0, rendererUniformBuffers[0], 0, 160);  // sizeof(UniformBufferObject)
        shadowWriter.writeBuffer(1, instanceBuffers.buffers[set], 0, sizeof(GrassInstance) * GrassConstants::MAX_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        shadowWriter.writeBuffer(2, windBuffers[0], 0, 32);  // sizeof(WindUniforms)
        shadowWriter.update();
    }

    // Update tile manager descriptor sets if in tiled mode
    if (tiledModeEnabled_ && tileManager_) {
        // Set shared buffers for tile manager to use (all tiles write to these)
        uint32_t firstBufferSet = 0;  // Will be updated per-frame in recordCompute
        tileManager_->setSharedBuffers(
            instanceBuffers.buffers[firstBufferSet],
            indirectBuffers.buffers[firstBufferSet]
        );

        // Convert uniform buffer vectors for tile manager
        std::vector<vk::Buffer> vkCullingBuffers(uniformBuffers.buffers.begin(), uniformBuffers.buffers.end());
        std::vector<vk::Buffer> vkParamsBuffers(paramsBuffers.buffers.begin(), paramsBuffers.buffers.end());

        // Convert TripleBuffered to std::array for tile info buffers
        std::array<vk::Buffer, 3> tileInfoArray{};
        for (size_t i = 0; i < tileInfoBuffers_.size() && i < 3; ++i) {
            tileInfoArray[i] = tileInfoBuffers_[i];
        }

        tileManager_->updateDescriptorSets(
            terrainHeightMapView_, terrainHeightMapSampler_,
            displacementImageView_, **displacementSampler_,
            tileArrayView_, tileSampler_,
            tileInfoArray,
            vkCullingBuffers,
            vkParamsBuffers
        );
    }
}

void GrassSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                                  float terrainSize, float terrainHeightScale, float time) {
    // Fill CullingUniforms (shared culling parameters) using unified constants
    CullingUniforms culling{};
    culling.cameraPosition = glm::vec4(cameraPos, 1.0f);
    extractFrustumPlanes(viewProj, culling.frustumPlanes);
    culling.maxDrawDistance = GrassConstants::MAX_DRAW_DISTANCE;
    // Disable legacy LOD dropping - now handled by tile-based LOD system
    culling.lodTransitionStart = -1.0f;  // Sentinel: disabled
    culling.lodTransitionEnd = -1.0f;
    culling.maxLodDropRate = 0.0f;
    memcpy(uniformBuffers.mappedPointers[frameIndex], &culling, sizeof(CullingUniforms));

    // Fill GrassParams (grass-specific parameters)
    GrassParams params{};

    // Update displacement region to follow camera
    displacementRegionCenter = glm::vec2(cameraPos.x, cameraPos.z);

    // Displacement region info for grass compute shader using unified constants
    // xy = world center, z = region size, w = texel size (derived from DISPLACEMENT_REGION_SIZE / DISPLACEMENT_TEXTURE_SIZE)
    params.displacementRegion = glm::vec4(displacementRegionCenter.x, displacementRegionCenter.y,
                                          GrassConstants::DISPLACEMENT_REGION_SIZE,
                                          GrassConstants::DISPLACEMENT_TEXEL_SIZE);

    // Terrain parameters for heightmap sampling
    params.terrainSize = terrainSize;
    params.terrainHeightScale = terrainHeightScale;
    memcpy(paramsBuffers.mappedPointers[frameIndex], &params, sizeof(GrassParams));

    // Update active tiles in tiled mode based on camera position
    if (tiledModeEnabled_ && tileManager_) {
        frameCounter_++;
        tileManager_->updateActiveTiles(cameraPos, frameCounter_, time);
    }
}

void GrassSystem::updateDisplacementSources(const glm::vec3& playerPos, float playerRadius, float deltaTime) {
    // Clear previous sources
    currentDisplacementSources.clear();

    // Add player as displacement source
    DisplacementSource playerSource;
    playerSource.positionAndRadius = glm::vec4(playerPos, playerRadius * 2.0f);  // Influence radius larger than capsule
    playerSource.strengthAndVelocity = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);  // Full strength, no velocity for now
    currentDisplacementSources.push_back(playerSource);

    // Future: Add NPCs, projectiles, etc. here
}

void GrassSystem::recordDisplacementUpdate(vk::CommandBuffer cmd, uint32_t frameIndex) {
    // Copy displacement sources to per-frame buffer (double-buffered)
    memcpy(displacementSourceBuffers.mappedPointers[frameIndex], currentDisplacementSources.data(),
           sizeof(DisplacementSource) * currentDisplacementSources.size());

    // Update displacement uniforms using unified constants
    DisplacementUniforms dispUniforms;
    dispUniforms.regionCenter = glm::vec4(displacementRegionCenter.x, displacementRegionCenter.y,
                                          GrassConstants::DISPLACEMENT_REGION_SIZE,
                                          GrassConstants::DISPLACEMENT_TEXEL_SIZE);
    const EnvironmentSettings fallbackSettings{};
    const EnvironmentSettings& settings = environmentSettings ? *environmentSettings : fallbackSettings;
    dispUniforms.params = glm::vec4(settings.grassDisplacementDecay, settings.grassMaxDisplacement, 1.0f / 60.0f,
                                    static_cast<float>(currentDisplacementSources.size()));
    memcpy(displacementUniformBuffers.mappedPointers[frameIndex], &dispUniforms, sizeof(DisplacementUniforms));

    // Transition displacement image to general layout if needed (first frame)
    // For subsequent frames, it should already be in GENERAL layout
    BarrierHelpers::imageToGeneral(cmd, displacementImage_);

    // Dispatch displacement update compute shader using per-frame descriptor set (double-buffered)
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, **displacementPipeline_);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           **displacementPipelineLayout_, 0,
                           displacementDescriptorSets_[frameIndex], {});

    // Dispatch using derived constant: DISPLACEMENT_DISPATCH_SIZE = DISPLACEMENT_TEXTURE_SIZE / WORKGROUP_SIZE
    cmd.dispatch(GrassConstants::DISPLACEMENT_DISPATCH_SIZE, GrassConstants::DISPLACEMENT_DISPATCH_SIZE, 1);

    // Barrier: displacement compute write -> grass compute read
    BarrierHelpers::imageToShaderRead(cmd, displacementImage_, vk::PipelineStageFlagBits::eComputeShader);
}

void GrassSystem::recordResetAndCompute(vk::CommandBuffer cmd, uint32_t frameIndex, float time) {
    // Double-buffer: compute writes to computeBufferSet
    uint32_t writeSet = particleSystem->getComputeBufferSet();

    // Ensure CPU writes to tile info buffer are visible to GPU before compute dispatch
    auto hostBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eComputeShader,
                        {}, hostBarrier, {}, {});

    // Use tiled mode if enabled and tile manager is available
    if (tiledModeEnabled_ && tileManager_) {
        // Set the correct buffer set for shared buffers before compute
        tileManager_->setSharedBuffers(
            instanceBuffers.buffers[writeSet],
            indirectBuffers.buffers[writeSet]
        );

        // Tiled mode: dispatch compute for each active tile
        tileManager_->recordCompute(cmd, frameIndex, time, writeSet);
        return;
    }

    // Legacy non-tiled mode (fallback)
    // Update compute descriptor set with per-frame buffers only
    // Note: Static images (bindings 3, 4, 5) are already bound in updateDescriptorSets()
    DescriptorManager::SetWriter writer(getDevice(), particleSystem->getComputeDescriptorSet(writeSet));
    writer.writeBuffer(2, uniformBuffers.buffers[frameIndex], 0, sizeof(CullingUniforms));
    writer.writeBuffer(7, paramsBuffers.buffers[frameIndex], 0, sizeof(GrassParams));

    // Update tile info buffer to the correct frame's buffer (triple-buffered to avoid CPU-GPU sync)
    if (!tileInfoBuffers_.empty() && tileInfoBuffers_.at(frameIndex)) {
        writer.writeBuffer(6, tileInfoBuffers_.at(frameIndex), 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }
    writer.update();

    // Reset indirect buffer before compute dispatch to prevent accumulation
    cmd.fillBuffer(indirectBuffers.buffers[writeSet], 0, sizeof(VkDrawIndirectCommand), 0);
    auto clearBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                        {}, clearBarrier, {}, {});

    // Dispatch grass compute shader using the compute buffer set
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, getComputePipelineHandles().pipeline);
    VkDescriptorSet computeSet = particleSystem->getComputeDescriptorSet(writeSet);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           getComputePipelineHandles().pipelineLayout, 0,
                           vk::DescriptorSet(computeSet), {});

    // Use extended push constants even in legacy mode (with zero tile origin)
    TiledGrassPushConstants grassPush{};
    grassPush.time = time;
    grassPush.tileOriginX = 0.0f;
    grassPush.tileOriginZ = 0.0f;
    grassPush.tileSize = GrassConstants::TILE_SIZE_LOD0;
    grassPush.spacingMult = 1.0f;
    grassPush.lodLevel = 0;
    grassPush.tileLoadTime = 0.0f;  // Legacy mode: no fade-in needed
    grassPush.padding = 0.0f;
    cmd.pushConstants<TiledGrassPushConstants>(
        getComputePipelineHandles().pipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0, grassPush);

    // Dispatch using derived constant: DISPATCH_SIZE = ceil(GRID_SIZE / WORKGROUP_SIZE)
    // Grid: GRID_SIZE x GRID_SIZE blades, Workgroup: WORKGROUP_SIZE x WORKGROUP_SIZE threads
    cmd.dispatch(GrassConstants::DISPATCH_SIZE, GrassConstants::DISPATCH_SIZE, 1);

    // Memory barrier: compute write -> vertex shader read (storage buffer) and indirect read
    // Note: This barrier ensures the compute results are visible when we draw from this buffer
    // in the NEXT frame (after advanceBufferSet swaps the sets)
    auto computeBarrier = vk::MemoryBarrier{}
        .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eIndirectCommandRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eDrawIndirect | vk::PipelineStageFlagBits::eVertexShader,
                        {}, computeBarrier, {}, {});
}

void GrassSystem::recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time) {
    // Double-buffer: graphics reads from renderBufferSet (previous frame's compute output)
    uint32_t readSet = particleSystem->getRenderBufferSet();

    // Set dynamic viewport and scissor to handle window resize
    vk::Extent2D ext = getExtent();
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(ext.width))
        .setHeight(static_cast<float>(ext.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    cmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(ext);
    cmd.setScissor(0, scissor);

    // Use tiled mode if enabled and tile manager is available
    if (tiledModeEnabled_ && tileManager_) {
        VkDescriptorSet graphicsSet = particleSystem->getGraphicsDescriptorSet(readSet);
        tileManager_->recordDraw(cmd, frameIndex, time, readSet,
                                  getGraphicsPipelineHandles().pipeline,
                                  getGraphicsPipelineHandles().pipelineLayout,
                                  graphicsSet,
                                  indirectBuffers.buffers[readSet],
                                  dynamicRendererUBO_);
        return;
    }

    // Legacy non-tiled mode
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, getGraphicsPipelineHandles().pipeline);

    VkDescriptorSet graphicsSet = particleSystem->getGraphicsDescriptorSet(readSet);

    // Use dynamic offset for binding 0 (renderer UBO) if dynamic buffer is available
    if (dynamicRendererUBO_ && dynamicRendererUBO_->isValid()) {
        uint32_t dynamicOffset = dynamicRendererUBO_->getDynamicOffset(frameIndex);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               getGraphicsPipelineHandles().pipelineLayout, 0,
                               vk::DescriptorSet(graphicsSet), dynamicOffset);
    } else {
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                               getGraphicsPipelineHandles().pipelineLayout, 0,
                               vk::DescriptorSet(graphicsSet), {});
    }

    TiledGrassPushConstants grassPush{};
    grassPush.time = time;
    grassPush.tileOriginX = 0.0f;
    grassPush.tileOriginZ = 0.0f;
    grassPush.tileSize = GrassConstants::TILE_SIZE_LOD0;
    grassPush.spacingMult = 1.0f;
    grassPush.lodLevel = 0;
    grassPush.tileLoadTime = 0.0f;  // Not used in graphics pass
    grassPush.padding = 0.0f;
    cmd.pushConstants<TiledGrassPushConstants>(
        getGraphicsPipelineHandles().pipelineLayout,
        vk::ShaderStageFlagBits::eVertex,
        0, grassPush);

    cmd.drawIndirect(indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void GrassSystem::recordShadowDraw(vk::CommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex) {
    // Double-buffer: shadow pass reads from renderBufferSet (same as main draw)
    uint32_t readSet = particleSystem->getRenderBufferSet();

    // Update shadow descriptor set to use this frame's renderer UBO
    // Bounds check: frameIndex must be within range, not just non-empty
    if (frameIndex < rendererUniformBuffers_.size()) {
        DescriptorManager::SetWriter(getDevice(), shadowDescriptorSets_[readSet])
            .writeBuffer(0, rendererUniformBuffers_[frameIndex], 0, 160)
            .update();
    }

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **shadowPipeline_);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           **shadowPipelineLayout_, 0,
                           shadowDescriptorSets_[readSet], {});

    GrassPushConstants grassPush{};
    grassPush.time = time;
    grassPush.cascadeIndex = static_cast<int>(cascadeIndex);
    cmd.pushConstants<GrassPushConstants>(
        **shadowPipelineLayout_,
        vk::ShaderStageFlagBits::eVertex,
        0, grassPush);

    cmd.drawIndirect(indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void GrassSystem::setSnowMask(vk::Device device, vk::ImageView snowMaskView, vk::Sampler snowMaskSampler) {
    // Update graphics descriptor sets with snow mask texture
    uint32_t bufferSetCount = particleSystem->getBufferSetCount();
    for (uint32_t setIndex = 0; setIndex < bufferSetCount; setIndex++) {
        DescriptorManager::SetWriter(device, particleSystem->getGraphicsDescriptorSet(setIndex))
            .writeImage(5, snowMaskView, snowMaskSampler)
            .update();
    }
}

// Backward-compatible overload: convert raw Vulkan types to vk:: types
void GrassSystem::updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& uniformBuffers,
                                        VkImageView shadowMapView, VkSampler shadowSampler,
                                        const std::vector<VkBuffer>& windBuffers,
                                        const std::vector<VkBuffer>& lightBuffersParam,
                                        VkImageView terrainHeightMapViewParam, VkSampler terrainHeightMapSamplerParam,
                                        const std::vector<VkBuffer>& snowBuffersParam,
                                        const std::vector<VkBuffer>& cloudShadowBuffersParam,
                                        VkImageView cloudShadowMapView, VkSampler cloudShadowMapSampler,
                                        VkImageView tileArrayViewParam,
                                        VkSampler tileSamplerParam,
                                        const std::array<VkBuffer, 3>& tileInfoBuffersParam,
                                        const BufferUtils::DynamicUniformBuffer* dynamicRendererUBO) {
    // Convert vectors
    std::vector<vk::Buffer> vkUniformBuffers(uniformBuffers.begin(), uniformBuffers.end());
    std::vector<vk::Buffer> vkWindBuffers(windBuffers.begin(), windBuffers.end());
    std::vector<vk::Buffer> vkLightBuffers(lightBuffersParam.begin(), lightBuffersParam.end());
    std::vector<vk::Buffer> vkSnowBuffers(snowBuffersParam.begin(), snowBuffersParam.end());
    std::vector<vk::Buffer> vkCloudShadowBuffers(cloudShadowBuffersParam.begin(), cloudShadowBuffersParam.end());
    std::array<vk::Buffer, 3> vkTileInfoBuffers;
    for (size_t i = 0; i < 3; ++i) {
        vkTileInfoBuffers[i] = tileInfoBuffersParam[i];
    }

    // Call the vk:: version
    updateDescriptorSets(
        vk::Device(device),
        vkUniformBuffers,
        vk::ImageView(shadowMapView),
        vk::Sampler(shadowSampler),
        vkWindBuffers,
        vkLightBuffers,
        vk::ImageView(terrainHeightMapViewParam),
        vk::Sampler(terrainHeightMapSamplerParam),
        vkSnowBuffers,
        vkCloudShadowBuffers,
        vk::ImageView(cloudShadowMapView),
        vk::Sampler(cloudShadowMapSampler),
        vk::ImageView(tileArrayViewParam),
        vk::Sampler(tileSamplerParam),
        vkTileInfoBuffers,
        dynamicRendererUBO);
}

void GrassSystem::advanceBufferSet() {
    particleSystem->advanceBufferSet();
}
