#include "GraphicsPipelineFactory.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>

GraphicsPipelineFactory::GraphicsPipelineFactory(VkDevice device) : device(device) {
    initDefaultColorBlendAttachment();
}

GraphicsPipelineFactory::~GraphicsPipelineFactory() {
    cleanup();
}

GraphicsPipelineFactory& GraphicsPipelineFactory::reset() {
    cleanup();

    vertShaderPath.clear();
    fragShaderPath.clear();
    renderPass = VK_NULL_HANDLE;
    subpass = 0;
    pipelineLayout = VK_NULL_HANDLE;
    extent = {0, 0};
    dynamicViewport = false;
    vertexBindings.clear();
    vertexAttributes.clear();
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    cullMode = VK_CULL_MODE_BACK_BIT;
    frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    polygonMode = VK_POLYGON_MODE_FILL;
    lineWidth = 1.0f;
    depthClampEnable = false;
    depthBiasEnable = false;
    depthBiasConstant = 0.0f;
    depthBiasSlope = 0.0f;
    sampleCount = VK_SAMPLE_COUNT_1_BIT;
    depthTestEnable = true;
    depthWriteEnable = true;
    depthCompareOp = VK_COMPARE_OP_LESS;
    depthBoundsTestEnable = false;
    minDepthBounds = 0.0f;
    maxDepthBounds = 1.0f;
    stencilTestEnable = false;
    hasColorAttachments = true;
    colorAttachmentCount = 1;
    initDefaultColorBlendAttachment();

    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::applyPreset(Preset preset) {
    switch (preset) {
        case Preset::Default:
            // Standard 3D rendering - use current defaults
            depthTestEnable = true;
            depthWriteEnable = true;
            cullMode = VK_CULL_MODE_BACK_BIT;
            break;

        case Preset::FullscreenQuad:
            // Post-processing / fullscreen effects
            vertexBindings.clear();
            vertexAttributes.clear();
            depthTestEnable = false;
            depthWriteEnable = false;
            cullMode = VK_CULL_MODE_NONE;
            setBlendMode(BlendMode::None);
            break;

        case Preset::Shadow:
            // Depth-only shadow rendering
            depthTestEnable = true;
            depthWriteEnable = true;
            depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            cullMode = VK_CULL_MODE_BACK_BIT;
            depthBiasEnable = true;
            depthBiasConstant = 1.25f;
            depthBiasSlope = 1.75f;
            hasColorAttachments = false;
            break;

        case Preset::Particle:
            // Particle rendering with alpha blending
            depthTestEnable = true;
            depthWriteEnable = false;
            cullMode = VK_CULL_MODE_NONE;
            setBlendMode(BlendMode::Alpha);
            break;
    }
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setShaders(const std::string& vertPath, const std::string& fragPath) {
    vertShaderPath = vertPath;
    fragShaderPath = fragPath;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setVertexShader(const std::string& path) {
    vertShaderPath = path;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setFragmentShader(const std::string& path) {
    fragShaderPath = path;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setRenderPass(VkRenderPass pass, uint32_t sub) {
    renderPass = pass;
    subpass = sub;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setPipelineLayout(VkPipelineLayout layout) {
    pipelineLayout = layout;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setExtent(VkExtent2D ext) {
    extent = ext;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDynamicViewport(bool dynamic) {
    dynamicViewport = dynamic;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setVertexInput(
    const std::vector<VkVertexInputBindingDescription>& bindings,
    const std::vector<VkVertexInputAttributeDescription>& attributes) {
    vertexBindings = bindings;
    vertexAttributes = attributes;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setNoVertexInput() {
    vertexBindings.clear();
    vertexAttributes.clear();
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setTopology(VkPrimitiveTopology topo) {
    topology = topo;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setCullMode(VkCullModeFlags mode) {
    cullMode = mode;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setFrontFace(VkFrontFace face) {
    frontFace = face;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setPolygonMode(VkPolygonMode mode) {
    polygonMode = mode;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthBias(float constantFactor, float slopeFactor) {
    depthBiasEnable = true;
    depthBiasConstant = constantFactor;
    depthBiasSlope = slopeFactor;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthClamp(bool enable) {
    depthClampEnable = enable;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setLineWidth(float width) {
    lineWidth = width;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setSampleCount(VkSampleCountFlagBits samples) {
    sampleCount = samples;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthTest(bool enable) {
    depthTestEnable = enable;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthWrite(bool enable) {
    depthWriteEnable = enable;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthCompareOp(VkCompareOp op) {
    depthCompareOp = op;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthBoundsTest(bool enable, float minBounds, float maxBounds) {
    depthBoundsTestEnable = enable;
    minDepthBounds = minBounds;
    maxDepthBounds = maxBounds;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setStencilTest(bool enable) {
    stencilTestEnable = enable;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setBlendMode(BlendMode mode) {
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    switch (mode) {
        case BlendMode::None:
            colorBlendAttachment.blendEnable = VK_FALSE;
            break;

        case BlendMode::Alpha:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;

        case BlendMode::Additive:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;

        case BlendMode::Premultiplied:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
    }
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setColorBlendAttachment(
    const VkPipelineColorBlendAttachmentState& attachment) {
    colorBlendAttachment = attachment;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setColorWriteMask(VkColorComponentFlags mask) {
    colorBlendAttachment.colorWriteMask = mask;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setNoColorAttachments() {
    hasColorAttachments = false;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setColorAttachmentCount(uint32_t count) {
    colorAttachmentCount = count;
    hasColorAttachments = count > 0;
    return *this;
}

void GraphicsPipelineFactory::initDefaultColorBlendAttachment() {
    colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
}

bool GraphicsPipelineFactory::loadShaderModules(std::vector<VkPipelineShaderStageCreateInfo>& stages) {
    if (vertShaderPath.empty() || fragShaderPath.empty()) {
        SDL_Log("GraphicsPipelineFactory: Shader paths not set");
        return false;
    }

    auto vertCode = ShaderLoader::readFile(vertShaderPath);
    auto fragCode = ShaderLoader::readFile(fragShaderPath);

    if (vertCode.empty() || fragCode.empty()) {
        SDL_Log("GraphicsPipelineFactory: Failed to read shader files");
        return false;
    }

    VkShaderModule vertModule = ShaderLoader::createShaderModule(device, vertCode);
    VkShaderModule fragModule = ShaderLoader::createShaderModule(device, fragCode);

    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        SDL_Log("GraphicsPipelineFactory: Failed to create shader modules");
        if (vertModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    shaderModules.push_back(vertModule);
    shaderModules.push_back(fragModule);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    stages.push_back(vertStage);
    stages.push_back(fragStage);

    return true;
}

bool GraphicsPipelineFactory::build(VkPipeline& pipeline) {
    // Validate required state
    if (renderPass == VK_NULL_HANDLE) {
        SDL_Log("GraphicsPipelineFactory: Render pass not set");
        return false;
    }
    if (pipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("GraphicsPipelineFactory: Pipeline layout not set");
        return false;
    }
    if (!dynamicViewport && (extent.width == 0 || extent.height == 0)) {
        SDL_Log("GraphicsPipelineFactory: Extent not set and viewport is not dynamic");
        return false;
    }

    // Load shaders
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    if (!loadShaderModules(shaderStages)) {
        return false;
    }

    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexBindings.empty() ? nullptr : vertexBindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.empty() ? nullptr : vertexAttributes.data();

    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    if (dynamicViewport) {
        viewportState.pViewports = nullptr;
        viewportState.pScissors = nullptr;
    } else {
        viewportState.pViewports = &viewport;
        viewportState.pScissors = &scissor;
    }

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = depthClampEnable ? VK_TRUE : VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = polygonMode;
    rasterizer.lineWidth = lineWidth;
    rasterizer.cullMode = cullMode;
    rasterizer.frontFace = frontFace;
    rasterizer.depthBiasEnable = depthBiasEnable ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = depthBiasConstant;
    rasterizer.depthBiasSlopeFactor = depthBiasSlope;
    rasterizer.depthBiasClamp = 0.0f;

    // Multisampling state
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = sampleCount;

    // Depth/stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = depthCompareOp;
    depthStencil.depthBoundsTestEnable = depthBoundsTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.minDepthBounds = minDepthBounds;
    depthStencil.maxDepthBounds = maxDepthBounds;
    depthStencil.stencilTestEnable = stencilTestEnable ? VK_TRUE : VK_FALSE;

    // Color blend state
    // For MRT, we create an array of blend attachments (all using the same settings)
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorAttachmentCount, colorBlendAttachment);

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    if (hasColorAttachments) {
        colorBlending.attachmentCount = colorAttachmentCount;
        colorBlending.pAttachments = colorBlendAttachments.data();
    } else {
        colorBlending.attachmentCount = 0;
        colorBlending.pAttachments = nullptr;
    }

    // Dynamic states
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    if (dynamicViewport) {
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();
    } else {
        dynamicState.dynamicStateCount = 0;
        dynamicState.pDynamicStates = nullptr;
    }

    // Create the pipeline
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
    pipelineInfo.pDynamicState = dynamicViewport ? &dynamicState : nullptr;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = subpass;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    cleanup();

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GraphicsPipelineFactory: Failed to create graphics pipeline (VkResult=%d) "
            "vert='%s' frag='%s'",
            static_cast<int>(result),
            vertShaderPath.c_str(),
            fragShaderPath.empty() ? "<none>" : fragShaderPath.c_str());
        return false;
    }

    return true;
}

void GraphicsPipelineFactory::cleanup() {
    for (VkShaderModule module : shaderModules) {
        vkDestroyShaderModule(device, module, nullptr);
    }
    shaderModules.clear();
}
