#pragma once

#include <vulkan/vulkan.h>
#include "DescriptorManager.h"
#include "Texture.h"

/**
 * MaterialDescriptorFactory - Encapsulates common descriptor bindings for materials
 *
 * Reduces duplication when creating descriptor sets for different materials.
 * All materials share the same "common" bindings (UBO, shadow maps, lights, etc.)
 * but differ in their texture bindings.
 */
class MaterialDescriptorFactory {
public:
    // Common resources shared across all material descriptor sets
    struct CommonBindings {
        VkBuffer uniformBuffer;
        VkDeviceSize uniformBufferSize;

        VkImageView shadowMapView;
        VkSampler shadowMapSampler;

        VkBuffer lightBuffer;
        VkDeviceSize lightBufferSize;

        VkImageView emissiveMapView;
        VkSampler emissiveMapSampler;

        VkImageView pointShadowView;
        VkSampler pointShadowSampler;

        VkImageView spotShadowView;
        VkSampler spotShadowSampler;

        VkImageView snowMaskView;
        VkSampler snowMaskSampler;

        // Optional: cloud shadow (may be added after initial creation)
        VkImageView cloudShadowView = VK_NULL_HANDLE;
        VkSampler cloudShadowSampler = VK_NULL_HANDLE;

        // Snow and cloud shadow UBOs (binding 10 and 11)
        VkBuffer snowUboBuffer = VK_NULL_HANDLE;
        VkDeviceSize snowUboBufferSize = 0;
        VkBuffer cloudShadowUboBuffer = VK_NULL_HANDLE;
        VkDeviceSize cloudShadowUboBufferSize = 0;

        // Optional: bone matrices for skinned meshes
        VkBuffer boneMatricesBuffer = VK_NULL_HANDLE;
        VkDeviceSize boneMatricesBufferSize = 0;

        // Placeholder texture for unused PBR bindings (bindings 13-16 must always be written)
        VkImageView placeholderTextureView = VK_NULL_HANDLE;
        VkSampler placeholderTextureSampler = VK_NULL_HANDLE;
    };

    // Per-material texture bindings
    struct MaterialTextures {
        VkImageView diffuseView;
        VkSampler diffuseSampler;
        VkImageView normalView;
        VkSampler normalSampler;

        // Optional PBR textures (for Substance/PBR materials)
        // Set to VK_NULL_HANDLE if not used - shader will use push constant values
        VkImageView roughnessView = VK_NULL_HANDLE;
        VkSampler roughnessSampler = VK_NULL_HANDLE;
        VkImageView metallicView = VK_NULL_HANDLE;
        VkSampler metallicSampler = VK_NULL_HANDLE;
        VkImageView aoView = VK_NULL_HANDLE;
        VkSampler aoSampler = VK_NULL_HANDLE;
        VkImageView heightView = VK_NULL_HANDLE;
        VkSampler heightSampler = VK_NULL_HANDLE;
    };

    explicit MaterialDescriptorFactory(VkDevice device);

    // Write a complete material descriptor set using common + material-specific bindings
    void writeDescriptorSet(
        VkDescriptorSet set,
        const CommonBindings& common,
        const MaterialTextures& material);

    // Write a skinned material descriptor set (includes bone matrices at binding 10)
    void writeSkinnedDescriptorSet(
        VkDescriptorSet set,
        const CommonBindings& common,
        const MaterialTextures& material);

    // Update only the cloud shadow binding (for late initialization)
    void updateCloudShadowBinding(
        VkDescriptorSet set,
        VkImageView cloudShadowView,
        VkSampler cloudShadowSampler);

private:
    VkDevice device;

    void writeCommonBindings(
        DescriptorManager::SetWriter& writer,
        const CommonBindings& common);
};
