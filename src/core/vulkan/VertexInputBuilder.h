#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <cstddef>

/**
 * AttributeBuilder - Immutable builder for vertex input attribute descriptions
 *
 * Each setter returns a new copy, allowing stereotypes to be defined and reused.
 *
 * Example:
 *   auto posAttr = AttributeBuilder::vec3(0, offsetof(Vertex, position));
 *   auto uvAttr = AttributeBuilder::vec2(1, offsetof(Vertex, texCoord));
 */
class AttributeBuilder {
public:
    constexpr AttributeBuilder() = default;

    // ========================================================================
    // Setters (return new builder - immutable)
    // ========================================================================

    [[nodiscard]] constexpr AttributeBuilder location(uint32_t loc) const {
        AttributeBuilder copy = *this;
        copy.location_ = loc;
        return copy;
    }

    [[nodiscard]] constexpr AttributeBuilder binding(uint32_t bind) const {
        AttributeBuilder copy = *this;
        copy.binding_ = bind;
        return copy;
    }

    [[nodiscard]] constexpr AttributeBuilder format(vk::Format fmt) const {
        AttributeBuilder copy = *this;
        copy.format_ = fmt;
        return copy;
    }

    [[nodiscard]] constexpr AttributeBuilder offset(uint32_t off) const {
        AttributeBuilder copy = *this;
        copy.offset_ = off;
        return copy;
    }

    // ========================================================================
    // Stereotypes - predefined attribute formats
    // ========================================================================

    // Single float (R32_SFLOAT)
    static constexpr AttributeBuilder float1(uint32_t loc, uint32_t off, uint32_t bind = 0) {
        return AttributeBuilder()
            .location(loc)
            .binding(bind)
            .format(vk::Format::eR32Sfloat)
            .offset(off);
    }

    // vec2 (R32G32_SFLOAT) - UV coordinates
    static constexpr AttributeBuilder vec2(uint32_t loc, uint32_t off, uint32_t bind = 0) {
        return AttributeBuilder()
            .location(loc)
            .binding(bind)
            .format(vk::Format::eR32G32Sfloat)
            .offset(off);
    }

    // vec3 (R32G32B32_SFLOAT) - positions, normals
    static constexpr AttributeBuilder vec3(uint32_t loc, uint32_t off, uint32_t bind = 0) {
        return AttributeBuilder()
            .location(loc)
            .binding(bind)
            .format(vk::Format::eR32G32B32Sfloat)
            .offset(off);
    }

    // vec4 (R32G32B32A32_SFLOAT) - colors, tangents
    static constexpr AttributeBuilder vec4(uint32_t loc, uint32_t off, uint32_t bind = 0) {
        return AttributeBuilder()
            .location(loc)
            .binding(bind)
            .format(vk::Format::eR32G32B32A32Sfloat)
            .offset(off);
    }

    // ivec4 (R32G32B32A32_SINT) - bone indices
    static constexpr AttributeBuilder ivec4(uint32_t loc, uint32_t off, uint32_t bind = 0) {
        return AttributeBuilder()
            .location(loc)
            .binding(bind)
            .format(vk::Format::eR32G32B32A32Sint)
            .offset(off);
    }

    // uvec4 (R32G32B32A32_UINT) - unsigned int indices
    static constexpr AttributeBuilder uvec4(uint32_t loc, uint32_t off, uint32_t bind = 0) {
        return AttributeBuilder()
            .location(loc)
            .binding(bind)
            .format(vk::Format::eR32G32B32A32Uint)
            .offset(off);
    }

    // uint (R32_UINT) - single unsigned int
    static constexpr AttributeBuilder uint1(uint32_t loc, uint32_t off, uint32_t bind = 0) {
        return AttributeBuilder()
            .location(loc)
            .binding(bind)
            .format(vk::Format::eR32Uint)
            .offset(off);
    }

    // ========================================================================
    // Build method
    // ========================================================================

    [[nodiscard]] constexpr vk::VertexInputAttributeDescription build() const {
        return vk::VertexInputAttributeDescription{}
            .setLocation(location_)
            .setBinding(binding_)
            .setFormat(format_)
            .setOffset(offset_);
    }

