#include "town_generator2/building/Model.hpp"
#include "town_generator2/mapping/SVGRenderer.hpp"
#include "town_generator2/utils/Random.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>

using namespace town_generator2;

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options] [output.svg]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --seed N       Random seed (default: time-based)\n";
    std::cerr << "  --patches N    Number of patches (default: 15)\n";
    std::cerr << "  --palette NAME Palette: default, blueprint, bw, ink, night, ancient, colour, simple\n";
    std::cerr << "  -h, --help     Show this help\n";
    std::cerr << "\nIf output file is not specified, SVG is written to stdout.\n";
}

int main(int argc, char* argv[]) {
    int seed = -1;
    int nPatches = 15;
    std::string paletteName = "default";
    std::string outputFile;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::atoi(argv[++i]);
        } else if (arg == "--patches" && i + 1 < argc) {
            nPatches = std::atoi(argv[++i]);
        } else if (arg == "--palette" && i + 1 < argc) {
            paletteName = argv[++i];
        } else if (arg[0] != '-') {
            outputFile = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Select palette
    mapping::Palette palette = mapping::Palette::DEFAULT();
    if (paletteName == "blueprint") palette = mapping::Palette::BLUEPRINT();
    else if (paletteName == "bw") palette = mapping::Palette::BW();
    else if (paletteName == "ink") palette = mapping::Palette::INK();
    else if (paletteName == "night") palette = mapping::Palette::NIGHT();
    else if (paletteName == "ancient") palette = mapping::Palette::ANCIENT();
    else if (paletteName == "colour") palette = mapping::Palette::COLOUR();
    else if (paletteName == "simple") palette = mapping::Palette::SIMPLE();

    try {
        // Generate town
        std::cerr << "Generating town with " << nPatches << " patches";
        if (seed > 0) {
            std::cerr << " (seed: " << seed << ")";
        }
        std::cerr << "...\n";

        building::Model model(nPatches, seed);

        std::cerr << "Town generated successfully!\n";
        std::cerr << "  Patches: " << model.patches.size() << "\n";
        std::cerr << "  Inner patches: " << model.inner.size() << "\n";
        std::cerr << "  Gates: " << model.gates.size() << "\n";
        std::cerr << "  Streets: " << model.streets.size() << "\n";
        std::cerr << "  Roads: " << model.roads.size() << "\n";
        std::cerr << "  Walls: " << (model.wall ? "yes" : "no") << "\n";
        std::cerr << "  Citadel: " << (model.citadel ? "yes" : "no") << "\n";
        std::cerr << "  Plaza: " << (model.plaza ? "yes" : "no") << "\n";

        // Render to SVG
        mapping::SVGRenderer renderer(palette);
        std::string svg = renderer.render(model);

        // Output
        if (outputFile.empty()) {
            std::cout << svg;
        } else {
            std::ofstream out(outputFile);
            if (!out) {
                std::cerr << "Error: Cannot open output file: " << outputFile << "\n";
                return 1;
            }
            out << svg;
            std::cerr << "SVG written to: " << outputFile << "\n";
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
