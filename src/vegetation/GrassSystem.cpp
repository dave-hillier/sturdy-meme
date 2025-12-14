#include "GrassSystem.h"
#include "ShaderLoader.h"
#include "PipelineBuilder.h"
#include "BindingBuilder.h"
#include "UBOs.h"
#include "VulkanBarriers.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <array>

// Forward declare UniformBufferObject size (needed for descriptor set update)
struct UniformBufferObject;

bool GrassSystem::init(const InitInfo& info) {
    shadowRenderPass = info.shadowRenderPass;
    shadowMapSize = info.shadowMapSize;

    SystemLifecycleHelper::Hooks hooks{};
    hooks.createBuffers = [this]() { return createBuffers(); };
    hooks.createComputeDescriptorSetLayout = [this]() { return createComputeDescriptorSetLayout(); };
    hooks.createComputePipeline = [this]() { return createComputePipeline(); };
    hooks.createGraphicsDescriptorSetLayout = [this]() { return createGraphicsDescriptorSetLayout(); };
    hooks.createGraphicsPipeline = [this]() { return createGraphicsPipeline(); };
    hooks.createExtraPipelines = [this]() { return createExtraPipelines(); };
    hooks.createDescriptorSets = [this]() { return createDescriptorSets(); };
    hooks.destroyBuffers = [this](VmaAllocator allocator) { destroyBuffers(allocator); };

    return particleSystem.init(info, hooks, BUFFER_SET_COUNT);
}

void GrassSystem::destroy(VkDevice dev, VmaAllocator alloc) {
    vkDestroyPipeline(dev, shadowPipeline, nullptr);
    vkDestroyPipelineLayout(dev, shadowPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, shadowDescriptorSetLayout, nullptr);

    vkDestroyPipeline(dev, displacementPipeline, nullptr);
    vkDestroyPipelineLayout(dev, displacementPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, displacementDescriptorSetLayout, nullptr);
    vkDestroySampler(dev, displacementSampler, nullptr);
    vkDestroyImageView(dev, displacementImageView, nullptr);
    vmaDestroyImage(alloc, displacementImage, displacementAllocation);

    particleSystem.destroy(dev, alloc);
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

    BufferUtils::DoubleBufferedBufferBuilder instanceBuilder;
    if (!instanceBuilder.setAllocator(getAllocator())
             .setSetCount(BUFFER_SET_COUNT)
             .setSize(instanceBufferSize)
             .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .build(instanceBuffers)) {
        SDL_Log("Failed to create grass instance buffers");
        return false;
    }

    BufferUtils::DoubleBufferedBufferBuilder indirectBuilder;
    if (!indirectBuilder.setAllocator(getAllocator())
             .setSetCount(BUFFER_SET_COUNT)
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
    // Create displacement texture (RG16F, 512x512)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = DISPLACEMENT_TEXTURE_SIZE;
    imageInfo.extent.height = DISPLACEMENT_TEXTURE_SIZE;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R16G16_SFLOAT;  // RG16F for XZ displacement
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(getAllocator(), &imageInfo, &allocInfo,
                       &displacementImage, &displacementAllocation, nullptr) != VK_SUCCESS) {
        SDL_Log("Failed to create displacement image");
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = displacementImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(getDevice(), &viewInfo, nullptr, &displacementImageView) != VK_SUCCESS) {
        SDL_Log("Failed to create displacement image view");
        return false;
    }

    // Create sampler for grass compute shader to sample displacement
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(getDevice(), &samplerInfo, nullptr, &displacementSampler) != VK_SUCCESS) {
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
    // Displacement map (storage image, read-write)
    auto displacementMap = BindingBuilder()
        .setBinding(0)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    // Source buffer (SSBO)
    auto sourceBuffer = BindingBuilder()
        .setBinding(1)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    // Displacement uniforms
    auto displacementUniforms = BindingBuilder()
        .setBinding(2)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    std::array<VkDescriptorSetLayoutBinding, 3> bindings = {displacementMap, sourceBuffer, displacementUniforms};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(getDevice(), &layoutInfo, nullptr,
                                    &displacementDescriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create displacement descriptor set layout");
        return false;
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &displacementDescriptorSetLayout;

    if (vkCreatePipelineLayout(getDevice(), &pipelineLayoutInfo, nullptr,
                               &displacementPipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create displacement pipeline layout");
        return false;
    }

    // Load compute shader
    auto compShaderCode = ShaderLoader::readFile(getShaderPath() + "/grass_displacement.comp.spv");
    if (compShaderCode.empty()) {
        SDL_Log("Failed to load displacement compute shader");
        return false;
    }

    VkShaderModule compShaderModule = ShaderLoader::createShaderModule(getDevice(), compShaderCode);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = compShaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = displacementPipelineLayout;

    VkResult result = vkCreateComputePipelines(getDevice(), VK_NULL_HANDLE, 1,
                                               &pipelineInfo, nullptr,
                                               &displacementPipeline);

    vkDestroyShaderModule(getDevice(), compShaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_Log("Failed to create displacement compute pipeline");
        return false;
    }

    // Allocate per-frame displacement descriptor sets (double-buffered) using managed pool
    displacementDescriptorSets = getDescriptorPool()->allocate(displacementDescriptorSetLayout, getFramesInFlight());
    if (displacementDescriptorSets.empty()) {
        SDL_Log("Failed to allocate displacement descriptor sets");
        return false;
    }

    // Update each per-frame descriptor set with image and per-frame buffers
    for (uint32_t i = 0; i < getFramesInFlight(); ++i) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = displacementImageView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorBufferInfo sourceBufferInfo{};
        sourceBufferInfo.buffer = displacementSourceBuffers.buffers[i];
        sourceBufferInfo.offset = 0;
        sourceBufferInfo.range = sizeof(DisplacementSource) * MAX_DISPLACEMENT_SOURCES;

        VkDescriptorBufferInfo uniformBufferInfo{};
        uniformBufferInfo.buffer = displacementUniformBuffers.buffers[i];
        uniformBufferInfo.offset = 0;
        uniformBufferInfo.range = sizeof(DisplacementUniforms);

        std::array<VkWriteDescriptorSet, 3> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = displacementDescriptorSets[i];
        writes[0].dstBinding = 0;
        writes[0].dstArrayElement = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &imageInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = displacementDescriptorSets[i];
        writes[1].dstBinding = 1;
        writes[1].dstArrayElement = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &sourceBufferInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = displacementDescriptorSets[i];
        writes[2].dstBinding = 2;
        writes[2].dstArrayElement = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &uniformBufferInfo;

        vkUpdateDescriptorSets(getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return true;
}

bool GrassSystem::createComputeDescriptorSetLayout() {
    PipelineBuilder builder(getDevice());
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT)
        .addDescriptorBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT);

    return builder.buildDescriptorSetLayout(getComputePipelineHandles().descriptorSetLayout);
}

bool GrassSystem::createComputePipeline() {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/grass.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GrassPushConstants));

    if (!builder.buildPipelineLayout({getComputePipelineHandles().descriptorSetLayout}, getComputePipelineHandles().pipelineLayout)) {
        return false;
    }

    return builder.buildComputePipeline(getComputePipelineHandles().pipelineLayout, getComputePipelineHandles().pipeline);
}

