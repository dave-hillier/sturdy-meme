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
    assetPath = info.assetPath;

    // Initialize default water parameters - English estuary/coastal style
    waterUniforms.waterColor = glm::vec4(0.15f, 0.22f, 0.25f, 0.9f);  // Grey-green estuary color
    waterUniforms.waveParams = glm::vec4(0.3f, 15.0f, 0.25f, 0.5f);   // amplitude, wavelength, steepness, speed (English Channel swell)
    waterUniforms.waveParams2 = glm::vec4(0.15f, 5.0f, 0.35f, 0.8f);  // Secondary wave (medium chop)
    waterUniforms.waterExtent = glm::vec4(0.0f, 0.0f, 100.0f, 100.0f);  // position, size
    waterUniforms.waterLevel = 0.0f;
    waterUniforms.foamThreshold = 0.25f;  // Higher threshold for realistic whitecaps
    waterUniforms.fresnelPower = 5.0f;
    waterUniforms.terrainSize = 16384.0f;      // Default terrain size
    waterUniforms.terrainHeightScale = 235.0f; // Default height scale (maxAlt - minAlt = 220 - (-15))
    waterUniforms.shoreBlendDistance = 8.0f;   // 8m shore blend (wider for muddy estuaries)
    waterUniforms.shoreFoamWidth = 15.0f;      // 15m shore foam band (much wider)
    waterUniforms.flowStrength = 1.0f;         // 1m UV offset per flow cycle
    waterUniforms.flowSpeed = 0.5f;            // Flow animation speed
    waterUniforms.flowFoamStrength = 0.5f;     // Flow-based foam intensity
    waterUniforms.fbmNearDistance = 50.0f;     // Max detail within 50m
    waterUniforms.fbmFarDistance = 500.0f;     // Min detail beyond 500m

    // PBR Scattering defaults (English estuary - murkier than ocean)
    // Higher turbidity for sediment-laden coastal waters
    waterUniforms.scatteringCoeffs = glm::vec4(0.6f, 0.15f, 0.05f, 0.3f); // absorption RGB + turbidity (murky)
    waterUniforms.specularRoughness = 0.05f;   // Water is quite smooth
    waterUniforms.absorptionScale = 0.15f;     // Depth-based absorption rate
    waterUniforms.scatteringScale = 1.0f;      // Turbidity multiplier
    waterUniforms.displacementScale = 1.0f;   // Interactive displacement scale (Phase 4)
    waterUniforms.sssIntensity = 1.5f;        // Subsurface scattering intensity (Phase 17)
    waterUniforms.causticsScale = 0.1f;       // Caustics pattern scale (Phase 9)
    waterUniforms.causticsSpeed = 0.8f;       // Caustics animation speed (Phase 9)
    waterUniforms.causticsIntensity = 0.5f;   // Caustics brightness (Phase 9)
    waterUniforms.nearPlane = 0.1f;           // Default camera near plane
    waterUniforms.farPlane = 50000.0f;        // Default camera far plane (matches Camera.cpp)
    waterUniforms.padding1 = 0.0f;
    waterUniforms.padding2 = 0.0f;

    // Phase 12: Material blending defaults
    // Secondary material defaults to same as primary (no blending)
    waterUniforms.waterColor2 = waterUniforms.waterColor;
    waterUniforms.scatteringCoeffs2 = waterUniforms.scatteringCoeffs;
    waterUniforms.absorptionScale2 = waterUniforms.absorptionScale;
    waterUniforms.scatteringScale2 = waterUniforms.scatteringScale;
    waterUniforms.specularRoughness2 = waterUniforms.specularRoughness;
    waterUniforms.sssIntensity2 = waterUniforms.sssIntensity;
    waterUniforms.blendCenter = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    waterUniforms.blendDistance = 50.0f;  // Default 50m blend distance
    waterUniforms.blendMode = 0;          // Distance mode

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;
    if (!createWaterMesh()) return false;
    if (!createUniformBuffers()) return false;
    if (!loadFoamTexture()) return false;
    if (!loadCausticsTexture()) return false;

    return true;
}

