#include "CatmullClarkSystem.h"
#include "OBJLoader.h"
#include "ShaderLoader.h"
#include "BufferUtils.h"
#include <SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cstring>

using ShaderLoader::loadShaderModule;

bool CatmullClarkSystem::init(const InitInfo& info, const CatmullClarkConfig& cfg) {
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
    config = cfg;

    // Create base mesh
    if (!config.objPath.empty()) {
        mesh = OBJLoader::loadQuadMesh(config.objPath);
        if (mesh.vertices.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load OBJ, falling back to cube");
            mesh = CatmullClarkMesh::createCube();
        }
    } else {
        mesh = CatmullClarkMesh::createCube();
    }
    if (!mesh.uploadToGPU(allocator)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload Catmull-Clark mesh to GPU");
        return false;
    }

    // Initialize CBT
    CatmullClarkCBT::InitInfo cbtInfo{};
    cbtInfo.allocator = allocator;
    cbtInfo.maxDepth = config.maxDepth;
    cbtInfo.faceCount = static_cast<int>(mesh.faces.size());

    if (!cbt.init(cbtInfo)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize Catmull-Clark CBT");
        return false;
    }

    // Create buffers and pipelines
    if (!createUniformBuffers()) return false;
    if (!createIndirectBuffers()) return false;
    if (!createComputeDescriptorSetLayout()) return false;
    if (!createRenderDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;
    if (!createSubdivisionPipeline()) return false;
    if (!createRenderPipeline()) return false;
    if (!createWireframePipeline()) return false;

    SDL_Log("Catmull-Clark subdivision system initialized");
    return true;
}

void CatmullClarkSystem::destroy(VkDevice device, VmaAllocator allocator) {
    // Destroy mesh buffers
    mesh.destroy(allocator);

    // Destroy CBT
    cbt.destroy(allocator);

    // Destroy indirect buffers
    if (indirectDispatchBuffer) {
        vmaDestroyBuffer(allocator, indirectDispatchBuffer, indirectDispatchAllocation);
    }
    if (indirectDrawBuffer) {
        vmaDestroyBuffer(allocator, indirectDrawBuffer, indirectDrawAllocation);
    }

    // Destroy uniform buffers
    for (size_t i = 0; i < uniformBuffers.size(); ++i) {
        if (uniformBuffers[i]) {
            vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
        }
    }
    uniformBuffers.clear();
    uniformAllocations.clear();
    uniformMappedPtrs.clear();

    // Destroy pipelines
    if (subdivisionPipeline) vkDestroyPipeline(device, subdivisionPipeline, nullptr);
    if (renderPipeline) vkDestroyPipeline(device, renderPipeline, nullptr);
    if (wireframePipeline) vkDestroyPipeline(device, wireframePipeline, nullptr);

    // Destroy pipeline layouts
    if (subdivisionPipelineLayout) vkDestroyPipelineLayout(device, subdivisionPipelineLayout, nullptr);
    if (renderPipelineLayout) vkDestroyPipelineLayout(device, renderPipelineLayout, nullptr);

    // Destroy descriptor set layouts
    if (computeDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
    if (renderDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, renderDescriptorSetLayout, nullptr);
}

bool CatmullClarkSystem::createUniformBuffers() {
    uniformBuffers.resize(framesInFlight);
    uniformAllocations.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; ++i) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(UniformBufferObject);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo{};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &uniformBuffers[i],
                           &uniformAllocations[i], &allocationInfo) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create Catmull-Clark uniform buffer %u", i);
            return false;
        }
        uniformMappedPtrs[i] = allocationInfo.pMappedData;
    }

    return true;
}

bool CatmullClarkSystem::createIndirectBuffers() {
    // Indirect dispatch buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(VkDispatchIndirectCommand);
        bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &indirectDispatchBuffer,
                            &indirectDispatchAllocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect dispatch buffer");
            return false;
        }
    }

    // Indirect draw buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(VkDrawIndirectCommand);
        bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &indirectDrawBuffer,
                            &indirectDrawAllocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect draw buffer");
            return false;
        }
    }

    return true;
}

