#include "Application.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>

// Print available performance toggles
static void printUsage(const char* progName) {
    SDL_Log("Usage: %s [options]", progName);
    SDL_Log("");
    SDL_Log("Performance Toggle Options:");
    SDL_Log("  --disable <name>    Disable a specific toggle");
    SDL_Log("  --enable <name>     Enable a specific toggle");
    SDL_Log("  --minimal           Start with minimal rendering (sky + terrain + objects)");
    SDL_Log("  --list-toggles      List all available toggle names");
    SDL_Log("");
    SDL_Log("Toggle names (use with --disable/--enable):");
    SDL_Log("  Compute: terrainCompute, subdivisionCompute, grassCompute, weatherCompute,");
    SDL_Log("           snowCompute, leafCompute, foamCompute, cloudShadowCompute");
    SDL_Log("  HDR Draw: skyDraw, terrainDraw, catmullClarkDraw, sceneObjectsDraw,");
    SDL_Log("            skinnedCharacterDraw, treeEditDraw, grassDraw, waterDraw,");
    SDL_Log("            leavesDraw, weatherDraw, debugLinesDraw");
    SDL_Log("  Shadows: shadowPass, terrainShadows, grassShadows");
    SDL_Log("  Post: hiZPyramid, bloom");
    SDL_Log("  Other: froxelFog, atmosphereLUT, ssr, waterGBuffer, waterTileCull");
    SDL_Log("");
    SDL_Log("Examples:");
    SDL_Log("  %s --disable grassCompute --disable grassDraw", progName);
    SDL_Log("  %s --minimal", progName);
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::vector<std::pair<std::string, bool>> toggleChanges;
    bool minimalMode = false;
    bool listToggles = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--list-toggles") {
            listToggles = true;
        } else if (arg == "--minimal") {
            minimalMode = true;
        } else if (arg == "--disable" && i + 1 < argc) {
            toggleChanges.emplace_back(argv[++i], false);
        } else if (arg == "--enable" && i + 1 < argc) {
            toggleChanges.emplace_back(argv[++i], true);
        }
    }

    if (listToggles) {
        printUsage(argv[0]);
        return 0;
    }

    Application app;

    if (!app.init("Vulkan Game", 1280, 720)) {
        return 1;
    }

    // Apply performance toggle settings after init
    auto& toggles = app.getRenderer().getPerformanceToggles();

    if (minimalMode) {
        SDL_Log("Performance: Starting in minimal mode");
        toggles.disableAll();
        toggles.skyDraw = true;
        toggles.terrainDraw = true;
        toggles.sceneObjectsDraw = true;
    }

    for (const auto& [name, enabled] : toggleChanges) {
        if (toggles.setToggle(name, enabled)) {
            SDL_Log("Performance: %s %s", enabled ? "Enabled" : "Disabled", name.c_str());
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unknown toggle: %s", name.c_str());
        }
    }

    app.run();
    app.shutdown();

    return 0;
}
