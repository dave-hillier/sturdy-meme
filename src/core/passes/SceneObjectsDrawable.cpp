#include "SceneObjectsDrawable.h"
#include "UBOs.h"  // For PushConstants (generated from shaders)
#include "../GPUSceneBuffer.h"

#include "SceneManager.h"
#include "scene/SceneBuilder.h"
#include "npc/NPCSimulation.h"
#include "GrassSystem.h"
#include "ScatterSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "GlobalBufferManager.h"
#include "ShadowSystem.h"
#include "WindSystem.h"
#include "Mesh.h"

// ECS includes
#include "ecs/World.h"
#include "ecs/Components.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <numeric>

SceneObjectsDrawable::SceneObjectsDrawable(const Resources& resources)
    : resources_(resources)
{
}

void SceneObjectsDrawable::recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                          float time, const HDRDrawParams& params) {
    vk::CommandBuffer vkCmd(cmd);

    if (params.sceneObjectsPipeline) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *params.sceneObjectsPipeline);
    }
    recordSceneObjects(cmd, frameIndex, params);
}

void SceneObjectsDrawable::recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex,
                                               const HDRDrawParams& params) {
    if (!params.pipelineLayout) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SceneObjectsDrawable: pipelineLayout not set");
        return;
    }

    // Use GPU-driven indirect rendering if enabled
    if (params.useIndirectDraw && params.gpuSceneBuffer && params.gpuSceneBuffer->getObjectCount() > 0) {
        recordSceneObjectsIndirect(cmd, frameIndex, params);
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Get MaterialRegistry for descriptor set lookup
    const auto& materialRegistry = resources_.scene->getSceneBuilder().getMaterialRegistry();

    // Helper lambda to render an entity with RenderData
    auto renderWithRenderData = [&](const ecs::RenderData& data, VkDescriptorSet descSet) {
        if (!data.mesh) return;

        PushConstants push{};
        push.model = data.transform;
        push.roughness = data.roughness;
        push.metallic = data.metallic;
        push.emissiveIntensity = data.emissiveIntensity;
        push.opacity = data.opacity;
        push.emissiveColor = glm::vec4(data.emissiveColor, 1.0f);
        push.pbrFlags = data.pbrFlags;
        push.alphaTestThreshold = data.alphaTestThreshold;

        vkCmd.pushConstants<PushConstants>(
            *params.pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, push);

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                 *params.pipelineLayout, 0, vk::DescriptorSet(descSet), {});

        vk::Buffer vertexBuffers[] = {data.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(data.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        vkCmd.drawIndexed(data.mesh->getIndexCount(), 1, 0, 0, 0);
    };

    // Helper lambda to render a legacy Renderable (for rocks/detritus)
    auto renderObject = [&](const Renderable& obj, VkDescriptorSet descSet) {
        PushConstants push{};
        push.model = obj.transform;
        push.roughness = obj.roughness;
        push.metallic = obj.metallic;
        push.emissiveIntensity = obj.emissiveIntensity;
        push.opacity = obj.opacity;
        push.emissiveColor = glm::vec4(obj.emissiveColor, 1.0f);
        push.pbrFlags = obj.pbrFlags;
        push.alphaTestThreshold = obj.alphaTestThreshold;

        vkCmd.pushConstants<PushConstants>(
            *params.pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, push);

        vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                 *params.pipelineLayout, 0, vk::DescriptorSet(descSet), {});

        vk::Buffer vertexBuffers[] = {obj.mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(obj.mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        vkCmd.drawIndexed(obj.mesh->getIndexCount(), 1, 0, 0, 0);
    };

    // When V-buffer is active, scene objects are rendered by the V-buffer resolve pass.
    // Skip them here but still render rocks, detritus, trees below.
    if (!resources_.visBufferActive && resources_.ecsWorld) {
        ecs::World& world = *resources_.ecsWorld;

        // Collect entities to render (those with MeshRef and MaterialRef, excluding special entities)
        std::vector<ecs::RenderData> renderList;
        renderList.reserve(256);  // Preallocate for typical scene size

        // Query all entities with MeshRef and MaterialRef (required for rendering)
        for (auto [entity, meshRef, materialRef] : world.view<ecs::MeshRef, ecs::MaterialRef>().each()) {
            // Skip entities rendered by specialized systems
            if (world.has<ecs::PlayerTag>(entity)) continue;   // Skinned mesh renderer
            if (world.has<ecs::NPCTag>(entity)) continue;      // NPC renderer
            if (world.has<ecs::TreeData>(entity)) continue;    // Tree renderer

            // Extract render data from entity's components
            ecs::RenderData data = ecs::extractRenderData(world, entity);
            if (data.mesh && data.materialId != ecs::InvalidMaterialId) {
                renderList.push_back(data);
            }
        }

        // Sort by materialId to minimize descriptor set switches
        std::sort(renderList.begin(), renderList.end(), [](const ecs::RenderData& a, const ecs::RenderData& b) {
            return a.materialId < b.materialId;
        });

        // Render sorted entities
        ecs::MaterialId lastMaterialId = ecs::InvalidMaterialId;
        VkDescriptorSet currentDescSet = VK_NULL_HANDLE;

        for (const auto& data : renderList) {
            if (data.materialId != lastMaterialId) {
                currentDescSet = materialRegistry.getDescriptorSet(data.materialId, frameIndex);
                if (currentDescSet == VK_NULL_HANDLE) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Skipping entity with invalid materialId %u", data.materialId);
                    continue;
                }
                lastMaterialId = data.materialId;
            }
            renderWithRenderData(data, currentDescSet);
        }
    } else if (!resources_.visBufferActive) {
        // Legacy path: Use Renderable vector
        const auto& sceneObjects = resources_.scene->getRenderables();

        // Build sorted indices by materialId to minimize descriptor set switches
        std::vector<size_t> sortedIndices(sceneObjects.size());
        std::iota(sortedIndices.begin(), sortedIndices.end(), 0);
        std::sort(sortedIndices.begin(), sortedIndices.end(), [&](size_t a, size_t b) {
            return sceneObjects[a].materialId < sceneObjects[b].materialId;
        });

        MaterialId lastMaterialId = INVALID_MATERIAL_ID;
        VkDescriptorSet currentDescSet = VK_NULL_HANDLE;

        for (size_t i : sortedIndices) {
            // Skip GPU-skinned characters (player + NPCs, rendered via separate pipeline)
            if (sceneObjects[i].gpuSkinned) continue;

            const auto& obj = sceneObjects[i];

            // Only update descriptor set when material changes
            if (obj.materialId != lastMaterialId) {
                currentDescSet = materialRegistry.getDescriptorSet(obj.materialId, frameIndex);
                if (currentDescSet == VK_NULL_HANDLE) {
                    // Skip objects with invalid materialId
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Skipping object with invalid materialId %u", obj.materialId);
                    continue;
                }
                lastMaterialId = obj.materialId;
            }
            renderObject(obj, currentDescSet);
        }
    }

    // Render procedural rocks (ScatterSystem owns its own descriptor sets)
    // These still use legacy Renderable as they're procedurally generated
    if (resources_.rocks && resources_.rocks->hasDescriptorSets()) {
        VkDescriptorSet rockDescSet = resources_.rocks->getDescriptorSet(frameIndex);
        for (const auto& rock : resources_.rocks->getSceneObjects()) {
            renderObject(rock, rockDescSet);
        }
    }

    // Render woodland detritus (ScatterSystem owns its own descriptor sets)
    if (resources_.detritus && resources_.detritus->hasDescriptorSets()) {
        VkDescriptorSet detritusDescSet = resources_.detritus->getDescriptorSet(frameIndex);
        for (const auto& detritus : resources_.detritus->getSceneObjects()) {
            renderObject(detritus, detritusDescSet);
        }
    }

    // Render procedural trees using dedicated TreeRenderer with wind animation
    if (resources_.tree && resources_.treeRenderer) {
        resources_.treeRenderer->render(vk::CommandBuffer(cmd), frameIndex,
                                        resources_.wind->getTime(),
                                        *resources_.tree, resources_.treeLOD);
    }

    // Render tree impostors for distant trees
    if (resources_.treeLOD) {
        if (resources_.impostorCull && resources_.impostorCull->getTreeCount() > 0) {
            // Use GPU-culled indirect rendering
            resources_.treeLOD->renderImpostorsGPUCulled(
                cmd, frameIndex,
                resources_.globalBuffers->uniformBuffers.buffers[frameIndex],
                resources_.shadow->getShadowImageView(),
                resources_.shadow->getShadowSampler(),
                resources_.impostorCull->getVisibleImpostorBuffer(),
                resources_.impostorCull->getIndirectDrawBuffer()
            );
        } else {
            // Fall back to CPU-culled rendering
            resources_.treeLOD->renderImpostors(
                cmd, frameIndex,
                resources_.globalBuffers->uniformBuffers.buffers[frameIndex],
                resources_.shadow->getShadowImageView(),
                resources_.shadow->getShadowSampler()
            );
        }
    }
}

void SceneObjectsDrawable::recordSceneObjectsIndirect(VkCommandBuffer cmd, uint32_t frameIndex,
                                                       const HDRDrawParams& params) {
    if (!params.gpuSceneBuffer || !params.instancedPipelineLayout) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SceneObjectsDrawable: Indirect rendering requires gpuSceneBuffer and instancedPipelineLayout");
        return;
    }

    GPUSceneBuffer* sceneBuffer = params.gpuSceneBuffer;
    if (sceneBuffer->getObjectCount() == 0) {
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Bind instanced pipeline if provided
    if (params.instancedPipeline) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *params.instancedPipeline);
    }

    // Get MaterialRegistry for descriptor set lookup
    const auto& materialRegistry = resources_.scene->getSceneBuilder().getMaterialRegistry();
    const auto& sceneObjects = resources_.scene->getRenderables();

    // For indirect rendering, we need to:
    // 1. Bind the scene instance SSBO descriptor
    // 2. Bind vertex/index buffers per unique mesh
    // 3. Use vkCmdDrawIndexedIndirectCount to draw all visible instances

    // Since culling outputs draw commands sorted by object index (not by mesh),
    // and indirect draws need shared vertex/index buffers,
    // we use a simplified approach: draw all objects with one indirect call per mesh type

    // For now, fall back to regular rendering if indirect is requested but setup is incomplete
    // Full indirect rendering requires:
    // - A global vertex/index buffer with all meshes
    // - Indirect commands that reference offsets into the global buffer
    // - Material binding via SSBO instead of per-draw descriptor sets

    // TODO: Full implementation requires mesh batching infrastructure
    // For this phase, we demonstrate the indirect draw command structure

    // Bind first material's descriptor set (simplified - full implementation needs multi-material support)
    MaterialId firstMaterialId = INVALID_MATERIAL_ID;
    for (const auto& obj : sceneObjects) {
        if (obj.materialId != INVALID_MATERIAL_ID) {
            firstMaterialId = obj.materialId;
            break;
        }
    }

    if (firstMaterialId != INVALID_MATERIAL_ID) {
        VkDescriptorSet descSet = materialRegistry.getDescriptorSet(firstMaterialId, frameIndex);
        if (descSet != VK_NULL_HANDLE) {
            vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     *params.instancedPipelineLayout, 0, vk::DescriptorSet(descSet), {});
        }
    }

    // For demonstration, render each unique mesh with indirect count
    // A full implementation would batch objects by mesh and issue one indirect call per batch

    // Get unique meshes
    std::vector<const Mesh*> uniqueMeshes;
    for (const auto& obj : sceneObjects) {
        if (obj.mesh && std::find(uniqueMeshes.begin(), uniqueMeshes.end(), obj.mesh) == uniqueMeshes.end()) {
            uniqueMeshes.push_back(obj.mesh);
        }
    }

    // Draw each mesh type
    for (const Mesh* mesh : uniqueMeshes) {
        if (!mesh) continue;

        vk::Buffer vertexBuffers[] = {mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        vkCmd.bindVertexBuffers(0, vertexBuffers, offsets);
        vkCmd.bindIndexBuffer(mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);

        // Use vkCmdDrawIndexedIndirectCount for GPU-driven variable draw count
        // The draw count is determined by the culling pass
        vkCmd.drawIndexedIndirectCount(
            sceneBuffer->getIndirectBuffer(frameIndex),  // Indirect command buffer
            0,                                            // Offset to commands
            sceneBuffer->getDrawCountBuffer(frameIndex), // Count buffer
            0,                                            // Offset to count
            sceneBuffer->getObjectCount(),               // Max draw count
            sizeof(GPUDrawIndexedIndirectCommand)        // Stride between commands
        );
    }

    // Note: Trees, rocks, and other subsystems still use their own rendering paths
    // Full GPU-driven rendering would consolidate these into the scene buffer
}
