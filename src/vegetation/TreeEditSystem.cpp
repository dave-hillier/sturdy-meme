#include "TreeEditSystem.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cstring>

using ShaderLoader::loadShaderModule;

bool TreeEditSystem::init(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    renderPass = info.renderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;

    // Derive asset path from shader path (go up one level from shaders/)
    size_t lastSlash = shaderPath.rfind('/');
    if (lastSlash != std::string::npos) {
        assetPath = shaderPath.substr(0, lastSlash) + "/assets";
    } else {
        assetPath = "assets";
    }

    // Load bark and leaf textures
    if (!loadTextures()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load tree textures, using fallback colors");
    }

    // Create descriptor set layout and pipelines
    if (!createDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;
    if (!createPipelines()) return false;

    // Generate initial tree
    regenerateTree();

    SDL_Log("Tree edit system initialized");
    return true;
}

void TreeEditSystem::destroy(VkDevice device, VmaAllocator allocator) {
    // Destroy meshes
    branchMesh.destroy(allocator);
    leafMesh.destroy(allocator);
    meshesUploaded = false;

    // Destroy textures
    for (int i = 0; i < NUM_BARK_TYPES; ++i) {
        barkColorTextures[i].destroy(allocator, device);
        barkNormalTextures[i].destroy(allocator, device);
        barkAOTextures[i].destroy(allocator, device);
        barkRoughnessTextures[i].destroy(allocator, device);
    }
    for (int i = 0; i < NUM_LEAF_TYPES; ++i) {
        leafTextures[i].destroy(allocator, device);
    }
    fallbackTexture.reset();
    fallbackNormalTexture.reset();
    texturesLoaded = false;

    // Destroy pipelines
    if (solidPipeline) vkDestroyPipeline(device, solidPipeline, nullptr);
    if (wireframePipeline) vkDestroyPipeline(device, wireframePipeline, nullptr);
    if (leafPipeline) vkDestroyPipeline(device, leafPipeline, nullptr);

    // Destroy pipeline layout
    if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    // Destroy descriptor set layout
    if (descriptorSetLayout) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
}

bool TreeEditSystem::createDescriptorSetLayout() {
    // Use DescriptorManager::LayoutBuilder for consistent descriptor set layout creation
    descriptorSetLayout = DescriptorManager::LayoutBuilder(device)
        .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: Scene UBO
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 1: Bark color texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 2: Bark normal texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 3: Bark AO texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 4: Bark roughness texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 5: Leaf texture
        .build();

    if (descriptorSetLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree descriptor set layout");
        return false;
    }

    return true;
}

bool TreeEditSystem::createDescriptorSets() {
    if (!descriptorPool) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No descriptor pool provided for tree edit system");
        return false;
    }

    descriptorSets = descriptorPool->allocate(descriptorSetLayout, framesInFlight);
    if (descriptorSets.size() != framesInFlight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate tree descriptor sets");
        return false;
    }

    return true;
}