    // Implicit conversion
    [[nodiscard]] constexpr operator vk::VertexInputAttributeDescription() const {
        return build();
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    [[nodiscard]] constexpr uint32_t getLocation() const { return location_; }
    [[nodiscard]] constexpr uint32_t getBinding() const { return binding_; }
    [[nodiscard]] constexpr vk::Format getFormat() const { return format_; }
    [[nodiscard]] constexpr uint32_t getOffset() const { return offset_; }

private:
    uint32_t location_ = 0;
    uint32_t binding_ = 0;
    vk::Format format_ = vk::Format::eR32G32B32Sfloat;
    uint32_t offset_ = 0;
};

/**
 * BindingBuilder - Immutable builder for vertex input binding descriptions
 *
 * Example:
 *   auto perVertex = BindingBuilder::perVertex<Vertex>(0);
 *   auto perInstance = BindingBuilder::perInstance<InstanceData>(1);
 */
class VertexBindingBuilder {
public:
    constexpr VertexBindingBuilder() = default;

    // ========================================================================
    // Setters (return new builder - immutable)
    // ========================================================================

    [[nodiscard]] constexpr VertexBindingBuilder binding(uint32_t bind) const {
        VertexBindingBuilder copy = *this;
        copy.binding_ = bind;
        return copy;
    }

    [[nodiscard]] constexpr VertexBindingBuilder stride(uint32_t s) const {
        VertexBindingBuilder copy = *this;
        copy.stride_ = s;
        return copy;
    }

    [[nodiscard]] constexpr VertexBindingBuilder inputRate(vk::VertexInputRate rate) const {
        VertexBindingBuilder copy = *this;
        copy.inputRate_ = rate;
        return copy;
    }

    // ========================================================================
    // Stereotypes
    // ========================================================================

    // Per-vertex data (most common)
    template<typename VertexType>
    static constexpr VertexBindingBuilder perVertex(uint32_t bind = 0) {
        return VertexBindingBuilder()
            .binding(bind)
            .stride(sizeof(VertexType))
            .inputRate(vk::VertexInputRate::eVertex);
    }

    // Per-instance data (for instanced rendering)
    template<typename InstanceType>
    static constexpr VertexBindingBuilder perInstance(uint32_t bind = 1) {
        return VertexBindingBuilder()
            .binding(bind)
            .stride(sizeof(InstanceType))
            .inputRate(vk::VertexInputRate::eInstance);
    }

    // ========================================================================
    // Build method
    // ========================================================================

    [[nodiscard]] constexpr vk::VertexInputBindingDescription build() const {
        return vk::VertexInputBindingDescription{}
            .setBinding(binding_)
            .setStride(stride_)
            .setInputRate(inputRate_);
    }

    // Implicit conversion
    [[nodiscard]] constexpr operator vk::VertexInputBindingDescription() const {
        return build();
    }

private:
    uint32_t binding_ = 0;
    uint32_t stride_ = 0;
    vk::VertexInputRate inputRate_ = vk::VertexInputRate::eVertex;
};

/**
 * VertexInputBuilder - Immutable builder for complete vertex input state
 *
 * Collects bindings and attributes to build vk::PipelineVertexInputStateCreateInfo.
 *
 * Example usage:
 *   // Define a reusable vertex layout stereotype
 *   static const auto standardMeshLayout = VertexInputBuilder()
 *       .addBinding(VertexBindingBuilder::perVertex<Vertex>(0))
 *       .addAttribute(AttributeBuilder::vec3(0, offsetof(Vertex, position)))
 *       .addAttribute(AttributeBuilder::vec3(1, offsetof(Vertex, normal)))
 *       .addAttribute(AttributeBuilder::vec2(2, offsetof(Vertex, texCoord)));
 *
 *   // Use it directly
 *   auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
 *       .setPVertexInputState(&standardMeshLayout.build());
 *
 *   // Or customize from stereotype
 *   auto withTangents = standardMeshLayout
 *       .addAttribute(AttributeBuilder::vec4(3, offsetof(Vertex, tangent)));
 */
class VertexInputBuilder {
public:
    VertexInputBuilder() = default;

    // ========================================================================
    // Add methods (return new builder - immutable)
    // ========================================================================

    [[nodiscard]] VertexInputBuilder addBinding(const VertexBindingBuilder& binding) const {
        VertexInputBuilder copy = *this;
        copy.bindings_.push_back(binding.build());
        return copy;
    }

