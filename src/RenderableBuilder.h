#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>
#include <cassert>
#include <cstdint>

class Mesh;
class Texture;

// Material ID type - use MaterialRegistry to convert to descriptor sets
using MaterialId = uint32_t;
constexpr MaterialId INVALID_MATERIAL_ID = ~0u;

// A fully-configured renderable object - can only be created via RenderableBuilder
struct Renderable {
    glm::mat4 transform;
    Mesh* mesh;
    Texture* texture;  // Kept for backwards compatibility, prefer materialId
    MaterialId materialId = INVALID_MATERIAL_ID;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveIntensity = 0.0f;
    glm::vec3 emissiveColor = glm::vec3(1.0f);
    bool castsShadow = true;
    float opacity = 1.0f;  // For camera occlusion fading (1.0 = fully visible)

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
};
