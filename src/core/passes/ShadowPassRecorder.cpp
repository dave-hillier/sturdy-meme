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
#include "ScatterSystem.h"
#include "SkinnedMeshRenderer.h"
#include "GlobalBufferManager.h"
#include "SceneManager.h"
#include "scene/SceneBuilder.h"
#include "Profiler.h"
#include "AnimatedCharacter.h"
#include "SkinnedMesh.h"
#include "CullCommon.h"  // For extractFrustumPlanes

// ECS includes for Phase 6 rendering
#include "ecs/World.h"
#include "ecs/Components.h"

ShadowPassRecorder::ShadowPassRecorder(const ShadowPassResources& resources)
    : resources_(resources)
{
}

ShadowPassRecorder::ShadowPassRecorder(RendererSystems& systems)
    : resources_(ShadowPassResources::collect(systems))
{
}

void ShadowPassRecorder::record(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                                const glm::vec3& cameraPosition, const Params& params) {
    // Setup phase: build callbacks and collect shadow-casting objects
    resources_.profiler->beginCpuZone("Shadow:Setup");

    // Delegate to the shadow system with callbacks for terrain and grass
    auto terrainCallback = [this, &params, frameIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (params.terrainEnabled && params.terrainShadows) {
            resources_.profiler->beginGpuZone(cb, "Shadow:Terrain");
            resources_.terrain->recordShadowDraw(cb, frameIndex, lightMatrix, static_cast<int>(cascade));
            resources_.profiler->endGpuZone(cb, "Shadow:Terrain");
        }
    };

    auto grassCallback = [this, &params, frameIndex, time](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;  // Grass uses cascade index only
        if (params.grassShadows) {
            resources_.profiler->beginGpuZone(cb, "Shadow:Grass");
            resources_.vegetation.grass().recordShadowDraw(cb, frameIndex, time, static_cast<int>(cascade));
            resources_.profiler->endGpuZone(cb, "Shadow:Grass");
        }
    };

    auto treeCallback = [this, frameIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
        (void)lightMatrix;
        if (resources_.vegetation.hasTree() && resources_.vegetation.hasTreeRenderer()) {
            resources_.profiler->beginGpuZone(cb, "Shadow:Trees");
            resources_.vegetation.treeRenderer()->renderShadows(cb, frameIndex, *resources_.vegetation.tree(), static_cast<int>(cascade), resources_.vegetation.treeLOD());
            resources_.profiler->endGpuZone(cb, "Shadow:Trees");
        }
        // Render impostor shadows
        if (resources_.vegetation.hasTreeLOD()) {
            resources_.profiler->beginGpuZone(cb, "Shadow:Impostors");
            VkBuffer uniformBuffer = resources_.globalBuffers->uniformBuffers.buffers[frameIndex];
            auto* impostorCull = resources_.vegetation.impostorCull();
            if (impostorCull && impostorCull->getTreeCount() > 0) {
                // Use GPU-culled indirect rendering
                resources_.vegetation.treeLOD()->renderImpostorShadowsGPUCulled(
                    cb, frameIndex, static_cast<int>(cascade), uniformBuffer,
                    impostorCull->getVisibleImpostorBuffer(),
                    impostorCull->getIndirectDrawBuffer()
                );
            } else {
                // Fall back to CPU-culled rendering
                resources_.vegetation.treeLOD()->renderImpostorShadows(cb, frameIndex, static_cast<int>(cascade), uniformBuffer);
            }
            resources_.profiler->endGpuZone(cb, "Shadow:Impostors");
        }
    };

    // Combine scene objects and rock objects for shadow rendering
    // Skip player character - it's rendered separately with skinned shadow pipeline
    std::vector<Renderable> allObjects;
    bool hasCharacter = resources_.scene->getSceneBuilder().hasCharacter();

    size_t detritusCount = resources_.vegetation.hasDetritus() ? resources_.vegetation.detritus()->getSceneObjects().size() : 0;
    size_t rockCount = resources_.vegetation.rocks().getSceneObjects().size();

    // Phase 6: Use ECS if available, otherwise fall back to legacy renderables
    if (resources_.ecsWorld) {
        ecs::World& world = *resources_.ecsWorld;

        // Collect shadow-casting entities from ECS
        allObjects.reserve(256 + rockCount + detritusCount);

        for (auto [entity, meshRef, materialRef] : world.view<ecs::MeshRef, ecs::MaterialRef>().each()) {
            // Skip entities rendered by specialized systems
            if (world.has<ecs::PlayerTag>(entity)) continue;   // Skinned mesh renderer
            if (world.has<ecs::NPCTag>(entity)) continue;      // NPC renderer (handled separately)
            if (world.has<ecs::TreeData>(entity)) continue;    // Tree renderer

            // Only include shadow-casting entities
            if (!world.has<ecs::CastsShadow>(entity)) continue;

            // Extract render data and create a Renderable for shadow pass
            ecs::RenderData data = ecs::extractRenderData(world, entity);
            if (data.mesh && data.materialId != ecs::InvalidMaterialId) {
                Renderable r;
                r.transform = data.transform;
                r.mesh = data.mesh;
                r.materialId = data.materialId;
                r.roughness = data.roughness;
                r.metallic = data.metallic;
                r.emissiveIntensity = data.emissiveIntensity;
                r.emissiveColor = data.emissiveColor;
                r.alphaTestThreshold = data.alphaTestThreshold;
                r.pbrFlags = data.pbrFlags;
                r.castsShadow = true;
                r.opacity = data.opacity;
                allObjects.push_back(r);
            }
        }
    } else {
        // Legacy path: Use Renderable vector
        const auto& sceneObjects = resources_.scene->getRenderables();
        size_t playerIndex = resources_.scene->getSceneBuilder().getPlayerObjectIndex();

        allObjects.reserve(sceneObjects.size() + rockCount + detritusCount);
        for (size_t i = 0; i < sceneObjects.size(); ++i) {
            // Skip player character - rendered with skinned shadow pipeline
            if (hasCharacter && i == playerIndex) {
                continue;
            }
            allObjects.push_back(sceneObjects[i]);
        }
    }

    // Add rocks and detritus (these still use legacy Renderable)
    allObjects.insert(allObjects.end(), resources_.vegetation.rocks().getSceneObjects().begin(), resources_.vegetation.rocks().getSceneObjects().end());
    if (resources_.vegetation.hasDetritus()) {
        allObjects.insert(allObjects.end(), resources_.vegetation.detritus()->getSceneObjects().begin(), resources_.vegetation.detritus()->getSceneObjects().end());
    }

    // Skinned character shadow callback (renders with GPU skinning)
    ShadowSystem::DrawCallback skinnedCallback = nullptr;
    if (hasCharacter) {
        skinnedCallback = [this, frameIndex, playerIndex](VkCommandBuffer cb, uint32_t cascade, const glm::mat4& lightMatrix) {
            (void)lightMatrix;  // Not used, cascade matrices are in UBO
            SceneBuilder& sceneBuilder = resources_.scene->getSceneBuilder();
            const auto& sceneObjs = sceneBuilder.getRenderables();
            if (playerIndex >= sceneObjs.size()) return;

            resources_.profiler->beginGpuZone(cb, "Shadow:Skinned");
            const Renderable& playerObj = sceneObjs[playerIndex];
            AnimatedCharacter& character = sceneBuilder.getAnimatedCharacter();
            SkinnedMesh& skinnedMesh = character.getSkinnedMesh();

            // Bind skinned shadow pipeline with descriptor set that has bone matrices
            resources_.shadow->bindSkinnedShadowPipeline(cb, resources_.skinnedMesh->getDescriptorSet(frameIndex));

            // Record the skinned mesh shadow
            resources_.shadow->recordSkinnedMeshShadow(cb, cascade, playerObj.transform, skinnedMesh);
            resources_.profiler->endGpuZone(cb, "Shadow:Skinned");
        };
    }

    // Pre-cascade compute callback for GPU culling (runs before each cascade's render pass)
    ShadowSystem::ComputeCallback preCascadeComputeCallback = [this, cameraPosition](
        VkCommandBuffer cb, uint32_t frame, uint32_t cascade, const glm::mat4& lightMatrix) {
        if (resources_.vegetation.hasTreeRenderer() && resources_.vegetation.hasTree() && resources_.vegetation.hasTreeLOD()) {
            // Extract frustum planes from the light view-projection matrix
            glm::vec4 cascadeFrustumPlanes[6];
            extractFrustumPlanes(lightMatrix, cascadeFrustumPlanes);

            // Record GPU culling pass for branch shadows
            resources_.vegetation.treeRenderer()->recordBranchShadowCulling(
                cb, frame, cascade, cascadeFrustumPlanes, cameraPosition, resources_.vegetation.treeLOD());
        }
    };

    // Use any MaterialRegistry descriptor set for shadow pass (only needs common bindings/UBO)
    // MaterialId 0 is the first registered material (crate)
    const auto& materialRegistry = resources_.scene->getSceneBuilder().getMaterialRegistry();
    VkDescriptorSet shadowDescriptorSet = materialRegistry.getDescriptorSet(0, frameIndex);

    resources_.profiler->endCpuZone("Shadow:Setup");

    // Record all shadow cascades
    resources_.profiler->beginCpuZone("Shadow:Cascades");
    resources_.shadow->recordShadowPass(cmd, frameIndex, shadowDescriptorSet,
                                       allObjects,
                                       terrainCallback, grassCallback, treeCallback, skinnedCallback,
                                       preCascadeComputeCallback);
    resources_.profiler->endCpuZone("Shadow:Cascades");
}

// Legacy API implementation (deprecated)
void ShadowPassRecorder::record(VkCommandBuffer cmd, uint32_t frameIndex, float time, const glm::vec3& cameraPosition) {
    // Convert legacy config to new params
    Params params;
    params.terrainEnabled = legacyConfig_.terrainEnabled;
    if (legacyConfig_.perfToggles) {
        params.terrainShadows = legacyConfig_.perfToggles->terrainShadows;
        params.grassShadows = legacyConfig_.perfToggles->grassShadows;
    }

    // Call the stateless version
    record(cmd, frameIndex, time, cameraPosition, params);
}
