#include "town_generator/building/City.h"
#include "town_generator/svg/SVGWriter.h"
#include "town_generator/utils/Random.h"

#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <SDL3/SDL.h>

void printUsage(const char* programName) {
    SDL_Log("Usage: %s [options] <output.svg>", programName);
    SDL_Log("");
    SDL_Log("Options:");
    SDL_Log("  --seed <int>     Random seed (default: random)");
    SDL_Log("  --size <name>    City size: small, medium, large (default: medium)");
    SDL_Log("  --cells <int>  Number of cells (overrides --size)");
    SDL_Log("  --coast          Generate a coastal city with harbour");
    SDL_Log("  --no-coast       Generate an inland city (no water)");
    SDL_Log("  --help           Show this help message");
    SDL_Log("");
    SDL_Log("Examples:");
    SDL_Log("  %s city.svg", programName);
    SDL_Log("  %s --seed 12345 --size large city.svg", programName);
    SDL_Log("  %s --cells 50 --seed 42 --coast city.svg", programName);
}

int main(int argc, char* argv[]) {
    // Default values
    int seed = -1;
    int cells = 30;  // Medium city
    std::string outputFile;
    int coastOverride = -1;  // -1 = use random, 0 = no coast, 1 = force coast

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--seed") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --seed requires a value");
                return 1;
            }
            seed = std::atoi(argv[++i]);
        } else if (arg == "--size") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --size requires a value");
                return 1;
            }
            std::string size = argv[++i];
            if (size == "small") {
                cells = 15;
            } else if (size == "medium") {
                cells = 30;
            } else if (size == "large") {
                cells = 60;
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: Unknown size '%s'. Use small, medium, or large.", size.c_str());
                return 1;
            }
        } else if (arg == "--cells") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --cells requires a value");
                return 1;
            }
            cells = std::atoi(argv[++i]);
            if (cells < 5 || cells > 200) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: cells must be between 5 and 200");
                return 1;
            }
        } else if (arg == "--coast") {
            coastOverride = 1;
        } else if (arg == "--no-coast") {
            coastOverride = 0;
        } else if (arg[0] == '-') {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: Unknown option '%s'", arg.c_str());
            printUsage(argv[0]);
            return 1;
        } else {
            outputFile = arg;
        }
    }

    if (outputFile.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: No output file specified");
        printUsage(argv[0]);
        return 1;
    }

    // Generate seed if not provided
    if (seed == -1) {
        seed = static_cast<int>(std::time(nullptr));
    }

    SDL_Log("Generating city with %d cells, seed %d", cells, seed);

    // Generate the city
    try {
        town_generator::building::City model(cells, seed);

        // Apply coast override if specified
        if (coastOverride == 1) {
            model.coastNeeded = true;
        } else if (coastOverride == 0) {
            model.coastNeeded = false;
        }

        model.build();

        // Write SVG output
        if (town_generator::svg::SVGWriter::write(model, outputFile)) {
            SDL_Log("City generated successfully: %s", outputFile.c_str());
            SDL_Log("Seed: %d (use this seed to regenerate the same city)", seed);
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: Failed to write output file '%s'", outputFile.c_str());
            return 1;
        }
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error generating city: %s", e.what());
        return 1;
    }

    return 0;
}
