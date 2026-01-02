// Medieval Fantasy City Generator
// Ported from watabou's TownGeneratorOS (https://github.com/watabou/TownGeneratorOS)
//
// Generates procedural medieval city layouts with:
// - Voronoi-based district (ward) tessellation
// - City walls with gates and towers
// - Building footprints by ward type
// - Street network connecting gates to center
// - Tree placement for parks and farms
//
// Output formats:
// - GeoJSON for integration with rendering pipeline
// - SVG for quick visual preview

#include "Model.h"
#include "CityOutput.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <output_dir> [options]\n"
              << "\n"
              << "Generates a procedural medieval fantasy city layout.\n"
              << "\n"
              << "Arguments:\n"
              << "  output_dir              Directory for output files\n"
              << "\n"
              << "Options:\n"
              << "  --seed <value>          Random seed (0 = random, default: 0)\n"
              << "  --radius <value>        City radius in units (default: 100.0)\n"
              << "  --patches <value>       Number of ward patches (default: 30)\n"
              << "  --no-walls              Disable city walls\n"
              << "  --citadel               Add inner citadel\n"
              << "  --no-plaza              Disable central plaza\n"
              << "  --no-temple             Disable cathedral/temple\n"
              << "  --no-castle             Disable castle\n"
              << "  --river                 Add river flowing through city\n"
              << "  --coastal               Make city coastal with piers\n"
              << "  --coast-dir <degrees>   Direction to coast (0=east, 90=north, default: 0)\n"
              << "  --river-width <value>   River width (default: 5.0)\n"
              << "  --piers <value>         Number of piers for coastal cities (default: 3)\n"
              << "  --tree-density <value>  Tree density multiplier (default: 1.0)\n"
              << "  --svg-width <value>     SVG output width (default: 1024)\n"
              << "  --svg-height <value>    SVG output height (default: 1024)\n"
              << "  --help                  Show this help message\n"
              << "\n"
              << "Output files:\n"
              << "  city.geojson   GeoJSON with all city features\n"
              << "  city.svg       SVG preview image\n"
              << "\n"
              << "GeoJSON layers:\n"
              << "  boundary    - City border polygon\n"
              << "  wards       - Ward boundary polygons with type/color properties\n"
              << "  buildings   - Building footprint polygons\n"
              << "  walls       - Wall perimeter polygons\n"
              << "  towers      - Tower point features\n"
              << "  gates       - Gate point features\n"
              << "  streets     - Street/road line features\n"
              << "  plaza       - Central plaza polygon\n"
              << "  trees       - Tree point features\n"
              << "  water       - Rivers, ponds, and coast polygons\n"
              << "  bridges     - Bridge polygons over water\n"
              << "  piers       - Pier polygons extending into water\n"
              << "\n"
              << "Ward types:\n"
              << "  castle, cathedral, market, patriciate, craftsmen,\n"
              << "  merchants, administration, military, slum, farm, park, gate\n"
              << "\n"
              << "Examples:\n"
              << "  " << programName << " ./output --seed 42 --patches 40\n"
              << "  " << programName << " ./output --citadel --tree-density 2.0\n"
              << "  " << programName << " ./output --river --seed 123\n"
              << "  " << programName << " ./output --coastal --coast-dir 90 --piers 5\n"
              << "  " << programName << " ./output --river --coastal --coast-dir 45\n";
}