void TreeEditSystem::updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& sceneUniformBuffers) {
    // Get textures for current bark/leaf types
    int barkIdx = static_cast<int>(treeParams.barkType);
    int leafIdx = static_cast<int>(treeParams.leafType);

    // Fallback to index 0 if out of range
    if (barkIdx < 0 || barkIdx >= NUM_BARK_TYPES) barkIdx = 0;
    if (leafIdx < 0 || leafIdx >= NUM_LEAF_TYPES) leafIdx = 0;

    // Helper to get texture view/sampler with fallback
    auto getBarkColorView = [&]() -> VkImageView {
        return (texturesLoaded && barkColorTextures[barkIdx].getImageView())
            ? barkColorTextures[barkIdx].getImageView() : (*fallbackTexture)->getImageView();
    };
    auto getBarkColorSampler = [&]() -> VkSampler {
        return (texturesLoaded && barkColorTextures[barkIdx].getImageView())
            ? barkColorTextures[barkIdx].getSampler() : (*fallbackTexture)->getSampler();
    };
    auto getBarkNormalView = [&]() -> VkImageView {
        return (texturesLoaded && barkNormalTextures[barkIdx].getImageView())
            ? barkNormalTextures[barkIdx].getImageView() : (*fallbackNormalTexture)->getImageView();
    };
    auto getBarkNormalSampler = [&]() -> VkSampler {
        return (texturesLoaded && barkNormalTextures[barkIdx].getImageView())
            ? barkNormalTextures[barkIdx].getSampler() : (*fallbackNormalTexture)->getSampler();
    };
    auto getBarkAOView = [&]() -> VkImageView {
        return (texturesLoaded && barkAOTextures[barkIdx].getImageView())
            ? barkAOTextures[barkIdx].getImageView() : (*fallbackTexture)->getImageView();
    };
    auto getBarkAOSampler = [&]() -> VkSampler {
        return (texturesLoaded && barkAOTextures[barkIdx].getImageView())
            ? barkAOTextures[barkIdx].getSampler() : (*fallbackTexture)->getSampler();
    };
    auto getBarkRoughnessView = [&]() -> VkImageView {
        return (texturesLoaded && barkRoughnessTextures[barkIdx].getImageView())
            ? barkRoughnessTextures[barkIdx].getImageView() : (*fallbackTexture)->getImageView();
    };
    auto getBarkRoughnessSampler = [&]() -> VkSampler {
        return (texturesLoaded && barkRoughnessTextures[barkIdx].getImageView())
            ? barkRoughnessTextures[barkIdx].getSampler() : (*fallbackTexture)->getSampler();
    };
    auto getLeafView = [&]() -> VkImageView {
        return (texturesLoaded && leafTextures[leafIdx].getImageView())
            ? leafTextures[leafIdx].getImageView() : (*fallbackTexture)->getImageView();
    };
    auto getLeafSampler = [&]() -> VkSampler {
        return (texturesLoaded && leafTextures[leafIdx].getImageView())
            ? leafTextures[leafIdx].getSampler() : (*fallbackTexture)->getSampler();
    };

    // Use DescriptorManager::SetWriter for consistent descriptor set updates
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        DescriptorManager::SetWriter(device, descriptorSets[i])
            .writeBuffer(0, sceneUniformBuffers[i], 0, sizeof(UniformBufferObject))
            .writeImage(1, getBarkColorView(), getBarkColorSampler())
            .writeImage(2, getBarkNormalView(), getBarkNormalSampler())
            .writeImage(3, getBarkAOView(), getBarkAOSampler())
            .writeImage(4, getBarkRoughnessView(), getBarkRoughnessSampler())
            .writeImage(5, getLeafView(), getLeafSampler())
            .update();
    }

    // Track current types
    currentBarkType = treeParams.barkType;
    currentLeafType = treeParams.leafType;
}

void TreeEditSystem::updateTextureBindings() {
    // Update descriptor sets if bark or leaf type changed
    if (currentBarkType != treeParams.barkType || currentLeafType != treeParams.leafType) {
        // We need to get the UBO buffers from the renderer, but for now we'll trigger
        // a full regeneration which will update the descriptors
        SDL_Log("Texture type changed, descriptors will be updated on next frame");
    }
}

bool TreeEditSystem::createPipelines() {
    // Load shaders
    VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/tree.vert.spv");
    VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/tree.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load tree shaders");
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // Shader stages
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // Vertex input - use Vertex format from Mesh
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport state
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer - solid fill
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constants for model matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(TreePushConstants);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree pipeline layout");
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // Create solid pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &solidPipeline) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree solid pipeline");
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // Create wireframe pipeline
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &wireframePipeline) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree wireframe pipeline");
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    // Create leaf pipeline (no backface culling for double-sided leaves)
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    // Enable alpha blending for leaves
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &leafPipeline) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create tree leaf pipeline");
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    return true;
}

