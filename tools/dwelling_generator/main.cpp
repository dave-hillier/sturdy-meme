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
    SDL_Log("  --min-size <number>    Minimum cell dimension (default: 3)");
    SDL_Log("  --max-size <number>    Maximum cell dimension (default: 7)");
    SDL_Log("  --room-size <number>   Average room size in cells (default: 6)");
    SDL_Log("  --cell-size <number>   Cell size in pixels (default: 30)");
    SDL_Log("  --windows <0-1>        Window density (default: 0.7)");
    SDL_Log("  --show-grid            Show debug grid lines");
    SDL_Log("  -h, --help             Show this help message");
    SDL_Log(" ");
    SDL_Log("Output files:");
    SDL_Log("  dwelling_floor_N.svg   Floor plan for each floor");
    SDL_Log("  dwelling_all.svg       All floors combined");
    SDL_Log("  dwelling_3d.svg        Orthographic 3D view");
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
        else if (strcmp(argv[i], "--min-size") == 0 && i + 1 < argc) {
            params.minCellSize = std::atoi(argv[++i]);
            if (params.minCellSize < 2) params.minCellSize = 2;
        }
        else if (strcmp(argv[i], "--max-size") == 0 && i + 1 < argc) {
            params.maxCellSize = std::atoi(argv[++i]);
            if (params.maxCellSize < params.minCellSize) params.maxCellSize = params.minCellSize;
        }
        else if (strcmp(argv[i], "--room-size") == 0 && i + 1 < argc) {
            params.avgRoomSize = static_cast<float>(std::atof(argv[++i]));
            if (params.avgRoomSize < 2.0f) params.avgRoomSize = 2.0f;
        }
        else if (strcmp(argv[i], "--cell-size") == 0 && i + 1 < argc) {
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
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unknown option: %s", argv[i]);
        }
    }

    SDL_Log("Dwelling Generator");
    SDL_Log("==================");
    SDL_Log("Seed: %u", params.seed);
    SDL_Log("Floors: %d", params.numFloors);
    SDL_Log("Cell size range: %d-%d", params.minCellSize, params.maxCellSize);
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

    SDL_Log(" ");
    SDL_Log("Done! Output files written to: %s", outputDir.c_str());

    return 0;
}
