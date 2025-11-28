#include "RenderableBuilder.h"
#include <SDL3/SDL_log.h>

RenderableBuilder& RenderableBuilder::withMesh(Mesh* mesh) {
    mesh_ = mesh;
    return *this;
}

RenderableBuilder& RenderableBuilder::withTexture(Texture* texture) {
    texture_ = texture;
    return *this;
}

RenderableBuilder& RenderableBuilder::withTransform(const glm::mat4& transform) {
    transform_ = transform;
    return *this;
}

RenderableBuilder& RenderableBuilder::withRoughness(float roughness) {
    roughness_ = roughness;
    return *this;
}

RenderableBuilder& RenderableBuilder::withMetallic(float metallic) {
    metallic_ = metallic;
    return *this;
}

RenderableBuilder& RenderableBuilder::withEmissiveIntensity(float intensity) {
    emissiveIntensity_ = intensity;
    return *this;
}

RenderableBuilder& RenderableBuilder::withEmissiveColor(const glm::vec3& color) {
    emissiveColor_ = color;
    return *this;
}

RenderableBuilder& RenderableBuilder::withCastsShadow(bool casts) {
    castsShadow_ = casts;
    return *this;
}

RenderableBuilder& RenderableBuilder::atPosition(const glm::vec3& position) {
    transform_ = glm::translate(glm::mat4(1.0f), position);
    return *this;
}

bool RenderableBuilder::isValid() const {
    return mesh_ != nullptr && texture_ != nullptr && transform_.has_value();
}

Renderable RenderableBuilder::build() const {
    // Assert that all required fields are set
    if (!mesh_) {
        SDL_Log("RenderableBuilder::build() failed: mesh is required");
        assert(mesh_ != nullptr && "RenderableBuilder: mesh is required");
    }
    if (!texture_) {
        SDL_Log("RenderableBuilder::build() failed: texture is required");
        assert(texture_ != nullptr && "RenderableBuilder: texture is required");
    }
    if (!transform_.has_value()) {
        SDL_Log("RenderableBuilder::build() failed: transform is required");
        assert(transform_.has_value() && "RenderableBuilder: transform is required");
    }

    Renderable renderable;
    renderable.transform = transform_.value();
    renderable.mesh = mesh_;
    renderable.texture = texture_;
    renderable.roughness = roughness_;
    renderable.metallic = metallic_;
    renderable.emissiveIntensity = emissiveIntensity_;
    renderable.emissiveColor = emissiveColor_;
    renderable.castsShadow = castsShadow_;

    return renderable;
}
