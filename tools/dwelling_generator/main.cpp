#include "DwellingHouse.h"
#include "DwellingSVG.h"
#include <SDL3/SDL_log.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ctime>

void printUsage(const char* programName) {
    SDL_Log("Usage: %s [options]", programName);
    SDL_Log(" ");
    SDL_Log("Options:");
    SDL_Log("  -o, --output <path>    Output directory (default: current directory)");
    SDL_Log("  -s, --seed <number>    Random seed (default: time-based)");
    SDL_Log("  -f, --floors <number>  Number of floors (default: 1)");
    SDL_Log("  --style <name>         Style: natural, mechanical, organic, gothic");
    SDL_Log("  --building-size <n>    Building footprint section size (default: 3-7)");
    SDL_Log("  --room-size <number>   Average room size in grid cells (default: 6)");
    SDL_Log("  --pixel-size <number>  Grid cell size in pixels for SVG (default: 30)");
    SDL_Log("  --windows <0-1>        Window density (default: 0.7)");
    SDL_Log("  --show-grid            Show debug grid lines");
    SDL_Log("  -h, --help             Show this help message");
    SDL_Log(" ");
    SDL_Log("Styles:");
    SDL_Log("  natural    - Default organic house layout");
    SDL_Log("  mechanical - Regular rectangular rooms");
    SDL_Log("  organic    - Irregular room shapes with variation");
    SDL_Log("  gothic     - Castle-style with chapel, gallery, armoury");
    SDL_Log(" ");
    SDL_Log("Output files:");
    SDL_Log("  dwelling_floor_N.svg   Floor plan for each floor");
    SDL_Log("  dwelling_all.svg       All floors combined");
    SDL_Log("  dwelling_3d.svg        Orthographic 3D view");
    SDL_Log("  dwelling_facade.svg    Front elevation view");
}

int main(int argc, char* argv[]) {
    // Default parameters
    dwelling::DwellingParams params;
    dwelling::RenderOptions renderOptions;
    std::string outputDir = ".";

    params.seed = static_cast<uint32_t>(std::time(nullptr));

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc) {
            outputDir = argv[++i];
        }
        else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--seed") == 0) && i + 1 < argc) {
            params.seed = static_cast<uint32_t>(std::atoi(argv[++i]));
        }
        else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--floors") == 0) && i + 1 < argc) {
            params.numFloors = std::atoi(argv[++i]);
            if (params.numFloors < 1) params.numFloors = 1;
            if (params.numFloors > 6) params.numFloors = 6;
        }
        else if (strcmp(argv[i], "--building-size") == 0 && i + 1 < argc) {
            // Parse "min-max" or just "size"
            const char* arg = argv[++i];
            char* dash = const_cast<char*>(strchr(arg, '-'));
            if (dash) {
                *dash = '\0';
                params.minSectionSize = std::atoi(arg);
                params.maxSectionSize = std::atoi(dash + 1);
                *dash = '-';  // Restore
            } else {
                int size = std::atoi(arg);
                params.minSectionSize = size;
                params.maxSectionSize = size;
            }
            if (params.minSectionSize < 2) params.minSectionSize = 2;
            if (params.maxSectionSize < params.minSectionSize) params.maxSectionSize = params.minSectionSize;
        }
        else if (strcmp(argv[i], "--room-size") == 0 && i + 1 < argc) {
            params.avgRoomSize = static_cast<float>(std::atof(argv[++i]));
            if (params.avgRoomSize < 2.0f) params.avgRoomSize = 2.0f;
        }
        else if (strcmp(argv[i], "--pixel-size") == 0 && i + 1 < argc) {
            renderOptions.cellSize = static_cast<float>(std::atof(argv[++i]));
            if (renderOptions.cellSize < 10.0f) renderOptions.cellSize = 10.0f;
        }
        else if (strcmp(argv[i], "--windows") == 0 && i + 1 < argc) {
            params.windowDensity = static_cast<float>(std::atof(argv[++i]));
            if (params.windowDensity < 0.0f) params.windowDensity = 0.0f;
            if (params.windowDensity > 1.0f) params.windowDensity = 1.0f;
        }
        else if (strcmp(argv[i], "--show-grid") == 0) {
            renderOptions.showGrid = true;
        }
        else if (strcmp(argv[i], "--style") == 0 && i + 1 < argc) {
            ++i;
            if (strcmp(argv[i], "natural") == 0) {
                params.style = dwelling::DwellingStyle::Natural;
            } else if (strcmp(argv[i], "mechanical") == 0) {
                params.style = dwelling::DwellingStyle::Mechanical;
            } else if (strcmp(argv[i], "organic") == 0) {
                params.style = dwelling::DwellingStyle::Organic;
            } else if (strcmp(argv[i], "gothic") == 0) {
                params.style = dwelling::DwellingStyle::Gothic;
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unknown style: %s", argv[i]);
            }
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unknown option: %s", argv[i]);
        }
    }

    const char* styleName = "natural";
    switch (params.style) {
        case dwelling::DwellingStyle::Mechanical: styleName = "mechanical"; break;
        case dwelling::DwellingStyle::Organic: styleName = "organic"; break;
        case dwelling::DwellingStyle::Gothic: styleName = "gothic"; break;
        default: break;
    }

    SDL_Log("Dwelling Generator");
    SDL_Log("==================");
    SDL_Log("Seed: %u", params.seed);
    SDL_Log("Floors: %d", params.numFloors);
    SDL_Log("Style: %s", styleName);
    SDL_Log("Building size: %d-%d", params.minSectionSize, params.maxSectionSize);
    SDL_Log("Average room size: %.1f cells", params.avgRoomSize);
    SDL_Log("Window density: %.0f%%", params.windowDensity * 100);
    SDL_Log(" ");

    // Generate the house
    dwelling::House house(params);
    house.generate();

    SDL_Log("Generated: %s", house.name().c_str());
    SDL_Log("Grid size: %d x %d cells", house.gridWidth(), house.gridHeight());

    // Write floor plan for each floor
    for (int f = 0; f < house.numFloors(); ++f) {
        const dwelling::Plan* plan = house.floor(f);
        if (plan) {
            SDL_Log("Floor %d: %zu rooms, %zu doors, %zu windows",
                f, plan->rooms().size(), plan->doors().size(), plan->windows().size());

            std::string filename = outputDir + "/dwelling_floor_" + std::to_string(f) + ".svg";
            dwelling::writeFloorPlanSVG(filename, house, f, renderOptions);
        }
    }

    // Write combined floors view
    {
        std::string filename = outputDir + "/dwelling_all.svg";
        dwelling::writeAllFloorsSVG(filename, house, renderOptions);
    }

    // Write 3D view
    {
        std::string filename = outputDir + "/dwelling_3d.svg";
        dwelling::writeOrthoViewSVG(filename, house, renderOptions);
    }

    // Write facade/elevation view
    {
        std::string filename = outputDir + "/dwelling_facade.svg";
        dwelling::writeFacadeViewSVG(filename, house, renderOptions);
    }

    SDL_Log(" ");
    SDL_Log("Done! Output files written to: %s", outputDir.c_str());

    return 0;
}
