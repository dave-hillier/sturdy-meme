#include "MaterialDescriptorFactory.h"

MaterialDescriptorFactory::MaterialDescriptorFactory(VkDevice device)
    : device(device) {}

void MaterialDescriptorFactory::writeCommonBindings(
    DescriptorManager::SetWriter& writer,
    const CommonBindings& common) {

    writer
        // Binding 0: UBO
        .writeBuffer(0, common.uniformBuffer, 0, common.uniformBufferSize)
        // Binding 2: Shadow map (depth format)
        .writeImage(2, common.shadowMapView, common.shadowMapSampler,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        // Binding 4: Light buffer (SSBO)
        .writeBuffer(4, common.lightBuffer, 0, common.lightBufferSize,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
        // Binding 5: Emissive map
        .writeImage(5, common.emissiveMapView, common.emissiveMapSampler)
        // Binding 6: Point shadow maps (depth format, needs correct layout)
        .writeImage(6, common.pointShadowView, common.pointShadowSampler,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        // Binding 7: Spot shadow maps (depth format, needs correct layout)
        .writeImage(7, common.spotShadowView, common.spotShadowSampler,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        // Binding 8: Snow mask
        .writeImage(8, common.snowMaskView, common.snowMaskSampler);

    // Binding 9: Cloud shadow (optional, may be null during initial creation)
    if (common.cloudShadowView != VK_NULL_HANDLE) {
        writer.writeImage(9, common.cloudShadowView, common.cloudShadowSampler);
    }

    // Binding 10: Snow UBO (optional, may be null during initial creation)
    if (common.snowUboBuffer != VK_NULL_HANDLE) {
        writer.writeBuffer(10, common.snowUboBuffer, 0, common.snowUboBufferSize);
    }

    // Binding 11: Cloud shadow UBO (optional, may be null during initial creation)
    if (common.cloudShadowUboBuffer != VK_NULL_HANDLE) {
        writer.writeBuffer(11, common.cloudShadowUboBuffer, 0, common.cloudShadowUboBufferSize);
    }

    // Binding 17: Wind UBO for vegetation animation
    if (common.windBuffer != VK_NULL_HANDLE) {
        writer.writeBuffer(17, common.windBuffer, 0, common.windBufferSize);
    }
}

void MaterialDescriptorFactory::writeDescriptorSet(
    VkDescriptorSet set,
    const CommonBindings& common,
    const MaterialTextures& material) {

    DescriptorManager::SetWriter writer(device, set);
    writeCommonBindings(writer, common);

    writer
        // Binding 1: Diffuse texture
        .writeImage(1, material.diffuseView, material.diffuseSampler)
        // Binding 3: Normal map
        .writeImage(3, material.normalView, material.normalSampler);

    // PBR texture bindings (13-16) - always write, using placeholder if no texture provided
    VkImageView placeholderView = common.placeholderTextureView;
    VkSampler placeholderSampler = common.placeholderTextureSampler;

    writer.writeImage(13,
        material.roughnessView != VK_NULL_HANDLE ? material.roughnessView : placeholderView,
        material.roughnessSampler != VK_NULL_HANDLE ? material.roughnessSampler : placeholderSampler);
    writer.writeImage(14,
        material.metallicView != VK_NULL_HANDLE ? material.metallicView : placeholderView,
        material.metallicSampler != VK_NULL_HANDLE ? material.metallicSampler : placeholderSampler);
    writer.writeImage(15,
        material.aoView != VK_NULL_HANDLE ? material.aoView : placeholderView,
        material.aoSampler != VK_NULL_HANDLE ? material.aoSampler : placeholderSampler);
    writer.writeImage(16,
        material.heightView != VK_NULL_HANDLE ? material.heightView : placeholderView,
        material.heightSampler != VK_NULL_HANDLE ? material.heightSampler : placeholderSampler);

    writer.update();
}

void MaterialDescriptorFactory::writeSkinnedDescriptorSet(
    VkDescriptorSet set,
    const CommonBindings& common,
    const MaterialTextures& material) {

    DescriptorManager::SetWriter writer(device, set);
    writeCommonBindings(writer, common);

    writer
        // Binding 1: Diffuse texture
        .writeImage(1, material.diffuseView, material.diffuseSampler)
        // Binding 3: Normal map
        .writeImage(3, material.normalView, material.normalSampler);

    // Binding 12: Bone matrices (required for skinned meshes)
    // Use UNIFORM_BUFFER_DYNAMIC to enable per-draw offset selection for character-specific bone data
    if (common.boneMatricesBuffer != VK_NULL_HANDLE) {
        writer.writeBuffer(12, common.boneMatricesBuffer, 0, common.boneMatricesBufferSize,
                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
    }

    // PBR texture bindings (13-16) - always write, using placeholder if no texture provided
    VkImageView placeholderView = common.placeholderTextureView;
    VkSampler placeholderSampler = common.placeholderTextureSampler;

    writer.writeImage(13,
        material.roughnessView != VK_NULL_HANDLE ? material.roughnessView : placeholderView,
        material.roughnessSampler != VK_NULL_HANDLE ? material.roughnessSampler : placeholderSampler);
    writer.writeImage(14,
        material.metallicView != VK_NULL_HANDLE ? material.metallicView : placeholderView,
        material.metallicSampler != VK_NULL_HANDLE ? material.metallicSampler : placeholderSampler);
    writer.writeImage(15,
        material.aoView != VK_NULL_HANDLE ? material.aoView : placeholderView,
        material.aoSampler != VK_NULL_HANDLE ? material.aoSampler : placeholderSampler);
    writer.writeImage(16,
        material.heightView != VK_NULL_HANDLE ? material.heightView : placeholderView,
        material.heightSampler != VK_NULL_HANDLE ? material.heightSampler : placeholderSampler);

    writer.update();
}

void MaterialDescriptorFactory::updateCloudShadowBinding(
    VkDescriptorSet set,
    VkImageView cloudShadowView,
    VkSampler cloudShadowSampler) {

    DescriptorManager::SetWriter writer(device, set);
    writer.writeImage(9, cloudShadowView, cloudShadowSampler)
          .update();
}
