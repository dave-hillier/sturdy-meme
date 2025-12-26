#include "GrassSystem.h"
#include "CullCommon.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "DescriptorManager.h"
#include "UBOs.h"
#include "VulkanBarriers.h"
#include "VulkanResourceFactory.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <array>

using namespace vk;  // Vulkan-Hpp type-safe wrappers

// Forward declare UniformBufferObject size (needed for descriptor set update)
struct UniformBufferObject;

std::unique_ptr<GrassSystem> GrassSystem::create(const InitInfo& info) {
    std::unique_ptr<GrassSystem> system(new GrassSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

GrassSystem::~GrassSystem() {
    cleanup();
}

bool GrassSystem::initInternal(const InitInfo& info) {
    SDL_Log("GrassSystem::init() starting, device=%p, pool=%p", (void*)info.device, (void*)info.descriptorPool);
    shadowRenderPass = info.shadowRenderPass;
    shadowMapSize = info.shadowMapSize;

    // Store init info for accessors used during initialization
    storedDevice = info.device;
    storedAllocator = info.allocator;
    storedRenderPass = info.renderPass;
    storedDescriptorPool = info.descriptorPool;
    storedExtent = info.extent;
    storedShaderPath = info.shaderPath;
    storedFramesInFlight = info.framesInFlight;

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
    hooks.createExtraPipelines = [this]() { return createExtraPipelines(); };
    hooks.createDescriptorSets = [this]() { return createDescriptorSets(); };
    hooks.destroyBuffers = [this](VmaAllocator allocator) { destroyBuffers(allocator); };

    particleSystem = RAIIAdapter<ParticleSystem>::create(
        [&](auto& ps) {
            initializingPS = &ps;  // Store pointer for hooks to use
            // Use framesInFlight for buffer set count to ensure proper triple buffering
            return ps.init(info, hooks, info.framesInFlight);
        },
        [](auto& ps) { ps.destroy(ps.getDevice(), ps.getAllocator()); }
    );

    if (!particleSystem.has_value()) {
        return false;
    }

    SDL_Log("GrassSystem::init() - particleSystem created successfully");

    // Write compute descriptor sets now that particleSystem is fully initialized
    writeComputeDescriptorSets();
    SDL_Log("GrassSystem::init() - done writing compute descriptor sets");
    return true;
}

void GrassSystem::cleanup() {
    if (!storedDevice) return;  // Not initialized

    // RAII wrappers automatically clean up: shadowPipeline_, shadowPipelineLayout_,
    // shadowDescriptorSetLayout_, displacementPipeline_, displacementPipelineLayout_,
    // displacementDescriptorSetLayout_, displacementSampler_

    if (displacementImageView) {
        vkDestroyImageView(storedDevice, displacementImageView, nullptr);
        displacementImageView = VK_NULL_HANDLE;
    }
    if (displacementImage && storedAllocator) {
        vmaDestroyImage(storedAllocator, displacementImage, displacementAllocation);
        displacementImage = VK_NULL_HANDLE;
        displacementAllocation = VK_NULL_HANDLE;
    }

    particleSystem.reset();
}

void GrassSystem::destroyBuffers(VmaAllocator alloc) {
    BufferUtils::destroyBuffers(alloc, displacementSourceBuffers);
    BufferUtils::destroyBuffers(alloc, displacementUniformBuffers);

    BufferUtils::destroyBuffers(alloc, instanceBuffers);
    BufferUtils::destroyBuffers(alloc, indirectBuffers);
    BufferUtils::destroyBuffers(alloc, uniformBuffers);
}

bool GrassSystem::createBuffers() {
    VkDeviceSize instanceBufferSize = sizeof(GrassInstance) * MAX_INSTANCES;
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
    VkDeviceSize uniformBufferSize = sizeof(GrassUniforms);

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
             .setSize(uniformBufferSize)
             .build(uniformBuffers)) {
        SDL_Log("Failed to create grass uniform buffers");
        return false;
    }

    return createDisplacementResources();
}

bool GrassSystem::createDisplacementResources() {
    // Create displacement texture (RG16F, 512x512) using Vulkan-Hpp
    ImageCreateInfo imageInfo{
        {},                                  // flags
        ImageType::e2D,
        Format::eR16G16Sfloat,               // RG16F for XZ displacement
        Extent3D{DISPLACEMENT_TEXTURE_SIZE, DISPLACEMENT_TEXTURE_SIZE, 1},
        1, 1,                                // mipLevels, arrayLayers
        SampleCountFlagBits::e1,
        ImageTiling::eOptimal,
        ImageUsageFlagBits::eStorage | ImageUsageFlagBits::eSampled,
        SharingMode::eExclusive,
        0, nullptr,                          // queueFamilyIndexCount, pQueueFamilyIndices
        ImageLayout::eUndefined
    };

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    auto vkImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
    if (vmaCreateImage(getAllocator(), &vkImageInfo, &allocInfo,
                       &displacementImage, &displacementAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create displacement image");
        return false;
    }

    // Create image view using Vulkan-Hpp
    ImageViewCreateInfo viewInfo{
        {},                                  // flags
        displacementImage,
        ImageViewType::e2D,
        Format::eR16G16Sfloat,
        ComponentMapping{},                  // identity swizzle
        ImageSubresourceRange{ImageAspectFlagBits::eColor, 0, 1, 0, 1}
    };

    auto vkViewInfo = static_cast<VkImageViewCreateInfo>(viewInfo);
    if (vkCreateImageView(getDevice(), &vkViewInfo, nullptr, &displacementImageView) != VK_SUCCESS) {
        SDL_Log("Failed to create displacement image view");
        return false;
    }

    // Create sampler for grass compute shader to sample displacement
    if (!VulkanResourceFactory::createSamplerLinearClamp(getDevice(), displacementSampler_)) {
        SDL_Log("Failed to create displacement sampler");
        return false;
    }

    VkDeviceSize sourceBufferSize = sizeof(DisplacementSource) * MAX_DISPLACEMENT_SOURCES;
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
    displacementDescriptorSetLayout_ = ManagedDescriptorSetLayout::fromRaw(getDevice(), rawDescSetLayout);

    VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(
        getDevice(), displacementDescriptorSetLayout_.get());
    if (rawPipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create displacement pipeline layout");
        return false;
    }
    displacementPipelineLayout_ = ManagedPipelineLayout::fromRaw(getDevice(), rawPipelineLayout);

    // Load compute shader
    auto compShaderCode = ShaderLoader::readFile(getShaderPath() + "/grass_displacement.comp.spv");
    if (!compShaderCode) {
        SDL_Log("Failed to load displacement compute shader");
        return false;
    }

    auto compShaderModule = ShaderLoader::createShaderModule(getDevice(), *compShaderCode);
    if (!compShaderModule) {
        SDL_Log("Failed to create displacement compute shader module");
        return false;
    }

    PipelineShaderStageCreateInfo shaderStageInfo{
        {},                              // flags
        ShaderStageFlagBits::eCompute,
        *compShaderModule,
        "main"
    };

    ComputePipelineCreateInfo pipelineInfo{
        {},                              // flags
        shaderStageInfo,
        displacementPipelineLayout_.get()
    };

    auto vkPipelineInfo = static_cast<VkComputePipelineCreateInfo>(pipelineInfo);
    bool success = ManagedPipeline::createCompute(getDevice(), VK_NULL_HANDLE, vkPipelineInfo, displacementPipeline_);

    vkDestroyShaderModule(getDevice(), *compShaderModule, nullptr);

    if (!success) {
        SDL_Log("Failed to create displacement compute pipeline");
        return false;
    }

    // Allocate per-frame displacement descriptor sets (double-buffered) using managed pool
    displacementDescriptorSets = getDescriptorPool()->allocate(displacementDescriptorSetLayout_.get(), getFramesInFlight());
    if (displacementDescriptorSets.empty()) {
        SDL_Log("Failed to allocate displacement descriptor sets");
        return false;
    }

    // Update each per-frame descriptor set with image and per-frame buffers
    for (uint32_t i = 0; i < getFramesInFlight(); ++i) {
        DescriptorManager::SetWriter(getDevice(), displacementDescriptorSets[i])
            .writeStorageImage(0, displacementImageView)
            .writeBuffer(1, displacementSourceBuffers.buffers[i], 0, sizeof(DisplacementSource) * MAX_DISPLACEMENT_SOURCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .writeBuffer(2, displacementUniformBuffers.buffers[i], 0, sizeof(DisplacementUniforms))
            .update();
    }

    return true;
}

bool GrassSystem::createComputeDescriptorSetLayout(SystemLifecycleHelper::PipelineHandles& handles) {
    PipelineBuilder builder(getDevice());
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)    // instance buffer
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)       // indirect buffer
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)       // uniforms
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)  // terrain heightmap
        .addDescriptorBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)  // displacement map
        .addDescriptorBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)  // tile array
        .addDescriptorBinding(6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT);         // tile info

    return builder.buildDescriptorSetLayout(handles.descriptorSetLayout);
}