    [[nodiscard]] VertexInputBuilder addBinding(const vk::VertexInputBindingDescription& binding) const {
        VertexInputBuilder copy = *this;
        copy.bindings_.push_back(binding);
        return copy;
    }

    [[nodiscard]] VertexInputBuilder addAttribute(const AttributeBuilder& attr) const {
        VertexInputBuilder copy = *this;
        copy.attributes_.push_back(attr.build());
        return copy;
    }

    [[nodiscard]] VertexInputBuilder addAttribute(const vk::VertexInputAttributeDescription& attr) const {
        VertexInputBuilder copy = *this;
        copy.attributes_.push_back(attr);
        return copy;
    }

    // Add multiple attributes at once
    [[nodiscard]] VertexInputBuilder addAttributes(std::initializer_list<AttributeBuilder> attrs) const {
        VertexInputBuilder copy = *this;
        for (const auto& attr : attrs) {
            copy.attributes_.push_back(attr.build());
        }
        return copy;
    }

    // ========================================================================
    // Stereotypes - common vertex layouts
    // ========================================================================

    // Position only (for shadow/depth passes)
    template<typename VertexType>
    static VertexInputBuilder positionOnly(uint32_t posOffset = 0) {
        return VertexInputBuilder()
            .addBinding(VertexBindingBuilder::perVertex<VertexType>(0))
            .addAttribute(AttributeBuilder::vec3(0, posOffset));
    }

    // Position + UV (for simple textured meshes)
    template<typename VertexType>
    static VertexInputBuilder positionUV(uint32_t posOffset, uint32_t uvOffset) {
        return VertexInputBuilder()
            .addBinding(VertexBindingBuilder::perVertex<VertexType>(0))
            .addAttribute(AttributeBuilder::vec3(0, posOffset))
            .addAttribute(AttributeBuilder::vec2(1, uvOffset));
    }

    // Position + Normal + UV (standard mesh)
    template<typename VertexType>
    static VertexInputBuilder positionNormalUV(uint32_t posOffset, uint32_t normalOffset, uint32_t uvOffset) {
        return VertexInputBuilder()
            .addBinding(VertexBindingBuilder::perVertex<VertexType>(0))
            .addAttribute(AttributeBuilder::vec3(0, posOffset))
            .addAttribute(AttributeBuilder::vec3(1, normalOffset))
            .addAttribute(AttributeBuilder::vec2(2, uvOffset));
    }

    // Full PBR vertex (position, normal, UV, tangent, color)
    template<typename VertexType>
    static VertexInputBuilder fullPBR(uint32_t posOffset, uint32_t normalOffset,
                                       uint32_t uvOffset, uint32_t tangentOffset,
                                       uint32_t colorOffset, uint32_t colorLocation = 6) {
        return VertexInputBuilder()
            .addBinding(VertexBindingBuilder::perVertex<VertexType>(0))
            .addAttribute(AttributeBuilder::vec3(0, posOffset))
            .addAttribute(AttributeBuilder::vec3(1, normalOffset))
            .addAttribute(AttributeBuilder::vec2(2, uvOffset))
            .addAttribute(AttributeBuilder::vec4(3, tangentOffset))
            .addAttribute(AttributeBuilder::vec4(colorLocation, colorOffset));
    }

    // Empty input (for fullscreen passes that generate vertices in shader)
    static VertexInputBuilder empty() {
        return VertexInputBuilder();
    }

    // ========================================================================
    // Build method
    // ========================================================================

    // Build and store the create info (bindings/attributes must remain valid)
    [[nodiscard]] vk::PipelineVertexInputStateCreateInfo build() const {
        return vk::PipelineVertexInputStateCreateInfo{}
            .setVertexBindingDescriptions(bindings_)
            .setVertexAttributeDescriptions(attributes_);
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    [[nodiscard]] const std::vector<vk::VertexInputBindingDescription>& getBindings() const {
        return bindings_;
    }

    [[nodiscard]] const std::vector<vk::VertexInputAttributeDescription>& getAttributes() const {
        return attributes_;
    }

    [[nodiscard]] size_t getBindingCount() const { return bindings_.size(); }
    [[nodiscard]] size_t getAttributeCount() const { return attributes_.size(); }

private:
    std::vector<vk::VertexInputBindingDescription> bindings_;
    std::vector<vk::VertexInputAttributeDescription> attributes_;
};
