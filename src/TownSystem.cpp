#include "TownSystem.h"
#include "DescriptorManager.h"
#include "GraphicsPipelineFactory.h"
#include "ShaderLoader.h"
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <cstring>
#include <iostream>

using ShaderLoader::loadShaderModule;

bool TownSystem::init(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    renderPass = info.renderPass;
    shadowRenderPass = info.shadowRenderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shadowMapSize = info.shadowMapSize;
    shaderPath = info.shaderPath;
    texturePath = info.texturePath;
    framesInFlight = info.framesInFlight;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;

    if (!createBuildingMeshes()) {
        std::cerr << "TownSystem: Failed to create building meshes" << std::endl;
        return false;
    }

    if (!createRoadMesh()) {
        std::cerr << "TownSystem: Failed to create road mesh" << std::endl;
        return false;
    }

    if (!createTextures()) {
        std::cerr << "TownSystem: Failed to create textures" << std::endl;
        return false;
    }

    if (!createDescriptorSetLayout()) {
        std::cerr << "TownSystem: Failed to create descriptor set layout" << std::endl;
        return false;
    }

    if (!createGraphicsPipeline()) {
        std::cerr << "TownSystem: Failed to create graphics pipeline" << std::endl;
        return false;
    }

    if (!createShadowPipeline()) {
        std::cerr << "TownSystem: Failed to create shadow pipeline" << std::endl;
        return false;
    }

    if (!createDescriptorSets()) {
        std::cerr << "TownSystem: Failed to create descriptor sets" << std::endl;
        return false;
    }

    return true;
}

void TownSystem::destroy(VkDevice dev, VmaAllocator alloc) {
    // Destroy instance buffer
    if (buildingInstanceBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(alloc, buildingInstanceBuffer, buildingInstanceAlloc);
        buildingInstanceBuffer = VK_NULL_HANDLE;
    }

    // Destroy textures
    if (buildingTextureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(dev, buildingTextureSampler, nullptr);
        buildingTextureSampler = VK_NULL_HANDLE;
    }
    if (buildingTextureView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, buildingTextureView, nullptr);
        buildingTextureView = VK_NULL_HANDLE;
    }
    if (buildingTexture != VK_NULL_HANDLE) {
        vmaDestroyImage(alloc, buildingTexture, buildingTextureAlloc);
        buildingTexture = VK_NULL_HANDLE;
    }
    if (roofTextureView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, roofTextureView, nullptr);
        roofTextureView = VK_NULL_HANDLE;
    }
    if (roofTexture != VK_NULL_HANDLE) {
        vmaDestroyImage(alloc, roofTexture, roofTextureAlloc);
        roofTexture = VK_NULL_HANDLE;
    }
    if (roadTextureView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, roadTextureView, nullptr);
        roadTextureView = VK_NULL_HANDLE;
    }
    if (roadTexture != VK_NULL_HANDLE) {
        vmaDestroyImage(alloc, roadTexture, roadTextureAlloc);
        roadTexture = VK_NULL_HANDLE;
    }

    // Destroy pipelines
    if (graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, graphicsPipeline, nullptr);
        graphicsPipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (shadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, shadowPipeline, nullptr);
        shadowPipeline = VK_NULL_HANDLE;
    }
    if (shadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, shadowPipelineLayout, nullptr);
        shadowPipelineLayout = VK_NULL_HANDLE;
    }
    if (shadowDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, shadowDescriptorSetLayout, nullptr);
        shadowDescriptorSetLayout = VK_NULL_HANDLE;
    }

    // Destroy meshes
    buildingsMesh.destroy(alloc);
    for (auto& mesh : buildingMeshes) {
        mesh.destroy(alloc);
    }
    roadMesh.destroy(alloc);

    generated = false;
}

bool TownSystem::createBuildingMeshes() {
    // Pre-generate meshes for each building type with default dimensions
    // The actual dimensions will be handled via instance transforms

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (size_t i = 0; i < NUM_BUILDING_TYPES; ++i) {
        BuildingType type = static_cast<BuildingType>(i);

        // Use canonical dimensions (will be scaled per-instance)
        glm::vec3 dims(1.0f, 1.0f, 1.0f);

        meshGenerator.generateBuilding(type, dims, static_cast<float>(i), vertices, indices);

        if (vertices.empty() || indices.empty()) {
            std::cerr << "TownSystem: Empty mesh for building type " << i << std::endl;
            continue;
        }

        buildingMeshes[i].setCustomGeometry(vertices, indices);
        buildingMeshes[i].upload(allocator, device, commandPool, graphicsQueue);
    }

    return true;
}

