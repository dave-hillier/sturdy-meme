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
        // Binding 6: Point shadow maps
        .writeImage(6, common.pointShadowView, common.pointShadowSampler)
        // Binding 7: Spot shadow maps
        .writeImage(7, common.spotShadowView, common.spotShadowSampler)
        // Binding 8: Snow mask
        .writeImage(8, common.snowMaskView, common.snowMaskSampler);

    // Binding 9: Cloud shadow (optional, may be null during initial creation)
    if (common.cloudShadowView != VK_NULL_HANDLE) {
        writer.writeImage(9, common.cloudShadowView, common.cloudShadowSampler);
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
        .writeImage(3, material.normalView, material.normalSampler)
        .update();
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

    // Binding 10: Bone matrices (required for skinned meshes)
    if (common.boneMatricesBuffer != VK_NULL_HANDLE) {
        writer.writeBuffer(10, common.boneMatricesBuffer, 0, common.boneMatricesBufferSize);
    }

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
