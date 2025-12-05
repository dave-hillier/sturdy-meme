#include "WaterSystem.h"
#include "ShadowSystem.h"
#include "GraphicsPipelineFactory.h"
#include "BindingBuilder.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstring>

bool WaterSystem::init(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    hdrRenderPass = info.hdrRenderPass;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    extent = info.extent;
    commandPool = info.commandPool;
    graphicsQueue = info.graphicsQueue;
    waterSize = info.waterSize;

    // Initialize default water parameters
    waterUniforms.waterColor = glm::vec4(0.1f, 0.3f, 0.5f, 0.85f);  // Blue-green, semi-transparent
    waterUniforms.waveParams = glm::vec4(0.5f, 8.0f, 0.4f, 1.0f);   // amplitude, wavelength, steepness, speed
    waterUniforms.waveParams2 = glm::vec4(0.3f, 5.0f, 0.3f, 1.2f);  // Secondary wave
    waterUniforms.waterExtent = glm::vec4(0.0f, 0.0f, 100.0f, 100.0f);  // position, size
    waterUniforms.waterLevel = 0.0f;
    waterUniforms.foamThreshold = 0.3f;
    waterUniforms.fresnelPower = 5.0f;
    waterUniforms.terrainSize = 16384.0f;      // Default terrain size
    waterUniforms.terrainHeightScale = 220.0f; // Default height scale
    waterUniforms.shoreBlendDistance = 3.0f;   // 3m shore blend
    waterUniforms.shoreFoamWidth = 5.0f;       // 5m shore foam band
    waterUniforms.flowStrength = 1.0f;         // 1m UV offset per flow cycle
    waterUniforms.flowSpeed = 0.5f;            // Flow animation speed
    waterUniforms.flowFoamStrength = 0.5f;     // Flow-based foam intensity
    waterUniforms.fbmNearDistance = 50.0f;     // Max detail within 50m
    waterUniforms.fbmFarDistance = 500.0f;     // Min detail beyond 500m

    // PBR Scattering defaults (Ocean type)
    // Absorption coefficients based on real ocean water optical properties
    // Red absorbs fastest, blue slowest
    waterUniforms.scatteringCoeffs = glm::vec4(0.45f, 0.09f, 0.02f, 0.1f); // absorption RGB + turbidity
    waterUniforms.specularRoughness = 0.05f;   // Water is quite smooth
    waterUniforms.absorptionScale = 0.15f;     // Depth-based absorption rate
    waterUniforms.scatteringScale = 1.0f;      // Turbidity multiplier
    waterUniforms.displacementScale = 1.0f;   // Interactive displacement scale (Phase 4)

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;
    if (!createWaterMesh()) return false;
    if (!createUniformBuffers()) return false;

    return true;
}

void WaterSystem::destroy(VkDevice device, VmaAllocator allocator) {
    // Destroy uniform buffers
    for (size_t i = 0; i < waterUniformBuffers.size(); i++) {
        if (waterUniformBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(allocator, waterUniformBuffers[i], waterUniformAllocations[i]);
        }
    }
    waterUniformBuffers.clear();
    waterUniformAllocations.clear();
    waterUniformMapped.clear();

    // Destroy mesh
    waterMesh.destroy(allocator);

    // Destroy pipeline resources
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    descriptorSets.clear();
}

bool WaterSystem::createDescriptorSetLayout() {
    // Water shader bindings:
    // 0: Main UBO (scene uniforms)
    // 1: Water uniforms
    // 2: Shadow map array (for shadow sampling)
    // 3: Terrain heightmap (for shore detection)
    // 4: Flow map (for water flow direction and speed)

    auto uboBinding = BindingBuilder()
        .setBinding(0)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto waterUniformBinding = BindingBuilder()
        .setBinding(1)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
        .setStageFlags(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto shadowMapBinding = BindingBuilder()
        .setBinding(2)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto terrainHeightMapBinding = BindingBuilder()
        .setBinding(3)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto flowMapBinding = BindingBuilder()
        .setBinding(4)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    std::array<VkDescriptorSetLayoutBinding, 5> bindings = {
        uboBinding, waterUniformBinding, shadowMapBinding, terrainHeightMapBinding, flowMapBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create water descriptor set layout");
        return false;
    }

    // Create pipeline layout with push constants for model matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(glm::mat4);  // Just model matrix

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create water pipeline layout");
        return false;
    }

    return true;
}

