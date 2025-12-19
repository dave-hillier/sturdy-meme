#pragma once

#include <string>
#include <vector>
#include <functional>

/**
 * PerformanceToggles - Centralized control for rendering subsystem toggles
 *
 * Provides a unified interface for enabling/disabling render passes
 * to help identify performance bottlenecks and synchronization issues.
 *
 * Toggle categories:
 * - Compute passes (terrain LOD, grass simulation, weather, snow, etc.)
 * - HDR draw calls (sky, terrain, grass, water, etc.)
 * - Shadow rendering
 * - Post-processing (bloom, HiZ)
 * - Other stages (SSR, froxel fog, atmosphere)
 */
struct PerformanceToggles {
    // Compute stage passes
    bool terrainCompute = true;
    bool subdivisionCompute = true;
    bool grassCompute = true;
    bool weatherCompute = true;
    bool snowCompute = true;
    bool leafCompute = true;
    bool foamCompute = true;
    bool cloudShadowCompute = true;

    // HDR stage draw calls
    bool skyDraw = true;
    bool terrainDraw = true;
    bool catmullClarkDraw = true;
    bool sceneObjectsDraw = true;
    bool skinnedCharacterDraw = true;
    bool treeEditDraw = true;
    bool grassDraw = true;
    bool waterDraw = true;
    bool leavesDraw = true;
    bool weatherDraw = true;
    bool debugLinesDraw = true;

    // Shadow rendering
    bool shadowPass = true;
    bool terrainShadows = true;
    bool grassShadows = true;

    // Post-processing
    bool hiZPyramid = true;
    bool bloom = true;

    // Other stages
    bool froxelFog = true;
    bool atmosphereLUT = true;
    bool ssr = true;
    bool waterGBuffer = true;
    bool waterTileCull = true;

    // Synchronization barriers (for debugging sync issues)
    bool enableBarriers = true;

    // Get list of all toggles for UI/command line
    struct Toggle {
        std::string name;
        std::string category;
        bool* value;
    };

    std::vector<Toggle> getAllToggles() {
        return {
            // Compute
            {"terrainCompute", "Compute", &terrainCompute},
            {"subdivisionCompute", "Compute", &subdivisionCompute},
            {"grassCompute", "Compute", &grassCompute},
            {"weatherCompute", "Compute", &weatherCompute},
            {"snowCompute", "Compute", &snowCompute},
            {"leafCompute", "Compute", &leafCompute},
            {"foamCompute", "Compute", &foamCompute},
            {"cloudShadowCompute", "Compute", &cloudShadowCompute},

            // HDR Draw
            {"skyDraw", "HDR Draw", &skyDraw},
            {"terrainDraw", "HDR Draw", &terrainDraw},
            {"catmullClarkDraw", "HDR Draw", &catmullClarkDraw},
            {"sceneObjectsDraw", "HDR Draw", &sceneObjectsDraw},
            {"skinnedCharacterDraw", "HDR Draw", &skinnedCharacterDraw},
            {"treeEditDraw", "HDR Draw", &treeEditDraw},
            {"grassDraw", "HDR Draw", &grassDraw},
            {"waterDraw", "HDR Draw", &waterDraw},
            {"leavesDraw", "HDR Draw", &leavesDraw},
            {"weatherDraw", "HDR Draw", &weatherDraw},
            {"debugLinesDraw", "HDR Draw", &debugLinesDraw},

            // Shadows
            {"shadowPass", "Shadows", &shadowPass},
            {"terrainShadows", "Shadows", &terrainShadows},
            {"grassShadows", "Shadows", &grassShadows},

            // Post-processing
            {"hiZPyramid", "Post", &hiZPyramid},
            {"bloom", "Post", &bloom},

            // Other
            {"froxelFog", "Other", &froxelFog},
            {"atmosphereLUT", "Other", &atmosphereLUT},
            {"ssr", "Other", &ssr},
            {"waterGBuffer", "Other", &waterGBuffer},
            {"waterTileCull", "Other", &waterTileCull},
            {"enableBarriers", "Sync", &enableBarriers},
        };
    }

    // Enable/disable by name (for command line)
    bool setToggle(const std::string& name, bool enabled) {
        for (auto& t : getAllToggles()) {
            if (t.name == name) {
                *t.value = enabled;
                return true;
            }
        }
        return false;
    }

    // Toggle by name
    bool toggle(const std::string& name) {
        for (auto& t : getAllToggles()) {
            if (t.name == name) {
                *t.value = !(*t.value);
                return true;
            }
        }
        return false;
    }

    // Get value by name
    bool getToggle(const std::string& name) const {
        // Need non-const version for iteration
        PerformanceToggles* self = const_cast<PerformanceToggles*>(this);
        for (auto& t : self->getAllToggles()) {
            if (t.name == name) {
                return *t.value;
            }
        }
        return false;
    }

    // Disable all in a category
    void disableCategory(const std::string& category) {
        for (auto& t : getAllToggles()) {
            if (t.category == category) {
                *t.value = false;
            }
        }
    }

    // Enable all in a category
    void enableCategory(const std::string& category) {
        for (auto& t : getAllToggles()) {
            if (t.category == category) {
                *t.value = true;
            }
        }
    }

    // Enable all
    void enableAll() {
        for (auto& t : getAllToggles()) {
            *t.value = true;
        }
    }

    // Disable all (useful for minimal baseline)
    void disableAll() {
        for (auto& t : getAllToggles()) {
            *t.value = false;
        }
    }
};
