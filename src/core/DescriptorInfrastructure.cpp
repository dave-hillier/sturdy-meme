#include "DescriptorInfrastructure.h"
#include "VulkanContext.h"
#include "UBOs.h"
#include "Bindings.h"
#include "vulkan/PipelineLayoutBuilder.h"
#include "GraphicsPipelineFactory.h"
#include "Mesh.h"
#include <SDL3/SDL.h>

void DescriptorInfrastructure::addCommonDescriptorBindings(DescriptorManager::LayoutBuilder& builder) {
    // Main scene descriptor set layout - uses common bindings (0-11, 13-17)
    // This must match definitions in shaders/bindings.h
    builder
        .addUniformBuffer(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)  // 0: UBO
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 1: diffuse
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 2: shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 3: normal
        .addStorageBuffer(VK_SHADER_STAGE_FRAGMENT_BIT)         // 4: lights
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 5: emissive
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 6: point shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 7: spot shadow
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 8: snow mask
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 9: cloud shadow map
        .addUniformBuffer(VK_SHADER_STAGE_FRAGMENT_BIT)         // 10: Snow UBO
        .addUniformBuffer(VK_SHADER_STAGE_FRAGMENT_BIT)         // 11: Cloud shadow UBO
        // Note: binding 12 (bone matrices) is added separately for skinned layout
        .addBinding(Bindings::ROUGHNESS_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)  // 13: roughness
        .addBinding(Bindings::METALLIC_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)   // 14: metallic
        .addBinding(Bindings::AO_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)         // 15: AO
        .addBinding(Bindings::HEIGHT_MAP, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)     // 16: height
        .addBinding(Bindings::WIND_UBO, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);                // 17: wind UBO
}

bool DescriptorInfrastructure::initDescriptors(VulkanContext& context, const Config& config) {
    VkDevice device = context.getVkDevice();

    if (!createDescriptorSetLayout(device, context.getRaiiDevice())) {
        return false;
    }

    if (!createDescriptorPool(device, config)) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool DescriptorInfrastructure::createDescriptorSetLayout(VkDevice device, const vk::raii::Device& raiiDevice) {
    DescriptorManager::LayoutBuilder builder(device);
    addCommonDescriptorBindings(builder);
    VkDescriptorSetLayout rawLayout = builder.build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create descriptor set layout");
        return false;
    }

    descriptorSetLayout_.emplace(raiiDevice, rawLayout);
    return true;
}

bool DescriptorInfrastructure::createDescriptorPool(VkDevice device, const Config& config) {
    // Create the auto-growing descriptor pool with configurable sizes
    // Will automatically grow if exhausted
    descriptorManagerPool_.emplace(device, config.setsPerPool, config.poolSizes);
    return true;
}

bool DescriptorInfrastructure::createGraphicsPipeline(VulkanContext& context, VkRenderPass hdrRenderPass,
                                                       const std::string& resourcePath) {
    if (!initialized_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DescriptorInfrastructure::createGraphicsPipeline: not initialized");
        return false;
    }

    vk::Device device(context.getVkDevice());
    VkExtent2D swapchainExtent = context.getVkSwapchainExtent();

    // Create pipeline layout using PipelineLayoutBuilder
    // Set 0: Main rendering (UBO, textures, lights, etc.)
    // Set 1: Bindless texture array (optional, if bindless is available)
    // Set 2: Material data SSBO (optional, if bindless is available)
    auto builder = PipelineLayoutBuilder(context.getRaiiDevice())
        .addDescriptorSetLayout(**descriptorSetLayout_);

    if (bindlessTextureSetLayout_) {
        builder.addDescriptorSetLayout(bindlessTextureSetLayout_);
    }
    if (bindlessMaterialSetLayout_) {
        builder.addDescriptorSetLayout(bindlessMaterialSetLayout_);
    }

    auto layoutOpt = builder
        .addPushConstantRange<PushConstants>(
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .build();

    if (!layoutOpt) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create pipeline layout");
        return false;
    }
    pipelineLayout_ = std::move(layoutOpt);

    // Use factory for pipeline creation
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipeline rawPipeline = VK_NULL_HANDLE;
    GraphicsPipelineFactory factory(device);
    bool success = factory
        .applyPreset(GraphicsPipelineFactory::Preset::Default)
        .setShaders(resourcePath + "/shaders/shader.vert.spv",
                    resourcePath + "/shaders/shader.frag.spv")
        .setVertexInput({bindingDescription},
                        {attributeDescriptions.begin(), attributeDescriptions.end()})
        .setRenderPass(hdrRenderPass)
        .setPipelineLayout(**pipelineLayout_)
        .setExtent(swapchainExtent)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Alpha)
        .build(rawPipeline);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create graphics pipeline");
        return false;
    }

    graphicsPipeline_.emplace(context.getRaiiDevice(), rawPipeline);
    return true;
}

void DescriptorInfrastructure::cleanup() {
    // Cleanup in reverse order
    graphicsPipeline_.reset();
    pipelineLayout_.reset();
    descriptorSetLayout_.reset();

    if (descriptorManagerPool_.has_value()) {
        descriptorManagerPool_->destroy();
        descriptorManagerPool_.reset();
    }

    initialized_ = false;
}