bool CatmullClarkSystem::createComputeDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

    // Binding 0: Scene UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: CBT buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Mesh vertices
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Mesh halfedges
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: Mesh faces
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create compute descriptor set layout");
        return false;
    }

    return true;
}

bool CatmullClarkSystem::createRenderDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

    // Binding 0: Scene UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 1: CBT buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 2: Mesh vertices
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 3: Mesh halfedges
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 4: Mesh faces
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &renderDescriptorSetLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create render descriptor set layout");
        return false;
    }

    return true;
}

bool CatmullClarkSystem::createDescriptorSets() {
    // Allocate compute descriptor sets
    {
        std::vector<VkDescriptorSetLayout> layouts(framesInFlight, computeDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = framesInFlight;
        allocInfo.pSetLayouts = layouts.data();

        computeDescriptorSets.resize(framesInFlight);
        if (vkAllocateDescriptorSets(device, &allocInfo, computeDescriptorSets.data()) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate compute descriptor sets");
            return false;
        }
    }

    // Allocate render descriptor sets
    {
        std::vector<VkDescriptorSetLayout> layouts(framesInFlight, renderDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = framesInFlight;
        allocInfo.pSetLayouts = layouts.data();

        renderDescriptorSets.resize(framesInFlight);
        if (vkAllocateDescriptorSets(device, &allocInfo, renderDescriptorSets.data()) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate render descriptor sets");
            return false;
        }
    }

    return true;
}

void CatmullClarkSystem::updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& sceneUniformBuffers) {
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        std::vector<VkWriteDescriptorSet> descriptorWrites;

        // Compute descriptor set
        {
            VkDescriptorBufferInfo sceneBufferInfo{};
            sceneBufferInfo.buffer = sceneUniformBuffers[i];
            sceneBufferInfo.offset = 0;
            sceneBufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorBufferInfo cbtBufferInfo{};
            cbtBufferInfo.buffer = cbt.getBuffer();
            cbtBufferInfo.offset = 0;
            cbtBufferInfo.range = cbt.getBufferSize();

            VkDescriptorBufferInfo vertexBufferInfo{};
            vertexBufferInfo.buffer = mesh.vertexBuffer;
            vertexBufferInfo.offset = 0;
            vertexBufferInfo.range = mesh.vertices.size() * sizeof(CatmullClarkMesh::Vertex);

            VkDescriptorBufferInfo halfedgeBufferInfo{};
            halfedgeBufferInfo.buffer = mesh.halfedgeBuffer;
            halfedgeBufferInfo.offset = 0;
            halfedgeBufferInfo.range = mesh.halfedges.size() * sizeof(CatmullClarkMesh::Halfedge);

            VkDescriptorBufferInfo faceBufferInfo{};
            faceBufferInfo.buffer = mesh.faceBuffer;
            faceBufferInfo.offset = 0;
            faceBufferInfo.range = mesh.faces.size() * sizeof(CatmullClarkMesh::Face);

            VkWriteDescriptorSet write0{};
            write0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write0.dstSet = computeDescriptorSets[i];
            write0.dstBinding = 0;
            write0.dstArrayElement = 0;
            write0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write0.descriptorCount = 1;
            write0.pBufferInfo = &sceneBufferInfo;

            VkWriteDescriptorSet write1{};
            write1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write1.dstSet = computeDescriptorSets[i];
            write1.dstBinding = 1;
            write1.dstArrayElement = 0;
            write1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write1.descriptorCount = 1;
            write1.pBufferInfo = &cbtBufferInfo;

            VkWriteDescriptorSet write2{};
            write2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write2.dstSet = computeDescriptorSets[i];
            write2.dstBinding = 2;
            write2.dstArrayElement = 0;
            write2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write2.descriptorCount = 1;
            write2.pBufferInfo = &vertexBufferInfo;

            VkWriteDescriptorSet write3{};
            write3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write3.dstSet = computeDescriptorSets[i];
            write3.dstBinding = 3;
            write3.dstArrayElement = 0;
            write3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write3.descriptorCount = 1;
            write3.pBufferInfo = &halfedgeBufferInfo;

            VkWriteDescriptorSet write4{};
            write4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write4.dstSet = computeDescriptorSets[i];
            write4.dstBinding = 4;
            write4.dstArrayElement = 0;
            write4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write4.descriptorCount = 1;
            write4.pBufferInfo = &faceBufferInfo;

            vkUpdateDescriptorSets(device, 5, (VkWriteDescriptorSet[]){write0, write1, write2, write3, write4}, 0, nullptr);
        }

        // Render descriptor set (same buffers)
        {
            VkDescriptorBufferInfo sceneBufferInfo{};
            sceneBufferInfo.buffer = sceneUniformBuffers[i];
            sceneBufferInfo.offset = 0;
            sceneBufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorBufferInfo cbtBufferInfo{};
            cbtBufferInfo.buffer = cbt.getBuffer();
            cbtBufferInfo.offset = 0;
            cbtBufferInfo.range = cbt.getBufferSize();

            VkDescriptorBufferInfo vertexBufferInfo{};
            vertexBufferInfo.buffer = mesh.vertexBuffer;
            vertexBufferInfo.offset = 0;
            vertexBufferInfo.range = mesh.vertices.size() * sizeof(CatmullClarkMesh::Vertex);

            VkDescriptorBufferInfo halfedgeBufferInfo{};
            halfedgeBufferInfo.buffer = mesh.halfedgeBuffer;
            halfedgeBufferInfo.offset = 0;
            halfedgeBufferInfo.range = mesh.halfedges.size() * sizeof(CatmullClarkMesh::Halfedge);

            VkDescriptorBufferInfo faceBufferInfo{};
            faceBufferInfo.buffer = mesh.faceBuffer;
            faceBufferInfo.offset = 0;
            faceBufferInfo.range = mesh.faces.size() * sizeof(CatmullClarkMesh::Face);

            VkWriteDescriptorSet write0{};
            write0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write0.dstSet = renderDescriptorSets[i];
            write0.dstBinding = 0;
            write0.dstArrayElement = 0;
            write0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write0.descriptorCount = 1;
            write0.pBufferInfo = &sceneBufferInfo;

            VkWriteDescriptorSet write1{};
            write1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write1.dstSet = renderDescriptorSets[i];
            write1.dstBinding = 1;
            write1.dstArrayElement = 0;
            write1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write1.descriptorCount = 1;
            write1.pBufferInfo = &cbtBufferInfo;

            VkWriteDescriptorSet write2{};
            write2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write2.dstSet = renderDescriptorSets[i];
            write2.dstBinding = 2;
            write2.dstArrayElement = 0;
            write2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write2.descriptorCount = 1;
            write2.pBufferInfo = &vertexBufferInfo;

            VkWriteDescriptorSet write3{};
            write3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write3.dstSet = renderDescriptorSets[i];
            write3.dstBinding = 3;
            write3.dstArrayElement = 0;
            write3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write3.descriptorCount = 1;
            write3.pBufferInfo = &halfedgeBufferInfo;

            VkWriteDescriptorSet write4{};
            write4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write4.dstSet = renderDescriptorSets[i];
            write4.dstBinding = 4;
            write4.dstArrayElement = 0;
            write4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write4.descriptorCount = 1;
            write4.pBufferInfo = &faceBufferInfo;

            vkUpdateDescriptorSets(device, 5, (VkWriteDescriptorSet[]){write0, write1, write2, write3, write4}, 0, nullptr);
        }
    }
}

