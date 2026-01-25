#pragma once

#include "NPCData.h"
#include "core/interfaces/IRecordable.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>

class NPCSimulation;
class SkinnedMeshRenderer;
class Renderable;

// NPC Renderer - Handles batched draw commands for NPCs
// Implements IRecordable for integration with render pass system
class NPCRenderer : public IRecordable {
public:
    // Passkey for controlled construction
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit NPCRenderer(ConstructToken);

    struct InitInfo {
        SkinnedMeshRenderer* skinnedMeshRenderer;  // Shared skinned mesh renderer
    };

    /**
     * Factory: Create NPCRenderer.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<NPCRenderer> create(const InitInfo& info);

    ~NPCRenderer();

    // Non-copyable, non-movable
    NPCRenderer(const NPCRenderer&) = delete;
    NPCRenderer& operator=(const NPCRenderer&) = delete;
    NPCRenderer(NPCRenderer&&) = delete;
    NPCRenderer& operator=(NPCRenderer&&) = delete;

    /**
     * Prepare render data for the frame.
     * Call before recordDraw to update NPC render state.
     *
     * @param frameIndex Current frame index for triple-buffered resources
     * @param npcSim The NPC simulation (non-const as we need character access for rendering)
     * @param sceneObjects Scene objects containing NPC renderables
     */
    void prepare(uint32_t frameIndex,
                 NPCSimulation& npcSim,
                 const std::vector<Renderable>& sceneObjects);

    /**
     * Record draw commands to the command buffer.
     * IRecordable interface implementation.
     *
     * @param cmd Command buffer to record to (must be in recording state)
     * @param frameIndex Current frame index for triple-buffered resources
     */
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) override;

    // Statistics
    size_t getVisibleNPCCount() const { return visibleNPCCount_; }
    size_t getDrawCallCount() const { return drawCallCount_; }

    // Visibility culling (future: frustum culling, occlusion)
    void setFrustumCullingEnabled(bool enabled) { frustumCullingEnabled_ = enabled; }
    bool isFrustumCullingEnabled() const { return frustumCullingEnabled_; }

private:
    bool initInternal(const InitInfo& info);

    // Render data prepared each frame
    struct NPCRenderData {
        size_t npcIndex;           // Index into NPCSimulation
        size_t renderableIndex;    // Index into sceneObjects
        NPCLODLevel lodLevel;      // Current LOD level
    };

    SkinnedMeshRenderer* skinnedMeshRenderer_ = nullptr;

    // Per-frame render data
    std::vector<NPCRenderData> renderData_;
    NPCSimulation* currentNpcSim_ = nullptr;
    const std::vector<Renderable>* currentSceneObjects_ = nullptr;
    uint32_t currentFrameIndex_ = 0;

    // Statistics
    size_t visibleNPCCount_ = 0;
    size_t drawCallCount_ = 0;

    // Options
    bool frustumCullingEnabled_ = false;  // Future: frustum culling
};
