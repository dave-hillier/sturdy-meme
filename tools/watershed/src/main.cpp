#include "d8.h"
#include "watershed.h"
#include "png_io.h"
#include "river_svg.h"
#include "river_binary.h"
#include <SDL3/SDL_log.h>
#include <string>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

std::string derive_output_dir(const std::string& input_file) {
    fs::path input_path(input_file);
    fs::path stem = input_path.stem();
    fs::path parent = input_path.parent_path();
    if (parent.empty()) {
        return stem.string();
    }
    return (parent / stem).string();
}

void print_usage(const char* program) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
        "Usage: %s <input.png> [options]\n"
        "\n"
        "Options:\n"
        "  -t, --threshold <n>     River threshold (min flow accumulation, default: 10000)\n"
        "  -s, --sea-level <n>     Sea level elevation (default: 0)\n"
        "  -m, --min-area <n>      Minimum watershed area for merging (default: 0, no merging)\n"
        "  -o, --output <dir>      Output directory (default: derived from input filename)\n"
        "  -r, --resolution <n>    Processing resolution (default: 1024, 0 = full resolution)\n"
        "  --terrain-size <n>      World terrain size in meters (default: 16384.0)\n"
        "  --min-altitude <n>      Minimum altitude in meters (default: 0.0)\n"
        "  --max-altitude <n>      Maximum altitude in meters (default: 200.0)\n"
        "  -h, --help              Show this help message\n"
        "\n"
        "The input should be a 16-bit grayscale PNG representing elevation data.\n"
        "Output files are written to a directory derived from the input filename.\n"
        "River SVG coordinates are scaled back to original image dimensions.\n"
        "Binary output (rivers.dat, lakes.dat) uses world-space coordinates.\n",
        program);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string input_file;
    std::string output_dir;
    uint32_t river_threshold = 10000;
    uint16_t sea_level = 0;
    uint32_t min_area = 0;
    int resolution = 1024;  // Default processing resolution

    // Binary output config (world-space conversion)
    RiverBinaryConfig binary_config;
    binary_config.terrainSize = 16384.0f;
    binary_config.minAltitude = 0.0f;
    binary_config.maxAltitude = 200.0f;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-t" || arg == "--threshold") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: %s requires an argument", arg.c_str());
                return 1;
            }
            river_threshold = std::stoul(argv[++i]);
        } else if (arg == "-s" || arg == "--sea-level") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: %s requires an argument", arg.c_str());
                return 1;
            }
            sea_level = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (arg == "-m" || arg == "--min-area") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: %s requires an argument", arg.c_str());
                return 1;
            }
            min_area = std::stoul(argv[++i]);
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: %s requires an argument", arg.c_str());
                return 1;
            }
            output_dir = argv[++i];
        } else if (arg == "-r" || arg == "--resolution") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: %s requires an argument", arg.c_str());
                return 1;
            }
            resolution = std::stoi(argv[++i]);
        } else if (arg == "--terrain-size") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: %s requires an argument", arg.c_str());
                return 1;
            }
            binary_config.terrainSize = std::stof(argv[++i]);
        } else if (arg == "--min-altitude") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: %s requires an argument", arg.c_str());
                return 1;
            }
            binary_config.minAltitude = std::stof(argv[++i]);
        } else if (arg == "--max-altitude") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: %s requires an argument", arg.c_str());
                return 1;
            }
            binary_config.maxAltitude = std::stof(argv[++i]);
        } else if (arg[0] == '-') {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: Unknown option: %s", arg.c_str());
            print_usage(argv[0]);
            return 1;
        } else {
            input_file = arg;
        }
    }

    if (input_file.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: No input file specified");
        print_usage(argv[0]);
        return 1;
    }

    if (output_dir.empty()) {
        output_dir = derive_output_dir(input_file);
    }

    try {
        fs::create_directories(output_dir);
        SDL_Log("Output directory: %s", output_dir.c_str());

        SDL_Log("Reading elevation data from: %s", input_file.c_str());
        ElevationGrid full_elevation = read_elevation_png(input_file);
        SDL_Log("  Original size: %d x %d", full_elevation.width, full_elevation.height);

        // Store original dimensions for scaling SVG output
        int original_width = full_elevation.width;
        int original_height = full_elevation.height;

        // Downsample if resolution is specified and smaller than input
        ElevationGrid elevation = full_elevation;
        if (resolution > 0 && resolution < std::max(original_width, original_height)) {
            SDL_Log("Downsampling to resolution: %d", resolution);
            elevation = downsample_elevation(full_elevation, resolution);
            SDL_Log("  Processing size: %d x %d", elevation.width, elevation.height);

            // Scale threshold proportionally to account for fewer pixels
            float scale = static_cast<float>(elevation.width) / original_width;
            river_threshold = static_cast<uint32_t>(river_threshold * scale * scale);
            SDL_Log("  Adjusted threshold: %u (scaled for resolution)", river_threshold);
        }

        SDL_Log("Computing D8 flow directions...");
        D8Result d8 = compute_d8(elevation);

        SDL_Log("Resolving DAFA by watershed merging (sea level: %u)...", sea_level);
        d8 = resolve_dafa_by_merging(elevation, d8, sea_level);

        std::string flow_file = (fs::path(output_dir) / "flow.png").string();
        SDL_Log("Writing flow accumulation to: %s", flow_file.c_str());
        write_flow_accumulation_png(flow_file, d8);

        SDL_Log("Tracing rivers from sea outlets (threshold: %u, sea level: %u)...", river_threshold, sea_level);
        std::vector<uint32_t> river_map = trace_rivers_from_sea(elevation, d8, river_threshold, sea_level);

        std::string rivers_file = (fs::path(output_dir) / "rivers.png").string();
        SDL_Log("Writing river network to: %s", rivers_file.c_str());
        write_traced_rivers_png(rivers_file, river_map, d8.width, d8.height);

        SDL_Log("Extracting individual river paths...");
        std::vector<River> rivers = extract_river_paths(river_map, d8, d8.width, d8.height);

        std::string rivers_svg_file = (fs::path(output_dir) / "rivers.svg").string();
        SDL_Log("Writing river SVG to: %s (scaled to %dx%d)", rivers_svg_file.c_str(), original_width, original_height);
        write_rivers_svg(rivers_svg_file, rivers, d8.width, d8.height, original_width, original_height);

        std::string combined_file = (fs::path(output_dir) / "combined.png").string();
        SDL_Log("Writing combined terrain+rivers to: %s", combined_file.c_str());
        write_terrain_traced_rivers_png(combined_file, elevation, river_map, sea_level);

        SDL_Log("Delineating watersheds...");
        WatershedResult watersheds = delineate_watersheds(d8);
        SDL_Log("  Found %u basins", watersheds.basin_count);

        if (min_area > 0) {
            SDL_Log("Merging watersheds with area < %u pixels...", min_area);
            watersheds = merge_watersheds(watersheds, elevation, d8, min_area);
            SDL_Log("  Remaining basins: %u", watersheds.basin_count);
        }

        std::string output_file = (fs::path(output_dir) / "watersheds.png").string();
        SDL_Log("Writing watershed map to: %s", output_file.c_str());
        write_watershed_png(output_file, watersheds);

        // Write binary files for biome_preprocess compatibility
        std::string flow_acc_bin = (fs::path(output_dir) / "flow_accumulation.bin").string();
        SDL_Log("Writing flow accumulation binary to: %s", flow_acc_bin.c_str());
        write_flow_accumulation_bin(flow_acc_bin, d8);

        std::string flow_dir_bin = (fs::path(output_dir) / "flow_direction.bin").string();
        SDL_Log("Writing flow direction binary to: %s", flow_dir_bin.c_str());
        write_flow_direction_bin(flow_dir_bin, d8);

        std::string watershed_bin = (fs::path(output_dir) / "watershed_labels.bin").string();
        SDL_Log("Writing watershed labels binary to: %s", watershed_bin.c_str());
        write_watershed_labels_bin(watershed_bin, watersheds);

        // Write binary files for engine runtime (ErosionDataLoader)
        std::string rivers_dat = (fs::path(output_dir) / "rivers.dat").string();
        SDL_Log("Writing rivers binary to: %s (terrain: %.0fm, altitude: %.0f-%.0fm)",
                rivers_dat.c_str(), binary_config.terrainSize,
                binary_config.minAltitude, binary_config.maxAltitude);
        write_rivers_binary(rivers_dat, rivers, full_elevation, d8.width, d8.height, binary_config);

        std::string lakes_dat = (fs::path(output_dir) / "lakes.dat").string();
        SDL_Log("Writing lakes binary to: %s", lakes_dat.c_str());
        write_lakes_binary(lakes_dat);

        SDL_Log("Done.");
        return 0;

    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: %s", e.what());
        return 1;
    }
}
