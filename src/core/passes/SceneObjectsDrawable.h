#pragma once

// ============================================================================
// SceneObjectsDrawable.h - Scene object rendering as IHDRDrawable
// ============================================================================
//
// Encapsulates all scene object rendering: static meshes (ECS and legacy),
// procedural rocks and detritus, and tree rendering with LOD impostors.
//

#include "interfaces/IHDRDrawable.h"

// Forward declarations
class SceneManager;
class GlobalBufferManager;
class ShadowSystem;
class WindSystem;
class ScatterSystem;
class TreeSystem;
class TreeRenderer;
class TreeLODSystem;
class ImpostorCullSystem;
class GPUSceneBuffer;

namespace ecs { class World; }

struct VegetationSystemGroup;

/**
 * Renders scene objects in the HDR pass.
 *
 * Handles:
 * - Static mesh rendering (ECS entities or legacy Renderables)
 * - Material sorting for minimal descriptor set switches
 * - Procedural rocks and detritus (ScatterSystem)
 * - Tree rendering with wind animation (TreeRenderer)
 * - Tree impostor rendering with GPU culling (TreeLODSystem)
 * - GPU-driven indirect rendering path (Phase 3.3)
 */
class SceneObjectsDrawable : public IHDRDrawable {
public:
    struct Resources {
        SceneManager* scene = nullptr;
        GlobalBufferManager* globalBuffers = nullptr;
        ShadowSystem* shadow = nullptr;
        WindSystem* wind = nullptr;
        ecs::World* ecsWorld = nullptr;

        // Vegetation subsystems (optional)
        ScatterSystem* rocks = nullptr;
        ScatterSystem* detritus = nullptr;
        TreeSystem* tree = nullptr;
        TreeRenderer* treeRenderer = nullptr;
        TreeLODSystem* treeLOD = nullptr;
        ImpostorCullSystem* impostorCull = nullptr;
        bool visBufferActive = false;
    };

    explicit SceneObjectsDrawable(const Resources& resources);

    void recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                        float time, const HDRDrawParams& params) override;

private:
    void recordSceneObjects(VkCommandBuffer cmd, uint32_t frameIndex, const HDRDrawParams& params);
    void recordSceneObjectsIndirect(VkCommandBuffer cmd, uint32_t frameIndex, const HDRDrawParams& params);

    Resources resources_;
};