bool WaterSystem::createPipeline() {
    GraphicsPipelineFactory factory(device);

    // Get vertex input from Vertex struct
    auto bindingDesc = Vertex::getBindingDescription();
    auto attrDescs = Vertex::getAttributeDescriptions();

    std::vector<VkVertexInputBindingDescription> bindings = {bindingDesc};
    std::vector<VkVertexInputAttributeDescription> attributes(attrDescs.begin(), attrDescs.end());

    // Water pipeline: alpha blending, depth test but no depth write (for transparency)
    bool success = factory
        .setShaders(shaderPath + "/water.vert.spv", shaderPath + "/water.frag.spv")
        .setRenderPass(hdrRenderPass)
        .setPipelineLayout(pipelineLayout)
        .setExtent(extent)
        .setVertexInput(bindings, attributes)
        .setDepthTest(true)
        .setDepthWrite(false)  // Don't write depth for transparent water
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Alpha)
        .setCullMode(VK_CULL_MODE_NONE)  // Render both sides of water
        .build(pipeline);

    if (!success) {
        SDL_Log("Failed to create water pipeline");
        return false;
    }

    return true;
}

bool WaterSystem::createWaterMesh() {
    // Create a subdivided plane for the water surface
    // More subdivisions = smoother wave animation
    // Scale grid resolution based on water size for better wave detail
    // For large ocean planes, we need more vertices but there's a limit
    int gridSize = 64;
    if (waterSize > 1000.0f) gridSize = 256;
    if (waterSize > 20000.0f) gridSize = 512;  // For horizon extension
    const float size = waterSize;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Generate vertices
    for (int z = 0; z <= gridSize; z++) {
        for (int x = 0; x <= gridSize; x++) {
            Vertex v;
            float u = static_cast<float>(x) / gridSize;
            float v_coord = static_cast<float>(z) / gridSize;

            v.position = glm::vec3(
                (u - 0.5f) * size,
                0.0f,
                (v_coord - 0.5f) * size
            );
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.texCoord = glm::vec2(u, v_coord);
            v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            v.color = glm::vec4(1.0f);

            vertices.push_back(v);
        }
    }

    // Generate indices
    for (int z = 0; z < gridSize; z++) {
        for (int x = 0; x < gridSize; x++) {
            uint32_t topLeft = z * (gridSize + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * (gridSize + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;

            // First triangle
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Second triangle
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    waterMesh.setCustomGeometry(vertices, indices);
    waterMesh.upload(allocator, device, commandPool, graphicsQueue);

    SDL_Log("Water mesh created with %zu vertices, %zu indices",
            vertices.size(), indices.size());

    return true;
}

bool WaterSystem::createUniformBuffers() {
    waterUniformBuffers.resize(framesInFlight);
    waterUniformAllocations.resize(framesInFlight);
    waterUniformMapped.resize(framesInFlight);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(WaterUniforms);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    for (uint32_t i = 0; i < framesInFlight; i++) {
        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo,
                           &waterUniformBuffers[i], &waterUniformAllocations[i],
                           &allocationInfo) != VK_SUCCESS) {
            SDL_Log("Failed to create water uniform buffer %u", i);
            return false;
        }
        waterUniformMapped[i] = allocationInfo.pMappedData;
    }

    return true;
}

bool WaterSystem::createDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                                        VkDeviceSize uniformBufferSize,
                                        ShadowSystem& shadowSystem,
                                        VkImageView terrainHeightMapView,
                                        VkSampler terrainHeightMapSampler,
                                        VkImageView flowMapView,
                                        VkSampler flowMapSampler) {
    // Allocate descriptor sets using managed pool
    descriptorSets = descriptorPool->allocate(descriptorSetLayout, framesInFlight);
    if (descriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate water descriptor sets");
        return false;
    }

    // Get shadow resources
    VkImageView shadowView = shadowSystem.getShadowImageView();
    VkSampler shadowSampler = shadowSystem.getShadowSampler();

    // Update each descriptor set
    for (size_t i = 0; i < framesInFlight; i++) {
        // Main UBO binding
        VkDescriptorBufferInfo mainUboInfo{};
        mainUboInfo.buffer = uniformBuffers[i];
        mainUboInfo.offset = 0;
        mainUboInfo.range = uniformBufferSize;

        // Water uniforms binding
        VkDescriptorBufferInfo waterUboInfo{};
        waterUboInfo.buffer = waterUniformBuffers[i];
        waterUboInfo.offset = 0;
        waterUboInfo.range = sizeof(WaterUniforms);

        // Shadow map binding
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowView;
        shadowInfo.sampler = shadowSampler;

        // Terrain heightmap binding
        VkDescriptorImageInfo terrainInfo{};
        terrainInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        terrainInfo.imageView = terrainHeightMapView;
        terrainInfo.sampler = terrainHeightMapSampler;

        // Flow map binding
        VkDescriptorImageInfo flowInfo{};
        flowInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        flowInfo.imageView = flowMapView;
        flowInfo.sampler = flowMapSampler;

        std::array<VkWriteDescriptorSet, 5> descriptorWrites{};

        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &mainUboInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &waterUboInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = descriptorSets[i];
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &shadowInfo;

        descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[3].dstSet = descriptorSets[i];
        descriptorWrites[3].dstBinding = 3;
        descriptorWrites[3].dstArrayElement = 0;
        descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[3].descriptorCount = 1;
        descriptorWrites[3].pImageInfo = &terrainInfo;

        descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[4].dstSet = descriptorSets[i];
        descriptorWrites[4].dstBinding = 4;
        descriptorWrites[4].dstArrayElement = 0;
        descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[4].descriptorCount = 1;
        descriptorWrites[4].pImageInfo = &flowInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }

    SDL_Log("Water descriptor sets created with terrain heightmap and flow map");
    return true;
}

void WaterSystem::updateUniforms(uint32_t frameIndex) {
    // Copy water uniforms to mapped buffer
    std::memcpy(waterUniformMapped[frameIndex], &waterUniforms, sizeof(WaterUniforms));
}

void WaterSystem::setWaterExtent(const glm::vec2& position, const glm::vec2& size) {
    waterUniforms.waterExtent = glm::vec4(position.x, position.y, size.x, size.y);

    // Update model matrix to position the water plane
    waterModelMatrix = glm::mat4(1.0f);
    waterModelMatrix = glm::translate(waterModelMatrix,
                                       glm::vec3(position.x, waterUniforms.waterLevel, position.y));
}

void WaterSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1, &descriptorSets[frameIndex], 0, nullptr);

    // Push model matrix
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(
        waterUniforms.waterExtent.x,
        waterUniforms.waterLevel,
        waterUniforms.waterExtent.y
    ));
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(glm::mat4), &model);

    // Bind water mesh and draw
    VkBuffer vertexBuffers[] = {waterMesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, waterMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, waterMesh.getIndexCount(), 1, 0, 0, 0);
}

