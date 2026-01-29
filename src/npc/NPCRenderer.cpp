#include "NPCRenderer.h"
#include "NPCSimulation.h"
#include "SkinnedMeshRenderer.h"
#include "RenderableBuilder.h"
#include <SDL3/SDL.h>

NPCRenderer::NPCRenderer(ConstructToken) {}

std::unique_ptr<NPCRenderer> NPCRenderer::create(const InitInfo& info) {
    auto renderer = std::make_unique<NPCRenderer>(ConstructToken{});
    if (!renderer->initInternal(info)) {
        return nullptr;
    }
    return renderer;
}

NPCRenderer::~NPCRenderer() = default;

bool NPCRenderer::initInternal(const InitInfo& info) {
    if (!info.skinnedMeshRenderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "NPCRenderer: skinnedMeshRenderer is required");
        return false;
    }

    skinnedMeshRenderer_ = info.skinnedMeshRenderer;
    return true;
}

void NPCRenderer::prepare(uint32_t frameIndex,
                          NPCSimulation& npcSim,
                          const std::vector<Renderable>& sceneObjects) {
    currentNpcSim_ = &npcSim;
    currentSceneObjects_ = &sceneObjects;
    currentFrameIndex_ = frameIndex;

    // Clear previous frame's render data
    renderData_.clear();

    const auto& npcData = npcSim.getData();
    size_t npcCount = npcData.count();

    if (npcCount == 0) {
        visibleNPCCount_ = 0;
        drawCallCount_ = 0;
        return;
    }

    // Reserve space for render data
    renderData_.reserve(npcCount);

    // Bone slot allocation: slot 0 is reserved for player, NPCs use slots 1+
    // Max slots available = SkinnedMeshRenderer::getMaxSlots() - 1
    uint32_t nextBoneSlot = 1;  // Start at 1 (slot 0 reserved for player)
    const uint32_t maxSlots = SkinnedMeshRenderer::getMaxSlots();

    // Build render data for each visible NPC
    for (size_t i = 0; i < npcCount; ++i) {
        // Skip Virtual LOD NPCs (not rendered)
        if (npcData.lodLevels[i] == NPCLODLevel::Virtual) {
            continue;
        }

        // Skip NPCs without valid renderable
        size_t renderableIndex = npcData.renderableIndices[i];
        if (renderableIndex >= sceneObjects.size()) {
            continue;
        }

        // Skip NPCs without valid character
        auto* character = npcSim.getCharacter(i);
        if (!character) {
            continue;
        }

        // Check if we have slots available
        if (nextBoneSlot >= maxSlots) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "NPCRenderer: Exceeded max character slots (%u), skipping remaining NPCs", maxSlots);
            break;
        }

        // Future: Add frustum culling here
        // if (frustumCullingEnabled_ && !isInFrustum(npcData.positions[i])) {
        //     continue;
        // }

        NPCRenderData data{};
        data.npcIndex = i;
        data.renderableIndex = renderableIndex;
        data.lodLevel = npcData.lodLevels[i];
        data.boneSlot = nextBoneSlot;

        // Update bone matrices for this NPC in its assigned slot
        skinnedMeshRenderer_->updateBoneMatrices(frameIndex, nextBoneSlot, character);

        renderData_.push_back(data);
        nextBoneSlot++;
    }

    visibleNPCCount_ = renderData_.size();
    drawCallCount_ = visibleNPCCount_;  // Currently 1:1, will improve with batching

    // Future optimization: Sort by template/material for batching
    // std::sort(renderData_.begin(), renderData_.end(), [&](const NPCRenderData& a, const NPCRenderData& b) {
    //     return npcData.templateIndices[a.npcIndex] < npcData.templateIndices[b.npcIndex];
    // });
}

void NPCRenderer::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    if (!currentNpcSim_ || !currentSceneObjects_ || !skinnedMeshRenderer_) {
        return;
    }

    // Record draw calls for each visible NPC using their assigned bone slot
    // The dynamic offset in bindDescriptorSets selects the correct bone matrices
    for (const auto& data : renderData_) {
        auto* character = currentNpcSim_->getCharacter(data.npcIndex);
        if (!character) continue;

        const Renderable& npcObj = (*currentSceneObjects_)[data.renderableIndex];
        skinnedMeshRenderer_->record(cmd, frameIndex, data.boneSlot, npcObj, *character);
    }
}