bool TownSystem::createRoadMesh() {
    // Create a simple unit road segment (will be transformed per-road)
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    meshGenerator.generateRoadSegment(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), 1.0f, vertices, indices);

    if (!vertices.empty() && !indices.empty()) {
        roadMesh.setCustomGeometry(vertices, indices);
        roadMesh.upload(allocator, device, commandPool, graphicsQueue);
    }

    return true;
}

bool TownSystem::createTextures() {
    // Create simple procedural textures for buildings and roads
    // Using a simple solid color with noise for now

    const uint32_t texSize = 64;
    std::vector<uint8_t> buildingPixels(texSize * texSize * 4);
    std::vector<uint8_t> roofPixels(texSize * texSize * 4);
    std::vector<uint8_t> roadPixels(texSize * texSize * 4);

    // Building texture: warm brown/tan color with variation
    for (uint32_t y = 0; y < texSize; ++y) {
        for (uint32_t x = 0; x < texSize; ++x) {
            uint32_t idx = (y * texSize + x) * 4;

            // Simple noise
            float noise = (std::sin(x * 0.5f) * std::cos(y * 0.5f) + 1.0f) * 0.5f;
            noise = noise * 0.2f + 0.8f;

            // Warm brown/beige
            buildingPixels[idx + 0] = static_cast<uint8_t>(180 * noise); // R
            buildingPixels[idx + 1] = static_cast<uint8_t>(150 * noise); // G
            buildingPixels[idx + 2] = static_cast<uint8_t>(120 * noise); // B
            buildingPixels[idx + 3] = 255; // A
        }
    }

    // Roof texture: darker brown/red
    for (uint32_t y = 0; y < texSize; ++y) {
        for (uint32_t x = 0; x < texSize; ++x) {
            uint32_t idx = (y * texSize + x) * 4;

            float noise = (std::sin(x * 0.3f) * std::cos(y * 0.3f) + 1.0f) * 0.5f;
            noise = noise * 0.3f + 0.7f;

            roofPixels[idx + 0] = static_cast<uint8_t>(140 * noise); // R
            roofPixels[idx + 1] = static_cast<uint8_t>(80 * noise);  // G
            roofPixels[idx + 2] = static_cast<uint8_t>(60 * noise);  // B
            roofPixels[idx + 3] = 255;
        }
    }

    // Road texture: dirt/gravel
    for (uint32_t y = 0; y < texSize; ++y) {
        for (uint32_t x = 0; x < texSize; ++x) {
            uint32_t idx = (y * texSize + x) * 4;

            float noise = (std::sin(x * 1.2f + y * 0.8f) + 1.0f) * 0.5f;
            noise = noise * 0.2f + 0.8f;

            // Brown/gray dirt
            roadPixels[idx + 0] = static_cast<uint8_t>(100 * noise); // R
            roadPixels[idx + 1] = static_cast<uint8_t>(85 * noise);  // G
            roadPixels[idx + 2] = static_cast<uint8_t>(70 * noise);  // B
            roadPixels[idx + 3] = 255;
        }
    }

    // Helper to create a texture
    auto createTexture = [&](const std::vector<uint8_t>& pixels, VkImage& image,
                             VmaAllocation& alloc, VkImageView& view) -> bool {
        // Create staging buffer
        VkBufferCreateInfo stagingInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        stagingInfo.size = pixels.size();
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        VkBuffer stagingBuffer;
        VmaAllocation stagingAlloc;
        if (vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAlloc, nullptr) != VK_SUCCESS) {
            return false;
        }

        void* mapped;
        vmaMapMemory(allocator, stagingAlloc, &mapped);
        memcpy(mapped, pixels.data(), pixels.size());
        vmaUnmapMemory(allocator, stagingAlloc);

        // Create image
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.extent = {texSize, texSize, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo imageAllocInfo{};
        imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateImage(allocator, &imageInfo, &imageAllocInfo, &image, &alloc, nullptr) != VK_SUCCESS) {
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
            return false;
        }

        // Transition and copy
        VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAllocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        // Transition to transfer dst
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {texSize, texSize, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition to shader read
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);

        // Create image view
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        return vkCreateImageView(device, &viewInfo, nullptr, &view) == VK_SUCCESS;
    };

    if (!createTexture(buildingPixels, buildingTexture, buildingTextureAlloc, buildingTextureView)) {
        return false;
    }
    if (!createTexture(roofPixels, roofTexture, roofTextureAlloc, roofTextureView)) {
        return false;
    }
    if (!createTexture(roadPixels, roadTexture, roadTextureAlloc, roadTextureView)) {
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 8.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    return vkCreateSampler(device, &samplerInfo, nullptr, &buildingTextureSampler) == VK_SUCCESS;
}

