#include "ShadowPassRecorder.h"
#include "../RendererSystems.h"
#include "../PerformanceToggles.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "GrassSystem.h"
#include "TreeSystem.h"
#include "TreeRenderer.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "RockSystem.h"
#include "DetritusSystem.h"
#include "SkinnedMeshRenderer.h"
#include "GlobalBufferManager.h"
#include "SceneManager.h"
#include "scene/SceneBuilder.h"
#include "Profiler.h"
#include "AnimatedCharacter.h"
#include "SkinnedMesh.h"
#include "FrustumUtils.h"

ShadowPassRecorder::ShadowPassRecorder(RendererSystems& systems)
    : systems_(systems)
{
}

void ShadowPassRecorder::record(VkCommandBuffer cmd, uint32_t frameIndex, float time, const glm::vec3& cameraPosition) {
    // Setup phase: build callbacks and collect shadow-casting objects
    systems_.profiler().beginCpuZone("Shadow:Setup");

    // Delegate to the shadow system with callbacks for terrain and grass
    auto terrainCallback = [this, frameIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (config_.terrainEnabled && config_.perfToggles && config_.perfToggles->terrainShadows) {
            systems_.profiler().beginGpuZone(cb, "Shadow:Terrain");
            systems_.terrain().recordShadowDraw(cb, frameIndex, lightMatrix, static_cast<int>(cascade));
            systems_.profiler().endGpuZone(cb, "Shadow:Terrain");
        }
    };

    auto grassCallback = [this, frameIndex, time](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;  // Grass uses cascade index only
        if (config_.perfToggles && config_.perfToggles->grassShadows) {
            systems_.profiler().beginGpuZone(cb, "Shadow:Grass");
            systems_.grass().recordShadowDraw(cb, frameIndex, time, cascade);
            systems_.profiler().endGpuZone(cb, "Shadow:Grass");
        }
    };

    auto treeCallback = [this, frameIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;
        if (systems_.tree() && systems_.treeRenderer()) {
            systems_.profiler().beginGpuZone(cb, "Shadow:Trees");
            systems_.treeRenderer()->renderShadows(cb, frameIndex, *systems_.tree(), static_cast<int>(cascade), systems_.treeLOD());
            systems_.profiler().endGpuZone(cb, "Shadow:Trees");
        }
        // Render impostor shadows
        if (systems_.treeLOD()) {
            systems_.profiler().beginGpuZone(cb, "Shadow:Impostors");
            VkBuffer uniformBuffer = systems_.globalBuffers().uniformBuffers.buffers[frameIndex];
            auto* impostorCull = systems_.impostorCull();
            if (impostorCull && impostorCull->getTreeCount() > 0) {
                // Use GPU-culled indirect rendering
                systems_.treeLOD()->renderImpostorShadowsGPUCulled(
                    cb, frameIndex, static_cast<int>(cascade), uniformBuffer,
                    impostorCull->getVisibleImpostorBuffer(),
                    impostorCull->getIndirectDrawBuffer()
                );
            } else {
                // Fall back to CPU-culled rendering
                systems_.treeLOD()->renderImpostorShadows(cb, frameIndex, static_cast<int>(cascade), uniformBuffer);
            }
            systems_.profiler().endGpuZone(cb, "Shadow:Impostors");
        }
    };

    // Combine scene objects and rock objects for shadow rendering
    // Skip player character - it's rendered separately with skinned shadow pipeline
    std::vector<Renderable> allObjects;
    const auto& sceneObjects = systems_.scene().getRenderables();
    size_t playerIndex = systems_.scene().getSceneBuilder().getPlayerObjectIndex();
    bool hasCharacter = systems_.scene().getSceneBuilder().hasCharacter();

    size_t detritusCount = systems_.detritus() ? systems_.detritus()->getSceneObjects().size() : 0;
    size_t rockCount = systems_.rock().getSceneObjects().size();
    allObjects.reserve(sceneObjects.size() + rockCount + detritusCount);
    for (size_t i = 0; i < sceneObjects.size(); ++i) {
        // Skip player character - rendered with skinned shadow pipeline
        if (hasCharacter && i == playerIndex) {
            continue;
        }
        allObjects.push_back(sceneObjects[i]);
    }
    allObjects.insert(allObjects.end(), systems_.rock().getSceneObjects().begin(), systems_.rock().getSceneObjects().end());
    if (systems_.detritus()) {
        allObjects.insert(allObjects.end(), systems_.detritus()->getSceneObjects().begin(), systems_.detritus()->getSceneObjects().end());
    }

    // Skinned character shadow callback (renders with GPU skinning)
    ShadowSystem::DrawCallback skinnedCallback = nullptr;
    if (hasCharacter) {
        skinnedCallback = [this, frameIndex, playerIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
            (void)lightMatrix;  // Not used, cascade matrices are in UBO
            SceneBuilder& sceneBuilder = systems_.scene().getSceneBuilder();
            const auto& sceneObjs = sceneBuilder.getRenderables();
            if (playerIndex >= sceneObjs.size()) return;

            systems_.profiler().beginGpuZone(cb, "Shadow:Skinned");
            const Renderable& playerObj = sceneObjs[playerIndex];
            AnimatedCharacter& character = sceneBuilder.getAnimatedCharacter();
            SkinnedMesh& skinnedMesh = character.getSkinnedMesh();

            // Bind skinned shadow pipeline with descriptor set that has bone matrices
            systems_.shadow().bindSkinnedShadowPipeline(cb, systems_.skinnedMesh().getDescriptorSet(frameIndex));

            // Record the skinned mesh shadow
            systems_.shadow().recordSkinnedMeshShadow(cb, cascade, playerObj.transform, skinnedMesh);
            systems_.profiler().endGpuZone(cb, "Shadow:Skinned");
        };
    }

    // Pre-cascade compute callback for GPU culling (runs before each cascade's render pass)
    ShadowSystem::ComputeCallback preCascadeComputeCallback = [this, cameraPosition](
        VkCommandBuffer cb, uint32_t frame, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (systems_.treeRenderer() && systems_.tree() && systems_.treeLOD()) {
            // Extract frustum planes from the light view-projection matrix
            glm::vec4 cascadeFrustumPlanes[6];
            extractFrustumPlanes(lightMatrix, cascadeFrustumPlanes);

            // Record GPU culling pass for branch shadows
            systems_.treeRenderer()->recordBranchShadowCulling(
                cb, frame, cascade, cascadeFrustumPlanes, cameraPosition, systems_.treeLOD());
        }
    };

    // Use any MaterialRegistry descriptor set for shadow pass (only needs common bindings/UBO)
    // MaterialId 0 is the first registered material (crate)
    const auto& materialRegistry = systems_.scene().getSceneBuilder().getMaterialRegistry();
    VkDescriptorSet shadowDescriptorSet = materialRegistry.getDescriptorSet(0, frameIndex);

    systems_.profiler().endCpuZone("Shadow:Setup");

    // Record all shadow cascades
    systems_.profiler().beginCpuZone("Shadow:Cascades");
    systems_.shadow().recordShadowPass(cmd, frameIndex, shadowDescriptorSet,
                                       allObjects,
                                       terrainCallback, grassCallback, treeCallback, skinnedCallback,
                                       preCascadeComputeCallback);
    systems_.profiler().endCpuZone("Shadow:Cascades");
}
