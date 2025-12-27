#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <string>
#include <array>

// Forward declarations
class PipelineBuilder;
class ManagedPipeline;

/**
 * GraphicsPipelineFactory - Fluent builder for Vulkan graphics pipelines
 *
 * Reduces duplication in graphics pipeline creation by providing:
 * - Sensible defaults for all pipeline states
 * - Pre-configured presets for common use cases
 * - Fluent API for customization
 *
 * Usage:
 *   GraphicsPipelineFactory factory(device);
 *   factory.setShaders(vertPath, fragPath)
 *          .setRenderPass(renderPass)
 *          .setExtent(extent)
 *          .setPipelineLayout(layout)
 *          .build(pipeline);
 */
class GraphicsPipelineFactory {
public:
    // Common blend mode presets
    enum class BlendMode {
        None,       // No blending (opaque)
        Alpha,      // Standard alpha blending
        Additive,   // Additive blending (src + dst)
        Premultiplied  // Premultiplied alpha
    };

    // Common pipeline presets
    enum class Preset {
        Default,        // Standard 3D rendering with depth test
        FullscreenQuad, // No vertex input, no depth, for post-processing
        Shadow,         // Depth-only rendering with bias
        Particle        // Alpha blending, no depth write
    };

    explicit GraphicsPipelineFactory(VkDevice device);
    ~GraphicsPipelineFactory();

    // Reset all state to defaults
    GraphicsPipelineFactory& reset();

    // Set pipeline cache for faster pipeline creation
    GraphicsPipelineFactory& setPipelineCache(VkPipelineCache cache);

    // Apply a preset configuration
    GraphicsPipelineFactory& applyPreset(Preset preset);

    // Shader configuration
    GraphicsPipelineFactory& setShaders(const std::string& vertPath, const std::string& fragPath);
    GraphicsPipelineFactory& setVertexShader(const std::string& path);
    GraphicsPipelineFactory& setFragmentShader(const std::string& path);
    GraphicsPipelineFactory& setTessellationShaders(const std::string& tescPath, const std::string& tesePath);
    GraphicsPipelineFactory& setTessellationControlShader(const std::string& path);
    GraphicsPipelineFactory& setTessellationEvaluationShader(const std::string& path);

    // Render pass and layout
    GraphicsPipelineFactory& setRenderPass(VkRenderPass renderPass, uint32_t subpass = 0);
    GraphicsPipelineFactory& setPipelineLayout(VkPipelineLayout layout);

    // Viewport/scissor configuration
    GraphicsPipelineFactory& setExtent(VkExtent2D extent);
    GraphicsPipelineFactory& setDynamicViewport(bool dynamic = true);

    // Vertex input configuration
    GraphicsPipelineFactory& setVertexInput(
        const std::vector<VkVertexInputBindingDescription>& bindings,
        const std::vector<VkVertexInputAttributeDescription>& attributes);
    GraphicsPipelineFactory& setNoVertexInput();

    // Input assembly
    GraphicsPipelineFactory& setTopology(VkPrimitiveTopology topology);

    // Rasterization
    GraphicsPipelineFactory& setCullMode(VkCullModeFlags cullMode);
    GraphicsPipelineFactory& setFrontFace(VkFrontFace frontFace);
    GraphicsPipelineFactory& setPolygonMode(VkPolygonMode polygonMode);
    GraphicsPipelineFactory& setDepthBias(float constantFactor, float slopeFactor);
    GraphicsPipelineFactory& setDepthClamp(bool enable);
    GraphicsPipelineFactory& setLineWidth(float width);

    // Multisampling
    GraphicsPipelineFactory& setSampleCount(VkSampleCountFlagBits samples);
    GraphicsPipelineFactory& setAlphaToCoverage(bool enable);
    GraphicsPipelineFactory& setAlphaToOne(bool enable);

    // Depth/stencil
    GraphicsPipelineFactory& setDepthTest(bool enable);
    GraphicsPipelineFactory& setDepthWrite(bool enable);
    GraphicsPipelineFactory& setDepthCompareOp(VkCompareOp op);
    GraphicsPipelineFactory& setDepthBoundsTest(bool enable, float minBounds = 0.0f, float maxBounds = 1.0f);
    GraphicsPipelineFactory& setStencilTest(bool enable);

    // Color blending
    GraphicsPipelineFactory& setBlendMode(BlendMode mode);
    GraphicsPipelineFactory& setColorBlendAttachment(const VkPipelineColorBlendAttachmentState& attachment);
    GraphicsPipelineFactory& setColorWriteMask(VkColorComponentFlags mask);
    GraphicsPipelineFactory& setNoColorAttachments();
    GraphicsPipelineFactory& setColorAttachmentCount(uint32_t count);  // For multiple render targets

    // Build the pipeline (raw handle - caller must manage lifetime)
    bool build(VkPipeline& pipeline);

    // Build and return RAII-managed pipeline
    bool buildManaged(ManagedPipeline& outPipeline);

    // Cleanup any allocated shader modules (called automatically by build)
    void cleanup();

private:
    VkDevice device;
    VkPipelineCache pipelineCacheHandle = VK_NULL_HANDLE;

    // Shader state
    std::string vertShaderPath;
    std::string fragShaderPath;
    std::string tescShaderPath;  // Tessellation control shader
    std::string teseShaderPath;  // Tessellation evaluation shader
    std::vector<VkShaderModule> shaderModules;

    // Pipeline configuration
    VkRenderPass renderPass = VK_NULL_HANDLE;
    uint32_t subpass = 0;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    // Viewport state
    VkExtent2D extent = {0, 0};
    bool dynamicViewport = false;

    // Vertex input state
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;

    // Input assembly state
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Rasterization state
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    float lineWidth = 1.0f;
    bool depthClampEnable = false;
    bool depthBiasEnable = false;
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;

    // Multisampling state
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    bool alphaToCoverageEnable = false;
    bool alphaToOneEnable = false;

    // Depth/stencil state
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
    bool depthBoundsTestEnable = false;
    float minDepthBounds = 0.0f;
    float maxDepthBounds = 1.0f;
    bool stencilTestEnable = false;

    // Color blend state
    bool hasColorAttachments = true;
    uint32_t colorAttachmentCount = 1;  // Number of color attachments (MRT support)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};

    void initDefaultColorBlendAttachment();
    bool loadShaderModules(std::vector<VkPipelineShaderStageCreateInfo>& stages);
};