bool TownSystem::createDescriptorSetLayout() {
    // Main rendering descriptor set layout
    DescriptorManager::LayoutBuilder builder(device);
    builder.addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: UBO
           .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)                          // 1: Texture
           .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT);                         // 2: Shadow map

    descriptorSetLayout = builder.build();
    if (descriptorSetLayout == VK_NULL_HANDLE) {
        return false;
    }

    // Shadow pass descriptor set layout (just needs UBO)
    DescriptorManager::LayoutBuilder shadowBuilder(device);
    shadowBuilder.addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT);

    shadowDescriptorSetLayout = shadowBuilder.build();
    return shadowDescriptorSetLayout != VK_NULL_HANDLE;
}

bool TownSystem::createGraphicsPipeline() {
    // Push constant range
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(TownPushConstants);

    // Pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Vertex input setup
    auto bindingDesc = Vertex::getBindingDescription();
    auto attrDescs = Vertex::getAttributeDescriptions();
    std::vector<VkVertexInputBindingDescription> bindings = {bindingDesc};
    std::vector<VkVertexInputAttributeDescription> attributes(attrDescs.begin(), attrDescs.end());

    // Use GraphicsPipelineFactory
    GraphicsPipelineFactory factory(device);

    factory.setShaders(shaderPath + "/town.vert.spv", shaderPath + "/town.frag.spv")
           .setVertexInput(bindings, attributes)
           .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
           .setExtent(extent)
           .setPolygonMode(VK_POLYGON_MODE_FILL)
           .setCullMode(VK_CULL_MODE_BACK_BIT)
           .setFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE)
           .setSampleCount(VK_SAMPLE_COUNT_1_BIT)
           .setDepthTest(true)
           .setDepthWrite(true)
           .setDepthCompareOp(VK_COMPARE_OP_LESS)
           .setBlendMode(GraphicsPipelineFactory::BlendMode::None)
           .setPipelineLayout(pipelineLayout)
           .setRenderPass(renderPass, 0);

    if (!factory.build(graphicsPipeline)) {
        std::cerr << "TownSystem: Failed to build graphics pipeline" << std::endl;
        return false;
    }

    return true;
}

bool TownSystem::createShadowPipeline() {
    // Load shadow shader
    auto vertShader = loadShaderModule(device, shaderPath + "/town_shadow.vert.spv");

    if (vertShader == VK_NULL_HANDLE) {
        std::cerr << "TownSystem: Failed to load shadow vertex shader" << std::endl;
        return false;
    }

    // Push constant for shadow (just model + lightViewProj)
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::mat4) * 2;  // model + lightViewProj

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &shadowDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &shadowPipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, vertShader, nullptr);
        return false;
    }

    // Shadow pipeline - depth only, no fragment shader
    VkPipelineShaderStageCreateInfo shaderStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    shaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStage.module = vertShader;
    shaderStage.pName = "main";

    auto bindingDesc = Vertex::getBindingDescription();
    auto attrDescs = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0, 0, static_cast<float>(shadowMapSize), static_cast<float>(shadowMapSize), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {shadowMapSize, shadowMapSize}};

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.5f;
    rasterizer.depthBiasSlopeFactor = 1.75f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // No color attachment for shadow pass
    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 0;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 1;
    pipelineInfo.pStages = &shaderStage;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = shadowPipelineLayout;
    pipelineInfo.renderPass = shadowRenderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &shadowPipeline);

    vkDestroyShaderModule(device, vertShader, nullptr);

    return result == VK_SUCCESS;
}

bool TownSystem::createDescriptorSets() {
    descriptorSets.resize(framesInFlight);
    shadowDescriptorSets.resize(framesInFlight);

    // Allocate main descriptor sets
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = framesInFlight;
    allocInfo.pSetLayouts = layouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        return false;
    }

    // Allocate shadow descriptor sets
    std::vector<VkDescriptorSetLayout> shadowLayouts(framesInFlight, shadowDescriptorSetLayout);
    allocInfo.pSetLayouts = shadowLayouts.data();

    if (vkAllocateDescriptorSets(device, &allocInfo, shadowDescriptorSets.data()) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool TownSystem::createInstanceBuffers() {
    if (totalBuildingInstances == 0) return true;

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = totalBuildingInstances * sizeof(TownBuildingInstance);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    return vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buildingInstanceBuffer, &buildingInstanceAlloc, nullptr) == VK_SUCCESS;
}