bool GrassSystem::createGraphicsDescriptorSetLayout() {
    PipelineBuilder builder(getDevice());
    // Grass system descriptor set layout:
    // binding 0: UBO (main rendering uniforms)
    // binding 1: instance buffer (SSBO) - vertex shader only
    // binding 2: shadow map (sampler)
    // binding 3: wind UBO - vertex shader only
    // binding 4: light buffer (SSBO)
    // binding 5: snow mask texture (sampler)
    // binding 6: cloud shadow map (sampler)
    // binding 10: snow UBO
    // binding 11: cloud shadow UBO
    builder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                 VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(10, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addDescriptorBinding(11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

    return builder.buildDescriptorSetLayout(getGraphicsPipelineHandles().descriptorSetLayout);
}

bool GrassSystem::createGraphicsPipeline() {
    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/grass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
        .addShaderStage(getShaderPath() + "/grass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants));

    // No vertex input - procedural geometry from instance buffer
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(getExtent().width);
    viewport.height = static_cast<float>(getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = getExtent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for grass
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    if (!builder.buildPipelineLayout({getGraphicsPipelineHandles().descriptorSetLayout}, getGraphicsPipelineHandles().pipelineLayout)) {
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.renderPass = getRenderPass();
    pipelineInfo.subpass = 0;

    return builder.buildGraphicsPipeline(pipelineInfo, getGraphicsPipelineHandles().pipelineLayout, getGraphicsPipelineHandles().pipeline);
}

bool GrassSystem::createShadowPipeline() {
    PipelineBuilder layoutBuilder(getDevice());
    layoutBuilder.addDescriptorBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT)
        .addDescriptorBinding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT);

    if (!layoutBuilder.buildDescriptorSetLayout(shadowDescriptorSetLayout)) {
        return false;
    }

    PipelineBuilder builder(getDevice());
    builder.addShaderStage(getShaderPath() + "/grass_shadow.vert.spv", VK_SHADER_STAGE_VERTEX_BIT)
        .addShaderStage(getShaderPath() + "/grass_shadow.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants));

    // No vertex input - procedural geometry from instance buffer
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(shadowMapSize);
    viewport.height = static_cast<float>(shadowMapSize);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {shadowMapSize, shadowMapSize};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for grass
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 0.25f;
    rasterizer.depthBiasSlopeFactor = 0.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // No color attachment for shadow pass
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 0;

    if (!builder.buildPipelineLayout({shadowDescriptorSetLayout}, shadowPipelineLayout)) {
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    return builder.buildGraphicsPipeline(pipelineInfo, shadowPipelineLayout, shadowPipeline);
}

bool GrassSystem::createDescriptorSets() {
    // Allocate standard compute and graphics descriptor sets for both buffer sets
    if (!particleSystem.createStandardDescriptorSets()) {
        return false;
    }

    // Allocate shadow descriptor sets for both buffer sets using managed pool
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        shadowDescriptorSetsDB[set] = getDescriptorPool()->allocateSingle(shadowDescriptorSetLayout);
        if (shadowDescriptorSetsDB[set] == VK_NULL_HANDLE) {
            SDL_Log("Failed to allocate grass shadow descriptor set (set %u)", set);
            return false;
        }

        // Update compute descriptor sets (instance and indirect buffers only)
        // Uniform buffer will be updated in updateDescriptorSets with the right frame's buffer
        VkDescriptorBufferInfo instanceBufferInfo{};
        instanceBufferInfo.buffer = instanceBuffers.buffers[set];
        instanceBufferInfo.offset = 0;
        instanceBufferInfo.range = sizeof(GrassInstance) * MAX_INSTANCES;

        VkDescriptorBufferInfo indirectBufferInfo{};
        indirectBufferInfo.buffer = indirectBuffers.buffers[set];
        indirectBufferInfo.offset = 0;
        indirectBufferInfo.range = sizeof(VkDrawIndirectCommand);

        // Use first frame's uniform buffer initially; will be updated per-frame
        VkDescriptorBufferInfo uniformBufferInfo{};
        uniformBufferInfo.buffer = uniformBuffers.buffers[0];
        uniformBufferInfo.offset = 0;
        uniformBufferInfo.range = sizeof(GrassUniforms);

        std::array<VkWriteDescriptorSet, 3> computeWrites{};

        computeWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[0].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[0].dstBinding = 0;
        computeWrites[0].dstArrayElement = 0;
        computeWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[0].descriptorCount = 1;
        computeWrites[0].pBufferInfo = &instanceBufferInfo;

        computeWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[1].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[1].dstBinding = 1;
        computeWrites[1].dstArrayElement = 0;
        computeWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        computeWrites[1].descriptorCount = 1;
        computeWrites[1].pBufferInfo = &indirectBufferInfo;

        computeWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        computeWrites[2].dstSet = particleSystem.getComputeDescriptorSet(set);
        computeWrites[2].dstBinding = 2;
        computeWrites[2].dstArrayElement = 0;
        computeWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        computeWrites[2].descriptorCount = 1;
        computeWrites[2].pBufferInfo = &uniformBufferInfo;

        vkUpdateDescriptorSets(getDevice(), static_cast<uint32_t>(computeWrites.size()),
                               computeWrites.data(), 0, nullptr);
    }

    return true;
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
                                        VkImageView terrainHeightMapView, VkSampler terrainHeightMapSampler,
                                        const std::vector<VkBuffer>& snowBuffersParam,
                                        const std::vector<VkBuffer>& cloudShadowBuffersParam,
                                        VkImageView cloudShadowMapView, VkSampler cloudShadowMapSampler) {
    // Store terrain heightmap info for compute descriptor set updates
    this->terrainHeightMapView = terrainHeightMapView;
    this->terrainHeightMapSampler = terrainHeightMapSampler;

    // Update graphics and shadow descriptor sets for both buffer sets (A and B)
    // Note: We use the first frame's UBO/wind buffer since they're updated each frame anyway
    // For proper per-frame handling, we'd need dynamic offsets or per-frame descriptor sets
    for (uint32_t set = 0; set < BUFFER_SET_COUNT; set++) {
        // Use first frame's uniform buffers (they're updated before each draw anyway)
        VkDescriptorBufferInfo uboInfo{};
        uboInfo.buffer = rendererUniformBuffers[0];
        uboInfo.offset = 0;
        uboInfo.range = 160;  // sizeof(UniformBufferObject) - matches Renderer's UBO

        VkDescriptorBufferInfo instanceBufferInfo{};
        instanceBufferInfo.buffer = instanceBuffers.buffers[set];
        instanceBufferInfo.offset = 0;
        instanceBufferInfo.range = sizeof(GrassInstance) * MAX_INSTANCES;

        VkDescriptorImageInfo shadowImageInfo{};
        shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowImageInfo.imageView = shadowMapView;
        shadowImageInfo.sampler = shadowSampler;

        VkDescriptorBufferInfo windBufferInfo{};
        windBufferInfo.buffer = windBuffers[0];
        windBufferInfo.offset = 0;
        windBufferInfo.range = 32;  // sizeof(WindUniforms) - 2 vec4s

        VkDescriptorBufferInfo lightBufferInfo{};
        lightBufferInfo.buffer = lightBuffersParam[0];
        lightBufferInfo.offset = 0;
        lightBufferInfo.range = VK_WHOLE_SIZE;  // sizeof(LightBuffer)

        // Cloud shadow map (binding 6)
        VkDescriptorImageInfo cloudShadowMapInfo{};
        cloudShadowMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        cloudShadowMapInfo.imageView = cloudShadowMapView;
        cloudShadowMapInfo.sampler = cloudShadowMapSampler;

        // Snow UBO (binding 10)
        VkDescriptorBufferInfo snowBufferInfo{};
        snowBufferInfo.buffer = snowBuffersParam[0];
        snowBufferInfo.offset = 0;
        snowBufferInfo.range = sizeof(SnowUBO);

        // Cloud shadow UBO (binding 11)
        VkDescriptorBufferInfo cloudShadowBufferInfo{};
        cloudShadowBufferInfo.buffer = cloudShadowBuffersParam[0];
        cloudShadowBufferInfo.offset = 0;
        cloudShadowBufferInfo.range = sizeof(CloudShadowUBO);

        std::array<VkWriteDescriptorSet, 8> graphicsWrites{};

        graphicsWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[0].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[0].dstBinding = 0;
        graphicsWrites[0].dstArrayElement = 0;
        graphicsWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphicsWrites[0].descriptorCount = 1;
        graphicsWrites[0].pBufferInfo = &uboInfo;

        graphicsWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[1].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[1].dstBinding = 1;
        graphicsWrites[1].dstArrayElement = 0;
        graphicsWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        graphicsWrites[1].descriptorCount = 1;
        graphicsWrites[1].pBufferInfo = &instanceBufferInfo;

        graphicsWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[2].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[2].dstBinding = 2;
        graphicsWrites[2].dstArrayElement = 0;
        graphicsWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        graphicsWrites[2].descriptorCount = 1;
        graphicsWrites[2].pImageInfo = &shadowImageInfo;

        graphicsWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[3].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[3].dstBinding = 3;
        graphicsWrites[3].dstArrayElement = 0;
        graphicsWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphicsWrites[3].descriptorCount = 1;
        graphicsWrites[3].pBufferInfo = &windBufferInfo;

        graphicsWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[4].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[4].dstBinding = 4;
        graphicsWrites[4].dstArrayElement = 0;
        graphicsWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        graphicsWrites[4].descriptorCount = 1;
        graphicsWrites[4].pBufferInfo = &lightBufferInfo;

        // binding 6: cloud shadow map
        graphicsWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[5].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[5].dstBinding = 6;
        graphicsWrites[5].dstArrayElement = 0;
        graphicsWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        graphicsWrites[5].descriptorCount = 1;
        graphicsWrites[5].pImageInfo = &cloudShadowMapInfo;

        // binding 10: snow UBO
        graphicsWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[6].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[6].dstBinding = 10;
        graphicsWrites[6].dstArrayElement = 0;
        graphicsWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphicsWrites[6].descriptorCount = 1;
        graphicsWrites[6].pBufferInfo = &snowBufferInfo;

        // binding 11: cloud shadow UBO
        graphicsWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        graphicsWrites[7].dstSet = particleSystem.getGraphicsDescriptorSet(set);
        graphicsWrites[7].dstBinding = 11;
        graphicsWrites[7].dstArrayElement = 0;
        graphicsWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        graphicsWrites[7].descriptorCount = 1;
        graphicsWrites[7].pBufferInfo = &cloudShadowBufferInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(graphicsWrites.size()),
                               graphicsWrites.data(), 0, nullptr);

        // Update shadow descriptor sets (UBO + instance buffer + wind buffer)
        std::array<VkWriteDescriptorSet, 3> shadowWrites{};

        shadowWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrites[0].dstSet = shadowDescriptorSetsDB[set];
        shadowWrites[0].dstBinding = 0;
        shadowWrites[0].dstArrayElement = 0;
        shadowWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        shadowWrites[0].descriptorCount = 1;
        shadowWrites[0].pBufferInfo = &uboInfo;

        shadowWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrites[1].dstSet = shadowDescriptorSetsDB[set];
        shadowWrites[1].dstBinding = 1;
        shadowWrites[1].dstArrayElement = 0;
        shadowWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        shadowWrites[1].descriptorCount = 1;
        shadowWrites[1].pBufferInfo = &instanceBufferInfo;

        shadowWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        shadowWrites[2].dstSet = shadowDescriptorSetsDB[set];
        shadowWrites[2].dstBinding = 2;
        shadowWrites[2].dstArrayElement = 0;
        shadowWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        shadowWrites[2].descriptorCount = 1;
        shadowWrites[2].pBufferInfo = &windBufferInfo;

        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(shadowWrites.size()),
                               shadowWrites.data(), 0, nullptr);
    }
}

void GrassSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                                  float terrainSize, float terrainHeightScale) {
    GrassUniforms uniforms{};

    // Camera position
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);

    // Extract frustum planes from view-projection matrix
    // Each plane equation: ax + by + cz + d = 0
    // Using row-major extraction (GLM is column-major, so we transpose conceptually)
    glm::mat4 m = glm::transpose(viewProj);

    // Left:   row3 + row0
    uniforms.frustumPlanes[0] = m[3] + m[0];
    // Right:  row3 - row0
    uniforms.frustumPlanes[1] = m[3] - m[0];
    // Bottom: row3 + row1
    uniforms.frustumPlanes[2] = m[3] + m[1];
    // Top:    row3 - row1
    uniforms.frustumPlanes[3] = m[3] - m[1];
    // Near:   row3 + row2
    uniforms.frustumPlanes[4] = m[3] + m[2];
    // Far:    row3 - row2
    uniforms.frustumPlanes[5] = m[3] - m[2];

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float len = glm::length(glm::vec3(uniforms.frustumPlanes[i]));
        if (len > 0.0001f) {
            uniforms.frustumPlanes[i] /= len;
        }
    }

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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, displacementPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            displacementPipelineLayout, 0, 1,
                            &displacementDescriptorSets[frameIndex], 0, nullptr);

    // Dispatch: 512x512 / 16x16 = 32x32 workgroups
    vkCmdDispatch(cmd, 32, 32, 1);

    // Barrier: displacement compute write -> grass compute read
    Barriers::imageComputeToSampling(cmd, displacementImage,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void GrassSystem::recordResetAndCompute(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    // Double-buffer: compute writes to computeBufferSet
    uint32_t writeSet = particleSystem.getComputeBufferSet();

    // Update compute descriptor set to use this frame's uniform buffer, terrain heightmap, and displacement map
    // (uniforms contain per-frame camera/frustum data)
    VkDescriptorBufferInfo uniformBufferInfo{};
    uniformBufferInfo.buffer = uniformBuffers.buffers[frameIndex];
    uniformBufferInfo.offset = 0;
    uniformBufferInfo.range = sizeof(GrassUniforms);

    VkDescriptorImageInfo heightMapInfo{};
    heightMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    heightMapInfo.imageView = terrainHeightMapView;
    heightMapInfo.sampler = terrainHeightMapSampler;

    VkDescriptorImageInfo displacementMapInfo{};
    displacementMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    displacementMapInfo.imageView = displacementImageView;
    displacementMapInfo.sampler = displacementSampler;

    std::array<VkWriteDescriptorSet, 3> writes{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = particleSystem.getComputeDescriptorSet(writeSet);
    writes[0].dstBinding = 2;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &uniformBufferInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = particleSystem.getComputeDescriptorSet(writeSet);
    writes[1].dstBinding = 3;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &heightMapInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = particleSystem.getComputeDescriptorSet(writeSet);
    writes[2].dstBinding = 4;
    writes[2].dstArrayElement = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &displacementMapInfo;

    vkUpdateDescriptorSets(getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Reset indirect buffer before compute dispatch to prevent accumulation
    Barriers::clearBufferForComputeReadWrite(cmd, indirectBuffers.buffers[writeSet], 0, sizeof(VkDrawIndirectCommand));

    // Dispatch grass compute shader using the compute buffer set
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, getComputePipelineHandles().pipeline);
    VkDescriptorSet computeSet = particleSystem.getComputeDescriptorSet(writeSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            getComputePipelineHandles().pipelineLayout, 0, 1,
                            &computeSet, 0, nullptr);

    GrassPushConstants grassPush{};
    grassPush.time = time;
    vkCmdPushConstants(cmd, getComputePipelineHandles().pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GrassPushConstants), &grassPush);

    // Dispatch: ceil(1,000,000 / 64) = 15,625 workgroups (1000x1000 grid)
    vkCmdDispatch(cmd, 15625, 1, 1);

    // Memory barrier: compute write -> vertex shader read (storage buffer) and indirect read
    // Note: This barrier ensures the compute results are visible when we draw from this buffer
    // in the NEXT frame (after advanceBufferSet swaps the sets)
    Barriers::computeToVertexAndIndirectDraw(cmd);
}

void GrassSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    // Double-buffer: graphics reads from renderBufferSet
    // On first frame before double-buffering kicks in, read from compute set (same as renderBufferSet)
    uint32_t readSet = particleSystem.getRenderBufferSet();

    // Bootstrap: if we haven't diverged yet, read from what we just computed
    if (particleSystem.getComputeBufferSet() == particleSystem.getRenderBufferSet()) {
        readSet = particleSystem.getComputeBufferSet();
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, getGraphicsPipelineHandles().pipeline);
    VkDescriptorSet graphicsSet = particleSystem.getGraphicsDescriptorSet(readSet);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            getGraphicsPipelineHandles().pipelineLayout, 0, 1,
                            &graphicsSet, 0, nullptr);

    GrassPushConstants grassPush{};
    grassPush.time = time;
    vkCmdPushConstants(cmd, getGraphicsPipelineHandles().pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants), &grassPush);

    vkCmdDrawIndirect(cmd, indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void GrassSystem::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex, float time, uint32_t cascadeIndex) {
    // Double-buffer: shadow pass reads from renderBufferSet (same as main draw)
    uint32_t readSet = particleSystem.getRenderBufferSet();

    // Bootstrap: if we haven't diverged yet, read from what we just computed
    if (particleSystem.getComputeBufferSet() == particleSystem.getRenderBufferSet()) {
        readSet = particleSystem.getComputeBufferSet();
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            shadowPipelineLayout, 0, 1,
                            &shadowDescriptorSetsDB[readSet], 0, nullptr);

    GrassPushConstants grassPush{};
    grassPush.time = time;
    grassPush.cascadeIndex = static_cast<int>(cascadeIndex);
    vkCmdPushConstants(cmd, shadowPipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GrassPushConstants), &grassPush);

    vkCmdDrawIndirect(cmd, indirectBuffers.buffers[readSet], 0, 1, sizeof(VkDrawIndirectCommand));
}

void GrassSystem::setSnowMask(VkDevice device, VkImageView snowMaskView, VkSampler snowMaskSampler) {
    // Update graphics descriptor sets with snow mask texture
    for (uint32_t setIndex = 0; setIndex < BUFFER_SET_COUNT; setIndex++) {
        VkDescriptorImageInfo snowMaskInfo{snowMaskSampler, snowMaskView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet snowMaskWrite{};
        snowMaskWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        snowMaskWrite.dstSet = particleSystem.getGraphicsDescriptorSet(setIndex);
        snowMaskWrite.dstBinding = 5;
        snowMaskWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        snowMaskWrite.descriptorCount = 1;
        snowMaskWrite.pImageInfo = &snowMaskInfo;

        vkUpdateDescriptorSets(device, 1, &snowMaskWrite, 0, nullptr);
    }
}

void GrassSystem::advanceBufferSet() {
    particleSystem.advanceBufferSet();
}