int main(int argc, char* argv[]) {
    // Check for help flag first
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string outputDir = argv[1];

    // Default parameters
    city::CityParams params;
    float treeDensity = 1.0f;
    int svgWidth = 1024;
    int svgHeight = 1024;

    // Parse optional arguments
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--seed" && i + 1 < argc) {
            params.seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--radius" && i + 1 < argc) {
            params.radius = std::stof(argv[++i]);
        } else if (arg == "--patches" && i + 1 < argc) {
            params.numPatches = std::stoi(argv[++i]);
        } else if (arg == "--no-walls") {
            params.hasWalls = false;
        } else if (arg == "--citadel") {
            params.hasCitadel = true;
        } else if (arg == "--no-plaza") {
            params.hasPlaza = false;
        } else if (arg == "--no-temple") {
            params.hasTemple = false;
        } else if (arg == "--no-castle") {
            params.hasCastle = false;
        } else if (arg == "--river") {
            params.hasRiver = true;
        } else if (arg == "--coastal") {
            params.hasCoast = true;
        } else if (arg == "--coast-dir" && i + 1 < argc) {
            params.coastDirection = std::stof(argv[++i]) * 3.14159265f / 180.0f;  // Convert degrees to radians
        } else if (arg == "--river-width" && i + 1 < argc) {
            params.riverWidth = std::stof(argv[++i]);
        } else if (arg == "--piers" && i + 1 < argc) {
            params.numPiers = std::stoi(argv[++i]);
        } else if (arg == "--tree-density" && i + 1 < argc) {
            treeDensity = std::stof(argv[++i]);
        } else if (arg == "--svg-width" && i + 1 < argc) {
            svgWidth = std::stoi(argv[++i]);
        } else if (arg == "--svg-height" && i + 1 < argc) {
            svgHeight = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Create output directory if it doesn't exist
    std::filesystem::create_directories(outputDir);

    SDL_Log("Medieval Fantasy City Generator");
    SDL_Log("================================");
    SDL_Log("Output: %s", outputDir.c_str());
    SDL_Log("Seed: %u", params.seed);
    SDL_Log("Radius: %.1f", params.radius);
    SDL_Log("Patches: %d", params.numPatches);
    SDL_Log("Walls: %s", params.hasWalls ? "yes" : "no");
    SDL_Log("Citadel: %s", params.hasCitadel ? "yes" : "no");
    SDL_Log("Plaza: %s", params.hasPlaza ? "yes" : "no");
    SDL_Log("Temple: %s", params.hasTemple ? "yes" : "no");
    SDL_Log("Castle: %s", params.hasCastle ? "yes" : "no");
    SDL_Log("River: %s", params.hasRiver ? "yes" : "no");
    SDL_Log("Coastal: %s", params.hasCoast ? "yes" : "no");
    if (params.hasCoast) {
        SDL_Log("Coast direction: %.0f degrees", params.coastDirection * 180.0f / 3.14159265f);
        SDL_Log("Piers: %d", params.numPiers);
    }
    SDL_Log("Tree density: %.1f", treeDensity);

    // Generate city
    SDL_Log("Generating city...");
    city::Model model;
    model.generate(params);

    // Report statistics
    SDL_Log("Generated city with:");
    SDL_Log("  %zu patches", model.patches.size());
    SDL_Log("  %zu wards", model.wards.size());
    SDL_Log("  %zu buildings", model.getAllBuildings().size());
    SDL_Log("  %zu streets", model.streets.size());
    if (model.wall) {
        SDL_Log("  %zu wall towers", model.wall->towers.size());
    }
    SDL_Log("  %zu gates", model.gates.size());
    if (!model.water.rivers.empty()) {
        SDL_Log("  %zu rivers/coast", model.water.rivers.size());
    }
    if (!model.water.ponds.empty()) {
        SDL_Log("  %zu ponds", model.water.ponds.size());
    }
    if (!model.water.bridges.empty()) {
        SDL_Log("  %zu bridges", model.water.bridges.size());
    }
    if (!model.water.piers.empty()) {
        SDL_Log("  %zu piers", model.water.piers.size());
    }

    // Count wards by type
    std::map<city::WardType, int> wardCounts;
    for (const auto& ward : model.wards) {
        wardCounts[ward->type]++;
    }
    for (const auto& [type, count] : wardCounts) {
        SDL_Log("  %d x %s", count, city::wardTypeName(type));
    }

    // Export outputs
    std::string geojsonPath = outputDir + "/city.geojson";
    std::string svgPath = outputDir + "/city.svg";

    SDL_Log("Exporting GeoJSON: %s", geojsonPath.c_str());
    city::exportGeoJSON(model, geojsonPath, treeDensity);

    SDL_Log("Exporting SVG: %s", svgPath.c_str());
    city::exportSVG(model, svgPath, svgWidth, svgHeight, treeDensity);

    SDL_Log("City generation complete!");
    SDL_Log("View the city preview: %s", svgPath.c_str());

    return 0;
}