void TownSystem::generate(const TownConfig& config, std::function<float(float, float)> heightFunc) {
    generator.generate(config, heightFunc);
    generated = true;

    // Generate combined building mesh from modular system
    generateCombinedBuildingMesh();

    updateInstanceData();
}

void TownSystem::generateCombinedBuildingMesh() {
    const auto& buildings = generator.getBuildings();
    const auto& library = generator.getModuleLibrary();

    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;

    for (const auto& building : buildings) {
        if (building.moduleGrid.empty()) continue;

        // Calculate world offset for this building
        // Building position is at center of footprint, so offset by half dimensions
        glm::vec3 buildingOffset = building.position;
        buildingOffset.x -= building.dimensions.x * 0.5f;
        buildingOffset.z -= building.dimensions.z * 0.5f;

        // Apply rotation around building center
        glm::mat4 rotationMatrix = glm::rotate(glm::mat4(1.0f), building.rotation, glm::vec3(0, 1, 0));
        glm::vec3 buildingCenter = building.position;

        uint32_t baseVertex = static_cast<uint32_t>(allVertices.size());

        // Generate mesh for each module in the building grid
        for (int z = 0; z < building.gridSize.z; ++z) {
            for (int y = 0; y < building.gridSize.y; ++y) {
                for (int x = 0; x < building.gridSize.x; ++x) {
                    size_t gridIdx = x + y * building.gridSize.x + z * building.gridSize.x * building.gridSize.y;
                    size_t moduleIdx = building.moduleGrid[gridIdx];

                    if (moduleIdx >= library.getModuleCount()) continue;
                    const BuildingModule& mod = library.getModule(moduleIdx);
                    if (mod.type == ModuleType::Air) continue;

                    // Calculate module position in local space
                    glm::vec3 moduleLocalPos = glm::vec3(x, y, z) * ModuleMeshGenerator::MODULE_SIZE;

                    // Generate module mesh
                    std::vector<Vertex> moduleVerts;
                    std::vector<uint32_t> moduleInds;
                    moduleMeshGenerator.generateModuleMesh(mod.type, moduleVerts, moduleInds);

                    // Transform vertices to world space with rotation
                    for (auto& v : moduleVerts) {
                        // Offset by module position in local building space
                        glm::vec3 localPos = v.position + moduleLocalPos;

                        // Apply rotation around building center
                        glm::vec3 relativePos = localPos + buildingOffset - buildingCenter;
                        relativePos = glm::vec3(rotationMatrix * glm::vec4(relativePos, 1.0f));
                        v.position = relativePos + buildingCenter;

                        // Rotate normal
                        v.normal = glm::vec3(rotationMatrix * glm::vec4(v.normal, 0.0f));

                        // Rotate tangent
                        glm::vec3 tangent3 = glm::vec3(v.tangent);
                        tangent3 = glm::vec3(rotationMatrix * glm::vec4(tangent3, 0.0f));
                        v.tangent = glm::vec4(tangent3, v.tangent.w);

                        allVertices.push_back(v);
                    }

                    // Add indices with offset
                    for (uint32_t idx : moduleInds) {
                        allIndices.push_back(baseVertex + idx);
                    }
                    baseVertex = static_cast<uint32_t>(allVertices.size());
                }
            }
        }
    }

    // Upload combined mesh
    if (!allVertices.empty() && !allIndices.empty()) {
        buildingsMesh.setCustomGeometry(allVertices, allIndices);
        buildingsMesh.upload(allocator, device, commandPool, graphicsQueue);
    }
}