void WaterSystem::destroy(VkDevice device, VmaAllocator allocator) {
    // Destroy foam texture
    foamTexture.destroy(allocator, device);

    // Destroy caustics texture
    causticsTexture.destroy(allocator, device);

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
    // 5: Displacement map (for interactive splashes)
    // 6: Foam noise texture (tileable Worley noise)
    // 7: Temporal foam buffer (Phase 14: persistent foam)
    // 8: Caustics texture (Phase 9: animated underwater light patterns)
    // 9: SSR texture (Phase 10: screen-space reflections)
    // 10: Scene depth texture (Phase 11: dual depth for refraction)

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

    auto displacementMapBinding = BindingBuilder()
        .setBinding(5)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    auto foamTextureBinding = BindingBuilder()
        .setBinding(6)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto temporalFoamBinding = BindingBuilder()
        .setBinding(7)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto causticsTextureBinding = BindingBuilder()
        .setBinding(8)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto ssrTextureBinding = BindingBuilder()
        .setBinding(9)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    auto sceneDepthBinding = BindingBuilder()
        .setBinding(10)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    // FFT Ocean displacement maps (bindings 11-13, vertex shader)
    auto oceanDispBinding = BindingBuilder()
        .setBinding(11)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    auto oceanNormalBinding = BindingBuilder()
        .setBinding(12)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    auto oceanFoamBinding = BindingBuilder()
        .setBinding(13)
        .setDescriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .setStageFlags(VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    std::array<VkDescriptorSetLayoutBinding, 14> bindings = {
        uboBinding, waterUniformBinding, shadowMapBinding, terrainHeightMapBinding,
        flowMapBinding, displacementMapBinding, foamTextureBinding, temporalFoamBinding,
        causticsTextureBinding, ssrTextureBinding, sceneDepthBinding,
        oceanDispBinding, oceanNormalBinding, oceanFoamBinding
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        SDL_Log("Failed to create water descriptor set layout");
        return false;
    }

    // Create pipeline layout with push constants for model matrix + FFT params
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

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
    // Depth bias prevents z-fighting flickering at water/terrain intersection
    bool success = factory
        .setShaders(shaderPath + "/water.vert.spv", shaderPath + "/water.frag.spv")
        .setRenderPass(hdrRenderPass)
        .setPipelineLayout(pipelineLayout)
        .setExtent(extent)
        .setVertexInput(bindings, attributes)
        .setDepthTest(true)
        .setDepthWrite(false)  // Don't write depth for transparent water
        .setDepthBias(1.0f, 1.5f)  // Bias water slightly away from camera to prevent z-fighting
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

bool WaterSystem::loadFoamTexture() {
    std::string foamPath = assetPath + "/textures/foam_noise.png";

    // Try to load the foam texture, fall back to white if not found
    if (!foamTexture.load(foamPath, allocator, device, commandPool, graphicsQueue, physicalDevice, false)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Foam texture not found at %s, creating fallback white texture", foamPath.c_str());
        // Create a 1x1 white texture as fallback
        if (!foamTexture.createSolidColor(255, 255, 255, 255, allocator, device, commandPool, graphicsQueue)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create fallback foam texture");
            return false;
        }
    } else {
        SDL_Log("Loaded foam texture from %s", foamPath.c_str());
    }

    return true;
}

bool WaterSystem::loadCausticsTexture() {
    std::string causticsPath = assetPath + "/textures/caustics.png";

    // Try to load the caustics texture, fall back to white if not found
    if (!causticsTexture.load(causticsPath, allocator, device, commandPool, graphicsQueue, physicalDevice, false)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Caustics texture not found at %s, creating fallback white texture", causticsPath.c_str());
        // Create a 1x1 white texture as fallback
        if (!causticsTexture.createSolidColor(255, 255, 255, 255, allocator, device, commandPool, graphicsQueue)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create fallback caustics texture");
            return false;
        }
    } else {
        SDL_Log("Loaded caustics texture from %s", causticsPath.c_str());
    }

    return true;
}

bool WaterSystem::createDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                                        VkDeviceSize uniformBufferSize,
                                        ShadowSystem& shadowSystem,
                                        VkImageView terrainHeightMapView,
                                        VkSampler terrainHeightMapSampler,
                                        VkImageView flowMapView,
                                        VkSampler flowMapSampler,
                                        VkImageView displacementMapView,
                                        VkSampler displacementMapSampler,
                                        VkImageView temporalFoamView,
                                        VkSampler temporalFoamSampler,
                                        VkImageView ssrView,
                                        VkSampler ssrSampler,
                                        VkImageView sceneDepthView,
                                        VkSampler sceneDepthSampler) {
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

        // Displacement map binding (Phase 4: interactive splashes)
        VkDescriptorImageInfo displacementInfo{};
        displacementInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        displacementInfo.imageView = displacementMapView;
        displacementInfo.sampler = displacementMapSampler;

        // Foam noise texture binding
        VkDescriptorImageInfo foamInfo{};
        foamInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        foamInfo.imageView = foamTexture.getImageView();
        foamInfo.sampler = foamTexture.getSampler();

        // Temporal foam buffer binding (Phase 14: persistent foam)
        VkDescriptorImageInfo temporalFoamInfo{};
        temporalFoamInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        temporalFoamInfo.imageView = temporalFoamView;
        temporalFoamInfo.sampler = temporalFoamSampler;

        // Caustics texture binding (Phase 9: underwater light patterns)
        VkDescriptorImageInfo causticsInfo{};
        causticsInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        causticsInfo.imageView = causticsTexture.getImageView();
        causticsInfo.sampler = causticsTexture.getSampler();

        // SSR texture binding (Phase 10: screen-space reflections)
        VkDescriptorImageInfo ssrInfo{};
        ssrInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;  // SSR uses general layout for compute
        ssrInfo.imageView = ssrView;
        ssrInfo.sampler = ssrSampler;

        // Scene depth binding (Phase 11: dual depth for refraction)
        VkDescriptorImageInfo sceneDepthInfo{};
        sceneDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        sceneDepthInfo.imageView = sceneDepthView;
        sceneDepthInfo.sampler = sceneDepthSampler;

        // FFT Ocean bindings (11-13) - use displacement map as placeholder until FFT is integrated
        VkDescriptorImageInfo oceanDispInfo{};
        oceanDispInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        oceanDispInfo.imageView = displacementMapView;
        oceanDispInfo.sampler = displacementMapSampler;

        VkDescriptorImageInfo oceanNormalInfo{};
        oceanNormalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        oceanNormalInfo.imageView = displacementMapView;
        oceanNormalInfo.sampler = displacementMapSampler;

        VkDescriptorImageInfo oceanFoamInfo{};
        oceanFoamInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        oceanFoamInfo.imageView = displacementMapView;
        oceanFoamInfo.sampler = displacementMapSampler;

        std::array<VkWriteDescriptorSet, 14> descriptorWrites{};

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

        descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[5].dstSet = descriptorSets[i];
        descriptorWrites[5].dstBinding = 5;
        descriptorWrites[5].dstArrayElement = 0;
        descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[5].descriptorCount = 1;
        descriptorWrites[5].pImageInfo = &displacementInfo;

        descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[6].dstSet = descriptorSets[i];
        descriptorWrites[6].dstBinding = 6;
        descriptorWrites[6].dstArrayElement = 0;
        descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[6].descriptorCount = 1;
        descriptorWrites[6].pImageInfo = &foamInfo;

        descriptorWrites[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[7].dstSet = descriptorSets[i];
        descriptorWrites[7].dstBinding = 7;
        descriptorWrites[7].dstArrayElement = 0;
        descriptorWrites[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[7].descriptorCount = 1;
        descriptorWrites[7].pImageInfo = &temporalFoamInfo;

        descriptorWrites[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[8].dstSet = descriptorSets[i];
        descriptorWrites[8].dstBinding = 8;
        descriptorWrites[8].dstArrayElement = 0;
        descriptorWrites[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[8].descriptorCount = 1;
        descriptorWrites[8].pImageInfo = &causticsInfo;

        descriptorWrites[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[9].dstSet = descriptorSets[i];
        descriptorWrites[9].dstBinding = 9;
        descriptorWrites[9].dstArrayElement = 0;
        descriptorWrites[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[9].descriptorCount = 1;
        descriptorWrites[9].pImageInfo = &ssrInfo;

        descriptorWrites[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[10].dstSet = descriptorSets[i];
        descriptorWrites[10].dstBinding = 10;
        descriptorWrites[10].dstArrayElement = 0;
        descriptorWrites[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[10].descriptorCount = 1;
        descriptorWrites[10].pImageInfo = &sceneDepthInfo;

        // FFT Ocean displacement (binding 11)
        descriptorWrites[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[11].dstSet = descriptorSets[i];
        descriptorWrites[11].dstBinding = 11;
        descriptorWrites[11].dstArrayElement = 0;
        descriptorWrites[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[11].descriptorCount = 1;
        descriptorWrites[11].pImageInfo = &oceanDispInfo;

        // FFT Ocean normal (binding 12)
        descriptorWrites[12].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[12].dstSet = descriptorSets[i];
        descriptorWrites[12].dstBinding = 12;
        descriptorWrites[12].dstArrayElement = 0;
        descriptorWrites[12].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[12].descriptorCount = 1;
        descriptorWrites[12].pImageInfo = &oceanNormalInfo;

        // FFT Ocean foam (binding 13)
        descriptorWrites[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[13].dstSet = descriptorSets[i];
        descriptorWrites[13].dstBinding = 13;
        descriptorWrites[13].dstArrayElement = 0;
        descriptorWrites[13].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[13].descriptorCount = 1;
        descriptorWrites[13].pImageInfo = &oceanFoamInfo;

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }

    SDL_Log("Water descriptor sets created with terrain heightmap, flow map, displacement map, foam texture, temporal foam, caustics, SSR, and scene depth");
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

    // Set up push constants
    pushConstants.model = glm::mat4(1.0f);
    pushConstants.model = glm::translate(pushConstants.model, glm::vec3(
        waterUniforms.waterExtent.x,
        waterUniforms.waterLevel,
        waterUniforms.waterExtent.y
    ));
    // useFFTOcean and oceanSize values are set via setUseFFTOcean()
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(PushConstants), &pushConstants);

    // Bind water mesh and draw
    VkBuffer vertexBuffers[] = {waterMesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, waterMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, waterMesh.getIndexCount(), 1, 0, 0, 0);
}

void WaterSystem::recordMeshDraw(VkCommandBuffer cmd) {
    // Draw just the mesh (pipeline and descriptors bound externally)
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

// Phase 12: Material blending implementation

WaterSystem::WaterMaterial WaterSystem::getMaterialPreset(WaterType type) const {
    WaterMaterial material{};

    switch (type) {
        case WaterType::Ocean:
            material.waterColor = glm::vec4(0.01f, 0.03f, 0.08f, 0.95f);
            material.scatteringCoeffs = glm::vec4(0.45f, 0.09f, 0.02f, 0.05f);
            material.absorptionScale = 0.12f;
            material.scatteringScale = 0.8f;
            material.specularRoughness = 0.04f;
            material.sssIntensity = 1.2f;
            break;

        case WaterType::CoastalOcean:
            material.waterColor = glm::vec4(0.02f, 0.06f, 0.10f, 0.92f);
            material.scatteringCoeffs = glm::vec4(0.35f, 0.12f, 0.05f, 0.15f);
            material.absorptionScale = 0.18f;
            material.scatteringScale = 1.2f;
            material.specularRoughness = 0.05f;
            material.sssIntensity = 1.4f;
            break;

        case WaterType::River:
            material.waterColor = glm::vec4(0.04f, 0.08f, 0.06f, 0.90f);
            material.scatteringCoeffs = glm::vec4(0.25f, 0.18f, 0.12f, 0.25f);
            material.absorptionScale = 0.25f;
            material.scatteringScale = 1.5f;
            material.specularRoughness = 0.06f;
            material.sssIntensity = 1.0f;
            break;

        case WaterType::MuddyRiver:
            material.waterColor = glm::vec4(0.12f, 0.10f, 0.06f, 0.85f);
            material.scatteringCoeffs = glm::vec4(0.15f, 0.20f, 0.25f, 0.6f);
            material.absorptionScale = 0.4f;
            material.scatteringScale = 2.5f;
            material.specularRoughness = 0.08f;
            material.sssIntensity = 0.5f;
            break;

        case WaterType::ClearStream:
            material.waterColor = glm::vec4(0.01f, 0.04f, 0.08f, 0.98f);
            material.scatteringCoeffs = glm::vec4(0.50f, 0.08f, 0.01f, 0.02f);
            material.absorptionScale = 0.08f;
            material.scatteringScale = 0.5f;
            material.specularRoughness = 0.03f;
            material.sssIntensity = 2.0f;
            break;

        case WaterType::Lake:
            material.waterColor = glm::vec4(0.02f, 0.05f, 0.08f, 0.93f);
            material.scatteringCoeffs = glm::vec4(0.35f, 0.15f, 0.08f, 0.12f);
            material.absorptionScale = 0.20f;
            material.scatteringScale = 1.0f;
            material.specularRoughness = 0.04f;
            material.sssIntensity = 1.3f;
            break;

        case WaterType::Swamp:
            material.waterColor = glm::vec4(0.08f, 0.10f, 0.04f, 0.80f);
            material.scatteringCoeffs = glm::vec4(0.10f, 0.15f, 0.20f, 0.8f);
            material.absorptionScale = 0.5f;
            material.scatteringScale = 3.0f;
            material.specularRoughness = 0.10f;
            material.sssIntensity = 0.3f;
            break;

        case WaterType::Tropical:
            material.waterColor = glm::vec4(0.0f, 0.08f, 0.12f, 0.97f);
            material.scatteringCoeffs = glm::vec4(0.55f, 0.06f, 0.03f, 0.03f);
            material.absorptionScale = 0.06f;
            material.scatteringScale = 0.4f;
            material.specularRoughness = 0.03f;
            material.sssIntensity = 2.5f;
            break;
    }

    return material;
}

void WaterSystem::setPrimaryMaterial(const WaterMaterial& material) {
    waterUniforms.waterColor = material.waterColor;
    waterUniforms.scatteringCoeffs = material.scatteringCoeffs;
    waterUniforms.absorptionScale = material.absorptionScale;
    waterUniforms.scatteringScale = material.scatteringScale;
    waterUniforms.specularRoughness = material.specularRoughness;
    waterUniforms.sssIntensity = material.sssIntensity;
}

void WaterSystem::setSecondaryMaterial(const WaterMaterial& material) {
    waterUniforms.waterColor2 = material.waterColor;
    waterUniforms.scatteringCoeffs2 = material.scatteringCoeffs;
    waterUniforms.absorptionScale2 = material.absorptionScale;
    waterUniforms.scatteringScale2 = material.scatteringScale;
    waterUniforms.specularRoughness2 = material.specularRoughness;
    waterUniforms.sssIntensity2 = material.sssIntensity;
}

void WaterSystem::setPrimaryMaterial(WaterType type) {
    setPrimaryMaterial(getMaterialPreset(type));
    SDL_Log("Primary water material set to type %d", static_cast<int>(type));
}

void WaterSystem::setSecondaryMaterial(WaterType type) {
    setSecondaryMaterial(getMaterialPreset(type));
    SDL_Log("Secondary water material set to type %d", static_cast<int>(type));
}

void WaterSystem::setupMaterialTransition(WaterType from, WaterType to, const glm::vec2& center,
                                           float distance, BlendMode mode) {
    setPrimaryMaterial(from);
    setSecondaryMaterial(to);
    setBlendCenter(center);
    setBlendDistance(distance);
    setBlendMode(mode);

    SDL_Log("Material transition set up: type %d -> %d at (%.1f, %.1f), distance %.1fm, mode %d",
            static_cast<int>(from), static_cast<int>(to),
            center.x, center.y, distance, static_cast<int>(mode));
}