bool GrassSystem::createComputePipeline(SystemLifecycleHelper::PipelineHandles& handles) {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/grass.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GrassPushConstants));

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
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants));

    // No vertex input - procedural geometry from instance buffer (Vulkan-Hpp)
    PipelineVertexInputStateCreateInfo vertexInputInfo{};

    PipelineInputAssemblyStateCreateInfo inputAssembly{
        {},                              // flags
        PrimitiveTopology::eTriangleStrip,
        VK_FALSE                         // primitiveRestartEnable
    };

    Viewport viewport{
        0.0f, 0.0f,
        static_cast<float>(getExtent().width),
        static_cast<float>(getExtent().height),
        0.0f, 1.0f
    };

    Rect2D scissor{{0, 0}, getExtent()};

    PipelineViewportStateCreateInfo viewportState{
        {},                              // flags
        1, &viewport,
        1, &scissor
    };

    PipelineRasterizationStateCreateInfo rasterizer{
        {},                              // flags
        VK_FALSE,                        // depthClampEnable
        VK_FALSE,                        // rasterizerDiscardEnable
        PolygonMode::eFill,
        CullModeFlagBits::eNone,         // No culling for grass
        FrontFace::eCounterClockwise,
        VK_FALSE,                        // depthBiasEnable
        0.0f, 0.0f, 0.0f,                // depthBias factors
        1.0f                             // lineWidth
    };

    PipelineMultisampleStateCreateInfo multisampling{
        {},                              // flags
        SampleCountFlagBits::e1,
        VK_FALSE                         // sampleShadingEnable
    };

    PipelineDepthStencilStateCreateInfo depthStencil{
        {},                              // flags
        VK_TRUE,                         // depthTestEnable
        VK_TRUE,                         // depthWriteEnable
        CompareOp::eLess,
        VK_FALSE,                        // depthBoundsTestEnable
        VK_FALSE                         // stencilTestEnable
    };

    PipelineColorBlendAttachmentState colorBlendAttachment{
        VK_FALSE,                        // blendEnable
        {}, {}, {}, {}, {}, {},          // blend factors (unused)
        ColorComponentFlagBits::eR | ColorComponentFlagBits::eG |
        ColorComponentFlagBits::eB | ColorComponentFlagBits::eA
    };

    PipelineColorBlendStateCreateInfo colorBlending{
        {},                              // flags
        VK_FALSE,                        // logicOpEnable
        {},                              // logicOp
        1, &colorBlendAttachment
    };

    // Enable dynamic viewport and scissor for window resize handling
    std::array<DynamicState, 2> dynamicStates = {DynamicState::eViewport, DynamicState::eScissor};
    PipelineDynamicStateCreateInfo dynamicState{
        {},                              // flags
        dynamicStates
    };

    if (!builder.buildPipelineLayout({handles.descriptorSetLayout}, handles.pipelineLayout)) {
        return false;
    }

    // Cast to C types for PipelineBuilder compatibility
    auto vkVertexInputInfo = static_cast<VkPipelineVertexInputStateCreateInfo>(vertexInputInfo);
    auto vkInputAssembly = static_cast<VkPipelineInputAssemblyStateCreateInfo>(inputAssembly);
    auto vkViewportState = static_cast<VkPipelineViewportStateCreateInfo>(viewportState);
    auto vkRasterizer = static_cast<VkPipelineRasterizationStateCreateInfo>(rasterizer);
    auto vkMultisampling = static_cast<VkPipelineMultisampleStateCreateInfo>(multisampling);
    auto vkDepthStencil = static_cast<VkPipelineDepthStencilStateCreateInfo>(depthStencil);
    auto vkColorBlending = static_cast<VkPipelineColorBlendStateCreateInfo>(colorBlending);
    auto vkDynamicState = static_cast<VkPipelineDynamicStateCreateInfo>(dynamicState);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pVertexInputState = &vkVertexInputInfo;
    pipelineInfo.pInputAssemblyState = &vkInputAssembly;
    pipelineInfo.pViewportState = &vkViewportState;
    pipelineInfo.pRasterizationState = &vkRasterizer;
    pipelineInfo.pMultisampleState = &vkMultisampling;
    pipelineInfo.pDepthStencilState = &vkDepthStencil;
    pipelineInfo.pColorBlendState = &vkColorBlending;
    pipelineInfo.pDynamicState = &vkDynamicState;
    pipelineInfo.renderPass = getRenderPass();
    pipelineInfo.subpass = 0;

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
    shadowDescriptorSetLayout_ = ManagedDescriptorSetLayout::fromRaw(getDevice(), rawDescSetLayout);

    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/grass_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
        .addShaderStage(getShaderPath() + "/grass_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants));

    // No vertex input - procedural geometry from instance buffer (Vulkan-Hpp)
    PipelineVertexInputStateCreateInfo vertexInputInfo{};

    PipelineInputAssemblyStateCreateInfo inputAssembly{
        {},                              // flags
        PrimitiveTopology::eTriangleStrip,
        VK_FALSE                         // primitiveRestartEnable
    };

    Viewport viewport{
        0.0f, 0.0f,
        static_cast<float>(shadowMapSize),
        static_cast<float>(shadowMapSize),
        0.0f, 1.0f
    };

    Rect2D scissor{{0, 0}, {shadowMapSize, shadowMapSize}};

    PipelineViewportStateCreateInfo viewportState{
        {},                              // flags
        1, &viewport,
        1, &scissor
    };

    PipelineRasterizationStateCreateInfo rasterizer{
        {},                              // flags
        VK_FALSE,                        // depthClampEnable
        VK_FALSE,                        // rasterizerDiscardEnable
        PolygonMode::eFill,
        CullModeFlagBits::eNone,         // No culling for grass
        FrontFace::eCounterClockwise,
        VK_TRUE,                         // depthBiasEnable
        0.25f,                           // depthBiasConstantFactor
        0.0f,                            // depthBiasClamp
        0.75f,                           // depthBiasSlopeFactor
        1.0f                             // lineWidth
    };

    PipelineMultisampleStateCreateInfo multisampling{
        {},                              // flags
        SampleCountFlagBits::e1,
        VK_FALSE                         // sampleShadingEnable
    };

    PipelineDepthStencilStateCreateInfo depthStencil{
        {},                              // flags
        VK_TRUE,                         // depthTestEnable
        VK_TRUE,                         // depthWriteEnable
        CompareOp::eLess,
        VK_FALSE,                        // depthBoundsTestEnable
        VK_FALSE                         // stencilTestEnable
    };

    // No color attachment for shadow pass
    PipelineColorBlendStateCreateInfo colorBlending{
        {},                              // flags
        VK_FALSE,                        // logicOpEnable
        {},                              // logicOp
        0, nullptr                       // no attachments
    };

    VkPipelineLayout rawPipelineLayout = VK_NULL_HANDLE;
    if (!builder.buildPipelineLayout({shadowDescriptorSetLayout_.get()}, rawPipelineLayout)) {
        return false;
    }
    shadowPipelineLayout_ = ManagedPipelineLayout::fromRaw(getDevice(), rawPipelineLayout);

    // Cast to C types for PipelineBuilder compatibility
    auto vkVertexInputInfo = static_cast<VkPipelineVertexInputStateCreateInfo>(vertexInputInfo);
    auto vkInputAssembly = static_cast<VkPipelineInputAssemblyStateCreateInfo>(inputAssembly);
    auto vkViewportState = static_cast<VkPipelineViewportStateCreateInfo>(viewportState);
    auto vkRasterizer = static_cast<VkPipelineRasterizationStateCreateInfo>(rasterizer);
    auto vkMultisampling = static_cast<VkPipelineMultisampleStateCreateInfo>(multisampling);
    auto vkDepthStencil = static_cast<VkPipelineDepthStencilStateCreateInfo>(depthStencil);
    auto vkColorBlending = static_cast<VkPipelineColorBlendStateCreateInfo>(colorBlending);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pVertexInputState = &vkVertexInputInfo;
    pipelineInfo.pInputAssemblyState = &vkInputAssembly;
    pipelineInfo.pViewportState = &vkViewportState;
    pipelineInfo.pRasterizationState = &vkRasterizer;
    pipelineInfo.pMultisampleState = &vkMultisampling;
    pipelineInfo.pDepthStencilState = &vkDepthStencil;
    pipelineInfo.pColorBlendState = &vkColorBlending;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    if (!builder.buildGraphicsPipeline(pipelineInfo, shadowPipelineLayout_.get(), rawPipeline)) {
        return false;
    }
    shadowPipeline_ = ManagedPipeline::fromRaw(getDevice(), rawPipeline);

    return true;
}