void WaterSystem::updateTide(float tideHeight) {
    // tideHeight is normalized -1 to +1 from CelestialCalculator::calculateTide()
    // Scale by tidalRange and add to base water level
    waterUniforms.waterLevel = baseWaterLevel + (tideHeight * tidalRange);
}

void WaterSystem::setWaterType(WaterType type) {
    // Water type presets based on real-world optical properties
    // Absorption coefficients: how quickly each wavelength is absorbed (higher = faster absorption)
    // Real water absorbs red fastest, then green, then blue
    // Turbidity: amount of suspended particles causing scattering

    switch (type) {
        case WaterType::Ocean:
            // Deep ocean: very clear, strong blue tint
            waterUniforms.scatteringCoeffs = glm::vec4(0.45f, 0.09f, 0.02f, 0.05f);
            waterUniforms.waterColor = glm::vec4(0.01f, 0.03f, 0.08f, 0.95f);
            waterUniforms.absorptionScale = 0.12f;
            waterUniforms.scatteringScale = 0.8f;
            break;

        case WaterType::CoastalOcean:
            // Coastal waters: more sediment, blue-green
            waterUniforms.scatteringCoeffs = glm::vec4(0.35f, 0.12f, 0.05f, 0.15f);
            waterUniforms.waterColor = glm::vec4(0.02f, 0.06f, 0.10f, 0.92f);
            waterUniforms.absorptionScale = 0.18f;
            waterUniforms.scatteringScale = 1.2f;
            break;

        case WaterType::River:
            // River: green-brown tint, moderate turbidity
            waterUniforms.scatteringCoeffs = glm::vec4(0.25f, 0.18f, 0.12f, 0.25f);
            waterUniforms.waterColor = glm::vec4(0.04f, 0.08f, 0.06f, 0.90f);
            waterUniforms.absorptionScale = 0.25f;
            waterUniforms.scatteringScale = 1.5f;
            break;

        case WaterType::MuddyRiver:
            // Muddy river: brown, high turbidity
            waterUniforms.scatteringCoeffs = glm::vec4(0.15f, 0.20f, 0.25f, 0.6f);
            waterUniforms.waterColor = glm::vec4(0.12f, 0.10f, 0.06f, 0.85f);
            waterUniforms.absorptionScale = 0.4f;
            waterUniforms.scatteringScale = 2.5f;
            break;

        case WaterType::ClearStream:
            // Mountain stream: extremely clear
            waterUniforms.scatteringCoeffs = glm::vec4(0.50f, 0.08f, 0.01f, 0.02f);
            waterUniforms.waterColor = glm::vec4(0.01f, 0.04f, 0.08f, 0.98f);
            waterUniforms.absorptionScale = 0.08f;
            waterUniforms.scatteringScale = 0.5f;
            break;

        case WaterType::Lake:
            // Lake: dark blue-green, moderate clarity
            waterUniforms.scatteringCoeffs = glm::vec4(0.35f, 0.15f, 0.08f, 0.12f);
            waterUniforms.waterColor = glm::vec4(0.02f, 0.05f, 0.08f, 0.93f);
            waterUniforms.absorptionScale = 0.20f;
            waterUniforms.scatteringScale = 1.0f;
            break;

        case WaterType::Swamp:
            // Swamp: dark green-brown, very turbid
            waterUniforms.scatteringCoeffs = glm::vec4(0.10f, 0.15f, 0.20f, 0.8f);
            waterUniforms.waterColor = glm::vec4(0.08f, 0.10f, 0.04f, 0.80f);
            waterUniforms.absorptionScale = 0.5f;
            waterUniforms.scatteringScale = 3.0f;
            break;

        case WaterType::Tropical:
            // Tropical: bright turquoise, very clear
            waterUniforms.scatteringCoeffs = glm::vec4(0.55f, 0.06f, 0.03f, 0.03f);
            waterUniforms.waterColor = glm::vec4(0.0f, 0.08f, 0.12f, 0.97f);
            waterUniforms.absorptionScale = 0.06f;
            waterUniforms.scatteringScale = 0.4f;
            break;
    }

    SDL_Log("Water type set with absorption (%.2f, %.2f, %.2f), turbidity %.2f",
            waterUniforms.scatteringCoeffs.r, waterUniforms.scatteringCoeffs.g,
            waterUniforms.scatteringCoeffs.b, waterUniforms.scatteringCoeffs.a);
}