bool CatmullClarkSystem::createSubdivisionPipeline() {
    VkShaderModule shaderModule = loadShaderModule(device, shaderPath + "/catmullclark_subdivision.comp.spv");
    if (!shaderModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load Catmull-Clark subdivision compute shader");
        return false;
    }

    // Push constants for subdivision parameters
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(CatmullClarkSubdivisionPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &computeDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &subdivisionPipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create subdivision pipeline layout");
        vkDestroyShaderModule(device, shaderModule, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = subdivisionPipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &subdivisionPipeline);
    vkDestroyShaderModule(device, shaderModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create subdivision compute pipeline");
        return false;
    }

    return true;
}

bool CatmullClarkSystem::createRenderPipeline() {
    VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/catmullclark_render.vert.spv");
    VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/catmullclark_render.frag.spv");
    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load Catmull-Clark render shaders");
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // No vertex input - all vertex data comes from buffers bound as storage buffers
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Push constants for model matrix
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(CatmullClarkPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &renderDescriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &renderPipelineLayout) != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create render pipeline layout");
        vkDestroyShaderModule(device, vertModule, nullptr);
        vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

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
    pipelineInfo.layout = renderPipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &renderPipeline);

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create render graphics pipeline");
        return false;
    }

    return true;
}

bool CatmullClarkSystem::createWireframePipeline() {
    VkShaderModule vertModule = loadShaderModule(device, shaderPath + "/catmullclark_render.vert.spv");
    VkShaderModule fragModule = loadShaderModule(device, shaderPath + "/catmullclark_render.frag.spv");
    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load Catmull-Clark wireframe shaders");
        if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
        if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // No vertex input - all vertex data comes from buffers bound as storage buffers
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Wireframe rasterization
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_LINE;  // Wireframe mode
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // No culling for wireframe
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

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
    pipelineInfo.layout = renderPipelineLayout;  // Reuse render pipeline layout
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &wireframePipeline);

    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);

    if (result != VK_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create wireframe graphics pipeline");
        return false;
    }

    return true;
}

void CatmullClarkSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                         const glm::mat4& view, const glm::mat4& proj) {
    // The CatmullClarkSystem uses the shared scene UBO which is updated by the main renderer.
    // This method is provided for API consistency and future Catmull-Clark specific uniforms.
    // Currently no additional uniforms need updating here.
}

void CatmullClarkSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Bind the subdivision compute pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipeline);

    // Bind the compute descriptor set
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, subdivisionPipelineLayout,
                           0, 1, &computeDescriptorSets[frameIndex], 0, nullptr);

    // Push subdivision parameters
    CatmullClarkSubdivisionPushConstants pc{};
    pc.targetEdgePixels = config.targetEdgePixels;
    pc.splitThreshold = config.splitThreshold;
    pc.mergeThreshold = config.mergeThreshold;
    pc.padding = 0;
    vkCmdPushConstants(cmd, subdivisionPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                      0, sizeof(CatmullClarkSubdivisionPushConstants), &pc);

    // Dispatch compute shader - one workgroup per face at base level
    uint32_t workgroupCount = (cbt.getFaceCount() + SUBDIVISION_WORKGROUP_SIZE - 1) / SUBDIVISION_WORKGROUP_SIZE;
    vkCmdDispatch(cmd, std::max(1u, workgroupCount), 1, 1);

    // Memory barrier: compute shader writes -> graphics shader reads
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                        0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void CatmullClarkSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Select pipeline based on wireframe mode
    VkPipeline pipeline = wireframeMode ? wireframePipeline : renderPipeline;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Bind descriptor set for this frame
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipelineLayout,
                           0, 1, &renderDescriptorSets[frameIndex], 0, nullptr);

    // Set dynamic viewport
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    // Set dynamic scissor
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Push model matrix
    CatmullClarkPushConstants pc{};
    pc.model = glm::translate(glm::mat4(1.0f), config.position) *
               glm::scale(glm::mat4(1.0f), config.scale);
    vkCmdPushConstants(cmd, renderPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(CatmullClarkPushConstants), &pc);

    // Indirect draw - vertex count populated by subdivision compute shader
    vkCmdDrawIndirect(cmd, indirectDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}