void TreeEditSystem::regenerateTree() {
    // Wait for GPU to finish any in-flight work before destroying buffers
    if (meshesUploaded) {
        vkDeviceWaitIdle(device);
        branchMesh.destroy(allocator);
        leafMesh.destroy(allocator);
        meshesUploaded = false;
    }

    // Generate new tree geometry
    generator.generate(treeParams);

    // Build meshes
    generator.buildMesh(branchMesh);
    generator.buildLeafMesh(leafMesh, treeParams);

    // Upload to GPU
    uploadTreeMesh();
}

void TreeEditSystem::uploadTreeMesh() {
    if (generator.getBranchVertices().empty()) return;

    branchMesh.upload(allocator, device, commandPool, graphicsQueue);

    if (!generator.getLeafInstances().empty()) {
        leafMesh.upload(allocator, device, commandPool, graphicsQueue);
    }

    meshesUploaded = true;
    SDL_Log("Tree mesh uploaded: %u branch vertices, %zu leaf instances",
            branchMesh.getIndexCount(), generator.getLeafInstances().size());
}

void TreeEditSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!enabled || !meshesUploaded) return;
    if (branchMesh.getIndexCount() == 0) return;

    // Additional safety check: ensure vertex buffer is valid
    // This handles race conditions where buffers may be destroyed during frame recording
    if (branchMesh.getVertexBuffer() == VK_NULL_HANDLE ||
        branchMesh.getIndexBuffer() == VK_NULL_HANDLE) {
        return;
    }

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Push constants - model matrix
    TreePushConstants pc{};
    pc.model = glm::translate(glm::mat4(1.0f), position) *
               glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    pc.roughness = 0.8f;  // Bark is rough
    pc.metallic = 0.0f;   // Wood is not metallic
    pc.alphaTest = 0.0f;  // No alpha test for bark
    pc.isLeaf = 0;        // Rendering bark

    // Bind descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 1, &descriptorSets[frameIndex], 0, nullptr);

    // Draw branches
    VkPipeline branchPipeline = wireframeMode ? wireframePipeline : solidPipeline;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, branchPipeline);
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(TreePushConstants), &pc);

    VkBuffer vertexBuffers[] = {branchMesh.getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, branchMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, branchMesh.getIndexCount(), 1, 0, 0, 0);

    // Draw leaves
    if (showLeaves && leafMesh.getIndexCount() > 0 && !wireframeMode &&
        leafMesh.getVertexBuffer() != VK_NULL_HANDLE &&
        leafMesh.getIndexBuffer() != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, leafPipeline);

        // Adjust push constants for leaves
        pc.roughness = 0.6f;           // Leaves are somewhat rough
        pc.alphaTest = treeParams.leafAlphaTest;  // Alpha discard threshold
        pc.isLeaf = 1;                 // Rendering leaves
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(TreePushConstants), &pc);

        VkBuffer leafBuffers[] = {leafMesh.getVertexBuffer()};
        vkCmdBindVertexBuffers(cmd, 0, 1, leafBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, leafMesh.getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, leafMesh.getIndexCount(), 1, 0, 0, 0);
    }
}

glm::vec3 TreeEditSystem::getTreeCenter() const {
    return position + glm::vec3(0.0f, treeParams.trunkHeight * 0.5f * scale, 0.0f);
}