bool GrassSystem::createDescriptorSets() {
    // Note: Standard compute/graphics descriptor sets are allocated by ParticleSystem::init()
    // after all hooks complete. This hook only allocates GrassSystem-specific descriptor sets.
    // Compute descriptor set updates happen later in writeComputeDescriptorSets() called after init.

    SDL_Log("GrassSystem::createDescriptorSets - pool=%p, shadowLayout=%p", (void*)getDescriptorPool(), (void*)shadowDescriptorSetLayout_.get());
    SDL_Log("GrassSystem::createDescriptorSets - about to allocate shadow sets");

    // Allocate shadow descriptor sets for all buffer sets (matches frames in flight)
    uint32_t bufferSetCount = getFramesInFlight();
    shadowDescriptorSetsDB.resize(bufferSetCount);

    for (uint32_t set = 0; set < bufferSetCount; set++) {
        SDL_Log("GrassSystem::createDescriptorSets - allocating shadow set %u", set);
        shadowDescriptorSetsDB[set] = getDescriptorPool()->allocateSingle(shadowDescriptorSetLayout_.get());
        SDL_Log("GrassSystem::createDescriptorSets - allocated shadow set %u", set);
        if (shadowDescriptorSetsDB[set] == VK_NULL_HANDLE) {
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
    uint32_t bufferSetCount = (*particleSystem)->getBufferSetCount();
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        // Use non-fluent pattern to avoid copy semantics bug with DescriptorManager::SetWriter
        DescriptorManager::SetWriter writer(getDevice(), (*particleSystem)->getComputeDescriptorSet(set));
        writer.writeBuffer(0, instanceBuffers.buffers[set], 0, sizeof(GrassInstance) * MAX_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(1, indirectBuffers.buffers[set], 0, sizeof(VkDrawIndirectCommand), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        writer.writeBuffer(2, uniformBuffers.buffers[0], 0, sizeof(GrassUniforms));
        writer.update();
    }
}

bool GrassSystem::createExtraPipelines() {
    if (!createDisplacementPipeline()) return false;
    if (!createShadowPipeline()) return false;
    return true;
}

void GrassSystem::updateDescriptorSets(VkDevice dev, const std::vector<VkBuffer>& rendererUniformBuffers,
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
    // Store terrain heightmap info for compute descriptor set updates
    this->terrainHeightMapView = terrainHeightMapViewParam;
    this->terrainHeightMapSampler = terrainHeightMapSamplerParam;

    // Store tile cache resources (triple-buffered tile info)
    this->tileArrayView = tileArrayViewParam;
    this->tileSampler = tileSamplerParam;
    this->tileInfoBuffers = tileInfoBuffersParam;

    // Store renderer uniform buffers (kept for backward compatibility)
    this->rendererUniformBuffers_ = rendererUniformBuffers;

    // Store dynamic renderer UBO reference for per-frame binding with dynamic offsets
    this->dynamicRendererUBO_ = dynamicRendererUBO;

    // Update compute descriptor sets with terrain heightmap, displacement, and tile cache
    // Note: Bindings 0, 1, 2 are already written in writeComputeDescriptorSets() - only write new bindings here
    // Note: tile info buffer (binding 6) is updated per-frame in recordResetAndCompute
    uint32_t bufferSetCount = (*particleSystem)->getBufferSetCount();
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        // Use non-fluent pattern to avoid copy semantics bug with DescriptorManager::SetWriter
        DescriptorManager::SetWriter computeWriter(dev, (*particleSystem)->getComputeDescriptorSet(set));
        computeWriter.writeImage(3, terrainHeightMapView, terrainHeightMapSampler);
        computeWriter.writeImage(4, displacementImageView, displacementSampler_.get());

        // Tile cache bindings (5 and 6) - for high-res terrain sampling
        if (tileArrayView != VK_NULL_HANDLE && tileSampler != VK_NULL_HANDLE) {
            computeWriter.writeImage(5, tileArrayView, tileSampler);
        }
        // Write initial tile info buffer (frame 0) - will be updated per-frame
        if (tileInfoBuffers[0] != VK_NULL_HANDLE) {
            computeWriter.writeBuffer(6, tileInfoBuffers[0], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        computeWriter.update();
    }

    // Update graphics and shadow descriptor sets for all buffer sets
    for (uint32_t set = 0; set < bufferSetCount; set++) {
        // Graphics descriptor set - use non-fluent pattern
        DescriptorManager::SetWriter graphicsWriter(dev, (*particleSystem)->getGraphicsDescriptorSet(set));
        // Use dynamic UBO if available (avoids per-frame descriptor updates)
        if (dynamicRendererUBO && dynamicRendererUBO->isValid()) {
            graphicsWriter.writeBuffer(0, dynamicRendererUBO->buffer, 0, dynamicRendererUBO->alignedSize,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
        } else {
            graphicsWriter.writeBuffer(0, rendererUniformBuffers[0], 0, 160,
                                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);  // sizeof(UniformBufferObject)
        }
        graphicsWriter.writeBuffer(1, instanceBuffers.buffers[set], 0, sizeof(GrassInstance) * MAX_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        graphicsWriter.writeImage(2, shadowMapView, shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        graphicsWriter.writeBuffer(3, windBuffers[0], 0, 32);  // sizeof(WindUniforms)
        graphicsWriter.writeBuffer(4, lightBuffersParam[0], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        graphicsWriter.writeImage(6, cloudShadowMapView, cloudShadowMapSampler);
        graphicsWriter.writeBuffer(10, snowBuffersParam[0], 0, sizeof(SnowUBO));
        graphicsWriter.writeBuffer(11, cloudShadowBuffersParam[0], 0, sizeof(CloudShadowUBO));
        graphicsWriter.update();

        // Shadow descriptor set - use non-fluent pattern
        DescriptorManager::SetWriter shadowWriter(dev, shadowDescriptorSetsDB[set]);
        shadowWriter.writeBuffer(0, rendererUniformBuffers[0], 0, 160);  // sizeof(UniformBufferObject)
        shadowWriter.writeBuffer(1, instanceBuffers.buffers[set], 0, sizeof(GrassInstance) * MAX_INSTANCES, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        shadowWriter.writeBuffer(2, windBuffers[0], 0, 32);  // sizeof(WindUniforms)
        shadowWriter.update();
    }
}

void GrassSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                                  float terrainSize, float terrainHeightScale) {
    GrassUniforms uniforms{};

    // Camera position
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);

    // Extract frustum planes from view-projection matrix
    extractFrustumPlanes(viewProj, uniforms.frustumPlanes);

    // Update displacement region to follow camera
    displacementRegionCenter = glm::vec2(cameraPos.x, cameraPos.z);

    // Displacement region info for grass compute shader
    // xy = world center, z = region size (50m), w = texel size
    float texelSize = DISPLACEMENT_REGION_SIZE / static_cast<float>(DISPLACEMENT_TEXTURE_SIZE);
    uniforms.displacementRegion = glm::vec4(displacementRegionCenter.x, displacementRegionCenter.y,
                                            DISPLACEMENT_REGION_SIZE, texelSize);

    // Distance thresholds
    uniforms.maxDrawDistance = 50.0f;
    uniforms.lodTransitionStart = 30.0f;
    uniforms.lodTransitionEnd = 50.0f;

    // Terrain parameters for heightmap sampling
    uniforms.terrainSize = terrainSize;
    uniforms.terrainHeightScale = terrainHeightScale;

    // Copy to mapped buffer
    memcpy(uniformBuffers.mappedPointers[frameIndex], &uniforms, sizeof(GrassUniforms));
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

void GrassSystem::recordDisplacementUpdate(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Copy displacement sources to per-frame buffer (double-buffered)
    memcpy(displacementSourceBuffers.mappedPointers[frameIndex], currentDisplacementSources.data(),
           sizeof(DisplacementSource) * currentDisplacementSources.size());

    // Update displacement uniforms
    float texelSize = DISPLACEMENT_REGION_SIZE / static_cast<float>(DISPLACEMENT_TEXTURE_SIZE);
    DisplacementUniforms dispUniforms;
    dispUniforms.regionCenter = glm::vec4(displacementRegionCenter.x, displacementRegionCenter.y,
                                          DISPLACEMENT_REGION_SIZE, texelSize);
    const EnvironmentSettings fallbackSettings{};
    const EnvironmentSettings& settings = environmentSettings ? *environmentSettings : fallbackSettings;
    dispUniforms.params = glm::vec4(settings.grassDisplacementDecay, settings.grassMaxDisplacement, 1.0f / 60.0f,
                                    static_cast<float>(currentDisplacementSources.size()));
    memcpy(displacementUniformBuffers.mappedPointers[frameIndex], &dispUniforms, sizeof(DisplacementUniforms));

    // Transition displacement image to general layout if needed (first frame)
    // For subsequent frames, it should already be in GENERAL layout
    Barriers::transitionImage(cmd, displacementImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    // Dispatch displacement update compute shader using per-frame descriptor set (double-buffered)
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, displacementPipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            displacementPipelineLayout_.get(), 0, 1,
                            &displacementDescriptorSets[frameIndex], 0, nullptr);

    // Dispatch: 512x512 / 16x16 = 32x32 workgroups
    vkCmdDispatch(cmd, 32, 32, 1);

    // Barrier: displacement compute write -> grass compute read
    Barriers::imageComputeToSampling(cmd, displacementImage,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void GrassSystem::recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    // Double-buffer: compute writes to computeBufferSet
    uint32_t writeSet = (*particleSystem)->getComputeBufferSet();

    // Update compute descriptor set with per-frame buffers only
    // Note: Static images (bindings 3, 4, 5) are already bound in updateDescriptorSets()
    DescriptorManager::SetWriter writer(getDevice(), (*particleSystem)->getComputeDescriptorSet(writeSet));
    writer.writeBuffer(2, uniformBuffers.buffers[frameIndex], 0, sizeof(GrassUniforms));

    // Update tile info buffer to the correct frame's buffer (triple-buffered to avoid CPU-GPU sync)
    if (tileInfoBuffers[frameIndex % 3] != VK_NULL_HANDLE) {
        writer.writeBuffer(6, tileInfoBuffers[frameIndex % 3], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }
    writer.update();

    // Ensure CPU writes to tile info buffer are visible to GPU before compute dispatch
    // The tile info buffer is written by CPU in TerrainTileCache::updateTileInfoBuffer()
    // and needs to be visible to the grass compute shader that samples terrain heights
    Barriers::hostToCompute(cmd);

    // Reset indirect buffer before compute dispatch to prevent accumulation
    Barriers::clearBufferForComputeReadWrite(cmd, indirectBuffers.buffers[writeSet], 0, sizeof(VkDrawIndirectCommand));

    // Dispatch grass compute shader using the compute buffer set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, getComputePipelineHandles().pipeline);
    VkDescriptorSet computeSet = (*particleSystem)->getComputeDescriptorSet(writeSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            getComputePipelineHandles().pipelineLayout, 0, 1,
                            &computeSet, 0, nullptr);

    GrassPushConstants grassPush{};
    grassPush.time = time;
    vkCmdPushConstants(cmd, getComputePipelineHandles().pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GrassPushConstants), &grassPush);

    // Dispatch: 2D grid of workgroups for better texture cache coherency
    // Grid: 1000x1000 blades, Workgroup: 16x16 threads
    // ceil(1000/16) = 63 workgroups per dimension (63*63*256 = 1,016,064 threads)
    vkCmdDispatch(cmd, 63, 63, 1);

    // Memory barrier: compute write -> vertex shader read (storage buffer) and indirect read
    // Note: This barrier ensures the compute results are visible when we draw from this buffer
    // in the NEXT frame (after advanceBufferSet swaps the sets)
    Barriers::computeToVertexAndIndirectDraw(cmd);
}

void GrassSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    // Double-buffer: graphics reads from renderBufferSet (previous frame's compute output)
    uint32_t readSet = (*particleSystem)->getRenderBufferSet();

    // Dynamic UBO: no per-frame descriptor update needed - we pass the offset at bind time instead
    // This eliminates per-frame vkUpdateDescriptorSets calls for the renderer UBO

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getGraphicsPipelineHandles().pipeline);

    // Set dynamic viewport and scissor to handle window resize
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(getExtent().width);
    viewport.height = static_cast<float>(getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = getExtent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    VkDescriptorSet graphicsSet = (*particleSystem)->getGraphicsDescriptorSet(readSet);

    // Use dynamic offset for binding 0 (renderer UBO) if dynamic buffer is available
    if (dynamicRendererUBO_ && dynamicRendererUBO_->isValid()) {
        uint32_t dynamicOffset = dynamicRendererUBO_->getDynamicOffset(frameIndex);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                getGraphicsPipelineHandles().pipelineLayout, 0, 1,
                                &graphicsSet, 1, &dynamicOffset);
    } else {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                getGraphicsPipelineHandles().pipelineLayout, 0, 1,
                                &graphicsSet, 0, nullptr);
    }

    GrassPushConstants grassPush{};
    grassPush.time = time;
    vkCmdPushConstants(cmd, getGraphicsPipelineHandles().pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants), &grassPush);

    vkCmdDrawIndirect(cmd, indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void GrassSystem::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex) {
    // Double-buffer: shadow pass reads from renderBufferSet (same as main draw)
    uint32_t readSet = (*particleSystem)->getRenderBufferSet();

    // Update shadow descriptor set to use this frame's renderer UBO
    if (!rendererUniformBuffers_.empty()) {
        DescriptorManager::SetWriter(getDevice(), shadowDescriptorSetsDB[readSet])
            .writeBuffer(0, rendererUniformBuffers_[frameIndex], 0, 160)
            .update();
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_.get());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            shadowPipelineLayout_.get(), 0, 1,
                            &shadowDescriptorSetsDB[readSet], 0, nullptr);

    GrassPushConstants grassPush{};
    grassPush.time = time;
    grassPush.cascadeIndex = static_cast<int>(cascadeIndex);
    vkCmdPushConstants(cmd, shadowPipelineLayout_.get(),
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants), &grassPush);

    vkCmdDrawIndirect(cmd, indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void GrassSystem::setSnowMask(VkDevice device, VkImageView snowMaskView, VkSampler snowMaskSampler) {
    // Update graphics descriptor sets with snow mask texture
    uint32_t bufferSetCount = (*particleSystem)->getBufferSetCount();
    for (uint32_t setIndex = 0; setIndex < bufferSetCount; setIndex++) {
        DescriptorManager::SetWriter(device, (*particleSystem)->getGraphicsDescriptorSet(setIndex))
            .writeImage(5, snowMaskView, snowMaskSampler)
            .update();
    }
}

void GrassSystem::advanceBufferSet() {
    (*particleSystem)->advanceBufferSet();
}
