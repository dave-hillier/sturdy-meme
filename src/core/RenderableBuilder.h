#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>
#include <cassert>
#include <cstdint>
#include <string>

class Mesh;
class Texture;

// Material ID type - use MaterialRegistry to convert to descriptor sets
using MaterialId = uint32_t;
constexpr MaterialId INVALID_MATERIAL_ID = ~0u;

// A fully-configured renderable object - can only be created via RenderableBuilder
struct Renderable {
    glm::mat4 transform;
    Mesh* mesh;
    Texture* texture;  // For debug/inspection. Use materialId for rendering.
    MaterialId materialId = INVALID_MATERIAL_ID;  // Used for descriptor set lookup during rendering
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveIntensity = 0.0f;
    glm::vec3 emissiveColor = glm::vec3(1.0f);
    bool castsShadow = true;
    float opacity = 1.0f;  // For camera occlusion fading (1.0 = fully visible)
    uint32_t pbrFlags = 0;  // Bitmask indicating which PBR textures are bound (set automatically from material)
    float alphaTestThreshold = 0.0f;  // Alpha test threshold (0 = disabled, >0 = discard if alpha < threshold)
    std::string barkType = "oak";  // Bark texture type for trees (oak, pine, birch, willow)
    std::string leafType = "oak";  // Leaf texture type for trees (oak, ash, aspen, pine)
    int leafInstanceIndex = -1;  // Index into TreeSystem::leafDrawInfoPerTree_ for instanced leaf rendering
    glm::vec3 leafTint = glm::vec3(1.0f);  // Leaf color tint
    float autumnHueShift = 0.0f;  // Autumn hue shift (0=summer, 1=full autumn)

private:
    friend class RenderableBuilder;
    Renderable() = default;
};

// Builder class that ensures a Renderable cannot be created without required fields
class RenderableBuilder {
public:
    RenderableBuilder() = default;

    // Required: Set the mesh for this renderable
    RenderableBuilder& withMesh(Mesh* mesh);

    // Required: Set the texture for this renderable
    RenderableBuilder& withTexture(Texture* texture);

    // Optional: Set material ID (for MaterialRegistry-based rendering)
    RenderableBuilder& withMaterialId(MaterialId id);

    // Required: Set the world transform
    RenderableBuilder& withTransform(const glm::mat4& transform);

    // Optional: Set PBR roughness (default: 0.5)
    RenderableBuilder& withRoughness(float roughness);

    // Optional: Set PBR metallic (default: 0.0)
    RenderableBuilder& withMetallic(float metallic);

    // Optional: Set emissive intensity (default: 0.0, no emission)
    RenderableBuilder& withEmissiveIntensity(float intensity);

    // Optional: Set emissive color (default: white)
    RenderableBuilder& withEmissiveColor(const glm::vec3& color);

    // Optional: Set whether object casts shadows (default: true)
    RenderableBuilder& withCastsShadow(bool casts);

    // Optional: Set alpha test threshold (default: 0.0 = disabled)
    // Pixels with alpha < threshold will be discarded
    RenderableBuilder& withAlphaTest(float threshold);

    // Optional: Set bark texture type for trees (oak, pine, birch, willow)
    RenderableBuilder& withBarkType(const std::string& type);

    // Optional: Set leaf texture type for trees (oak, ash, aspen, pine)
    RenderableBuilder& withLeafType(const std::string& type);

    // Optional: Set leaf color tint
    RenderableBuilder& withLeafTint(const glm::vec3& tint);

    // Optional: Set autumn hue shift (0=summer green, 1=full autumn colors)
    RenderableBuilder& withAutumnHueShift(float shift);

    // Convenience: Set position only (creates translation matrix)
    RenderableBuilder& atPosition(const glm::vec3& position);

    // Build the renderable - asserts if required fields are missing
    // Returns a fully configured Renderable
    Renderable build() const;

    // Check if all required fields are set
    bool isValid() const;

private:
    std::optional<glm::mat4> transform_;
    Mesh* mesh_ = nullptr;
    Texture* texture_ = nullptr;
    MaterialId materialId_ = INVALID_MATERIAL_ID;
    float roughness_ = 0.5f;
    float metallic_ = 0.0f;
    float emissiveIntensity_ = 0.0f;
    glm::vec3 emissiveColor_ = glm::vec3(1.0f);
    bool castsShadow_ = true;
    float alphaTestThreshold_ = 0.0f;
    std::string barkType_ = "oak";
    std::string leafType_ = "oak";
    glm::vec3 leafTint_ = glm::vec3(1.0f);
    float autumnHueShift_ = 0.0f;
};