bool TreeEditSystem::loadTextures() {
    // Create fallback texture (gray color for bark, green for leaves)
    fallbackTexture = RAIIAdapter<Texture>::create(
        [this](auto& t) {
            if (!t.createSolidColor(128, 128, 128, 255, allocator, device, commandPool, graphicsQueue)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create fallback texture");
                return false;
            }
            return true;
        },
        [this](auto& t) { t.destroy(allocator, device); }
    );
    if (!fallbackTexture) return false;

    // Create fallback normal map texture (flat normal pointing up in tangent space)
    // RGB(128, 128, 255) = normalized (0, 0, 1) after conversion from [0,1] to [-1,1]
    fallbackNormalTexture = RAIIAdapter<Texture>::create(
        [this](auto& t) {
            if (!t.createSolidColor(128, 128, 255, 255, allocator, device, commandPool, graphicsQueue)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create fallback normal texture");
                return false;
            }
            return true;
        },
        [this](auto& t) { t.destroy(allocator, device); }
    );
    if (!fallbackNormalTexture) return false;

    // Bark type names (order matches BarkType enum)
    const char* barkNames[NUM_BARK_TYPES] = { "oak", "birch", "pine", "willow" };

    // Load bark textures for each type
    for (int i = 0; i < NUM_BARK_TYPES; ++i) {
        std::string colorPath = assetPath + "/textures/bark/" + barkNames[i] + "_color_1k.jpg";
        std::string normalPath = assetPath + "/textures/bark/" + barkNames[i] + "_normal_1k.jpg";
        std::string aoPath = assetPath + "/textures/bark/" + barkNames[i] + "_ao_1k.jpg";
        std::string roughnessPath = assetPath + "/textures/bark/" + barkNames[i] + "_roughness_1k.jpg";

        if (!barkColorTextures[i].load(colorPath, allocator, device, commandPool, graphicsQueue, physicalDevice, true)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load bark color texture: %s", colorPath.c_str());
        }
        if (!barkNormalTextures[i].load(normalPath, allocator, device, commandPool, graphicsQueue, physicalDevice, false)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load bark normal texture: %s", normalPath.c_str());
        }
        if (!barkAOTextures[i].load(aoPath, allocator, device, commandPool, graphicsQueue, physicalDevice, false)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load bark AO texture: %s", aoPath.c_str());
        }
        if (!barkRoughnessTextures[i].load(roughnessPath, allocator, device, commandPool, graphicsQueue, physicalDevice, false)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load bark roughness texture: %s", roughnessPath.c_str());
        }
    }

    // Leaf type names (order matches LeafType enum: Oak, Ash, Aspen, Pine)
    const char* leafNames[NUM_LEAF_TYPES] = { "oak", "ash", "aspen", "pine" };

    // Load leaf textures
    for (int i = 0; i < NUM_LEAF_TYPES; ++i) {
        std::string leafPath = assetPath + "/textures/leaves/" + leafNames[i] + "_color.png";

        if (!leafTextures[i].load(leafPath, allocator, device, commandPool, graphicsQueue, physicalDevice, true)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to load leaf texture: %s", leafPath.c_str());
        }
    }

    texturesLoaded = true;
    SDL_Log("Tree textures loaded: %d bark types, %d leaf types", NUM_BARK_TYPES, NUM_LEAF_TYPES);
    return true;
}

const Texture& TreeEditSystem::getBarkColorTexture() const {
    int idx = static_cast<int>(treeParams.barkType);
    if (idx >= 0 && idx < NUM_BARK_TYPES && barkColorTextures[idx].getImageView()) {
        return barkColorTextures[idx];
    }
    return **fallbackTexture;
}

const Texture& TreeEditSystem::getBarkNormalTexture() const {
    int idx = static_cast<int>(treeParams.barkType);
    if (idx >= 0 && idx < NUM_BARK_TYPES && barkNormalTextures[idx].getImageView()) {
        return barkNormalTextures[idx];
    }
    return **fallbackTexture;
}

const Texture& TreeEditSystem::getBarkAOTexture() const {
    int idx = static_cast<int>(treeParams.barkType);
    if (idx >= 0 && idx < NUM_BARK_TYPES && barkAOTextures[idx].getImageView()) {
        return barkAOTextures[idx];
    }
    return **fallbackTexture;
}

const Texture& TreeEditSystem::getBarkRoughnessTexture() const {
    int idx = static_cast<int>(treeParams.barkType);
    if (idx >= 0 && idx < NUM_BARK_TYPES && barkRoughnessTextures[idx].getImageView()) {
        return barkRoughnessTextures[idx];
    }
    return **fallbackTexture;
}

const Texture& TreeEditSystem::getLeafTexture() const {
    int idx = static_cast<int>(treeParams.leafType);
    if (idx >= 0 && idx < NUM_LEAF_TYPES && leafTextures[idx].getImageView()) {
        return leafTextures[idx];
    }
    return **fallbackTexture;
}
