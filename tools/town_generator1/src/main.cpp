#include "town_generator/building/Model.h"
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
    SDL_Log("  --patches <int>  Number of patches (overrides --size)");
    SDL_Log("  --help           Show this help message");
    SDL_Log("");
    SDL_Log("Examples:");
    SDL_Log("  %s city.svg", programName);
    SDL_Log("  %s --seed 12345 --size large city.svg", programName);
    SDL_Log("  %s --patches 50 --seed 42 city.svg", programName);
}

int main(int argc, char* argv[]) {
    // Default values
    int seed = -1;
    int patches = 30;  // Medium city
    std::string outputFile;

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
                patches = 15;
            } else if (size == "medium") {
                patches = 30;
            } else if (size == "large") {
                patches = 60;
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: Unknown size '%s'. Use small, medium, or large.", size.c_str());
                return 1;
            }
        } else if (arg == "--patches") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --patches requires a value");
                return 1;
            }
            patches = std::atoi(argv[++i]);
            if (patches < 5 || patches > 200) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: patches must be between 5 and 200");
                return 1;
            }
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

    SDL_Log("Generating city with %d patches, seed %d", patches, seed);

    // Generate the city
    try {
        town_generator::building::Model model(patches, seed);
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