void TownSystem::updateInstanceData() {
    // Clear old data
    for (auto& instances : buildingInstances) {
        instances.clear();
    }

    const auto& buildings = generator.getBuildings();

    for (const auto& building : buildings) {
        size_t typeIdx = static_cast<size_t>(building.type);
        if (typeIdx >= NUM_BUILDING_TYPES) continue;

        TownBuildingInstance instance;

        // Build model matrix: translate, rotate, scale
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, building.position);
        model = glm::rotate(model, building.rotation, glm::vec3(0, 1, 0));
        model = glm::scale(model, building.dimensions * building.scale);

        instance.modelMatrix = model;

        // Color variation based on building type and position
        float hash = glm::fract(std::sin(glm::dot(glm::vec2(building.position.x, building.position.z),
                                                   glm::vec2(127.1f, 311.7f))) * 43758.5453f);

        // Warm color variations
        instance.colorTint = glm::vec4(
            0.85f + hash * 0.15f,      // R
            0.75f + hash * 0.2f,       // G
            0.65f + hash * 0.25f,      // B
            0.7f + hash * 0.2f         // roughness
        );

        instance.params = glm::vec4(
            0.1f,                               // metallic
            static_cast<float>(typeIdx),        // building type
            0.0f, 0.0f
        );

        buildingInstances[typeIdx].push_back(instance);
    }

    // Calculate offsets and counts
    totalBuildingInstances = 0;
    for (size_t i = 0; i < NUM_BUILDING_TYPES; ++i) {
        buildingInstanceOffsets[i] = totalBuildingInstances;
        buildingInstanceCounts[i] = static_cast<uint32_t>(buildingInstances[i].size());
        totalBuildingInstances += buildingInstanceCounts[i];
    }

    // Prepare road transforms
    roadTransforms.clear();
    roadWidths.clear();

    const auto& roads = generator.getRoads();
    for (const auto& road : roads) {
        glm::vec3 dir = road.end - road.start;
        float length = glm::length(dir);
        if (length < 0.01f) continue;

        dir /= length;

        // Build transform: position at midpoint, rotate to align with direction, scale to length
        glm::vec3 midpoint = (road.start + road.end) * 0.5f;

        float angle = std::atan2(dir.x, dir.z);

        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, road.start);
        transform = glm::rotate(transform, angle, glm::vec3(0, 1, 0));
        transform = glm::scale(transform, glm::vec3(road.width, 1.0f, length));

        roadTransforms.push_back(transform);
        roadWidths.push_back(road.width);
    }
}

void TownSystem::updateDescriptorSets(VkDevice dev,
                                       const std::vector<VkBuffer>& sceneUniformBuffers,
                                       VkImageView shadowMapView,
                                       VkSampler shadowSampler) {
    for (size_t i = 0; i < framesInFlight; ++i) {
        // Main descriptor set
        DescriptorManager::SetWriter writer(dev, descriptorSets[i]);
        writer.writeBuffer(0, sceneUniformBuffers[i], 0, sizeof(UniformBufferObject))
              .writeImage(1, buildingTextureView, buildingTextureSampler)
              .writeImage(2, shadowMapView, shadowSampler)
              .update();

        // Shadow descriptor set
        DescriptorManager::SetWriter shadowWriter(dev, shadowDescriptorSets[i]);
        shadowWriter.writeBuffer(0, sceneUniformBuffers[i], 0, sizeof(UniformBufferObject))
                    .update();
    }
}

void TownSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!generated) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                            &descriptorSets[frameIndex], 0, nullptr);

    // Draw combined buildings mesh (already in world space)
    if (buildingsMesh.getIndexCount() > 0) {
        VkBuffer vertexBuffers[] = {buildingsMesh.getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, buildingsMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // Identity transform - mesh is already in world space
        TownPushConstants push;
        push.model = glm::mat4(1.0f);
        push.roughness = 0.7f;
        push.metallic = 0.0f;

        vkCmdPushConstants(cmd, pipelineLayout,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(TownPushConstants), &push);

        vkCmdDrawIndexed(cmd, buildingsMesh.getIndexCount(), 1, 0, 0, 0);
    }

    // Draw roads
    if (!roadTransforms.empty() && roadMesh.getIndexCount() > 0) {
        VkBuffer vertexBuffers[] = {roadMesh.getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, roadMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        for (size_t i = 0; i < roadTransforms.size(); ++i) {
            TownPushConstants push;
            push.model = roadTransforms[i];
            push.roughness = 0.9f;
            push.metallic = 0.0f;

            vkCmdPushConstants(cmd, pipelineLayout,
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(TownPushConstants), &push);

            vkCmdDrawIndexed(cmd, roadMesh.getIndexCount(), 1, 0, 0, 0);
        }
    }
}

void TownSystem::recordShadowDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                   const glm::mat4& lightViewProj, int cascadeIndex) {
    if (!generated) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelineLayout, 0, 1,
                            &shadowDescriptorSets[frameIndex], 0, nullptr);

    // Shadow push constants: model + lightViewProj
    struct ShadowPush {
        glm::mat4 model;
        glm::mat4 lightViewProj;
    } shadowPush;

    shadowPush.lightViewProj = lightViewProj;

    // Draw combined buildings mesh shadow
    if (buildingsMesh.getIndexCount() > 0) {
        VkBuffer vertexBuffers[] = {buildingsMesh.getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, buildingsMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        shadowPush.model = glm::mat4(1.0f);  // Identity - mesh is in world space

        vkCmdPushConstants(cmd, shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                          0, sizeof(ShadowPush), &shadowPush);

        vkCmdDrawIndexed(cmd, buildingsMesh.getIndexCount(), 1, 0, 0, 0);
    }
}
