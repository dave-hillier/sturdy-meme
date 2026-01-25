#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>

// =============================================================================
// NPC Render Data
// =============================================================================
// Thread-safe data structure for rendering NPCs.
// This decouples NPC simulation from rendering, allowing:
// - NPC logic to run on a separate thread
// - Future server-side NPC simulation
// - Clean separation of concerns
// =============================================================================

// Render data for a single NPC instance
struct NPCRenderInstance {
    glm::mat4 modelMatrix;      // World transform with scale applied
    glm::vec4 tintColor;        // Hostility color tint (RGBA)
    uint32_t boneSlot;          // Slot in bone matrices buffer
    bool visible;               // Should this NPC be rendered?

    NPCRenderInstance()
        : modelMatrix(1.0f)
        , tintColor(1.0f)
        , boneSlot(0)
        , visible(false) {}
};

// Complete render data for all NPCs in a frame
// This is produced by NPCManager and consumed by the renderer
struct NPCRenderData {
    std::vector<NPCRenderInstance> instances;

    // Statistics for debugging
    uint32_t totalCount = 0;
    uint32_t visibleCount = 0;
    uint32_t virtualCount = 0;
    uint32_t bulkCount = 0;
    uint32_t realCount = 0;

    void clear() {
        instances.clear();
        totalCount = 0;
        visibleCount = 0;
        virtualCount = 0;
        bulkCount = 0;
        realCount = 0;
    }

    void reserve(size_t count) {
        instances.reserve(count);
    }
};

// Configuration for NPC rendering (passed to NPCManager)
struct NPCRenderConfig {
    float characterScale = 0.01f;  // Scale factor for Mixamo characters
    bool debugForceVisible = false; // Force all NPCs to render (for debugging)
};
