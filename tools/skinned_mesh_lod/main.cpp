// Skinned mesh LOD generator tool
// Generates multiple levels of detail for skinned meshes while preserving bone weights
// Outputs binary LOD data or per-LOD files with manifest

#include "MeshSimplifier.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <sstream>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <input_file> <output_dir> [options]\n"
              << "\n"
              << "Generates LOD (Level of Detail) meshes for skinned character models.\n"
              << "Preserves bone weights and skeleton data during simplification.\n"
              << "\n"
              << "Arguments:\n"
              << "  input_file           Input mesh file (GLTF/GLB format)\n"
              << "  output_dir           Directory for output files\n"
              << "\n"
              << "Options:\n"
              << "  --lods <ratios>      Comma-separated LOD ratios (default: 1.0,0.5,0.25,0.125)\n"
              << "                       Each ratio is fraction of original triangle count\n"
              << "  --error <value>      Target simplification error (default: 0.01)\n"
              << "                       Lower = more accurate but fewer reductions\n"
              << "  --lock-boundary      Preserve mesh boundary edges (default: enabled)\n"
              << "  --no-lock-boundary   Allow boundary edges to be simplified\n"
              << "  --binary             Output single binary file instead of per-LOD files\n"
              << "  --help               Show this help message\n"
              << "\n"
              << "LOD Ratios:\n"
              << "  1.0   = Full detail (100% triangles)\n"
              << "  0.5   = Half detail (50% triangles)\n"
              << "  0.25  = Quarter detail (25% triangles)\n"
              << "  0.125 = Eighth detail (12.5% triangles)\n"
              << "\n"
              << "Output files (default mode):\n"
              << "  <name>_manifest.json   LOD manifest with statistics\n"
              << "  <name>_lod0.bin        Full detail mesh data\n"
              << "  <name>_lod1.bin        First LOD reduction\n"
              << "  ...                    Additional LOD levels\n"
              << "\n"
              << "Output file (--binary mode):\n"
              << "  <name>_lods.smld       Single binary with all LODs\n"
              << "\n"
              << "Binary format (SMLD):\n"
              << "  - Header: magic 'SMLD', version, LOD count, joint count\n"
              << "  - Skeleton: joint names, parent indices, transforms\n"
              << "  - Per-LOD: level, ratios, vertex/index data\n"
              << "\n"
              << "Example:\n"
              << "  " << programName << " character.glb ./output --lods 1.0,0.5,0.25\n"
              << "  " << programName << " character.glb ./output --binary --error 0.02\n";
}

std::vector<float> parseRatios(const std::string& str) {
    std::vector<float> ratios;
    std::stringstream ss(str);
    std::string item;

    while (std::getline(ss, item, ',')) {
        try {
            float ratio = std::stof(item);
            if (ratio > 0.0f && ratio <= 1.0f) {
                ratios.push_back(ratio);
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                           "Invalid LOD ratio %.3f (must be 0 < ratio <= 1), skipping", ratio);
            }
        } catch (...) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                       "Failed to parse LOD ratio: %s", item.c_str());
        }
    }

    // Sort ratios in descending order (highest detail first)
    std::sort(ratios.begin(), ratios.end(), std::greater<float>());

    return ratios;
}

int main(int argc, char* argv[]) {
    // Check for help flag first
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    std::string inputPath = argv[1];
    std::string outputDir = argv[2];

    LODConfig config;
    bool binaryOutput = false;

    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--lods" && i + 1 < argc) {
            config.lodRatios = parseRatios(argv[++i]);
            if (config.lodRatios.empty()) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No valid LOD ratios specified");
                return 1;
            }
        } else if (arg == "--error" && i + 1 < argc) {
            config.targetError = std::stof(argv[++i]);
        } else if (arg == "--lock-boundary") {
            config.lockBoundary = true;
        } else if (arg == "--no-lock-boundary") {
            config.lockBoundary = false;
        } else if (arg == "--binary") {
            binaryOutput = true;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown option: %s", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    // Create output directory
    std::filesystem::create_directories(outputDir);

    SDL_Log("Skinned Mesh LOD Generator");
    SDL_Log("==========================");
    SDL_Log("Input: %s", inputPath.c_str());
    SDL_Log("Output: %s", outputDir.c_str());
    SDL_Log("LOD ratios: ");
    for (float r : config.lodRatios) {
        SDL_Log("  %.1f%%", r * 100.0f);
    }
    SDL_Log("Target error: %.4f", config.targetError);
    SDL_Log("Lock boundary: %s", config.lockBoundary ? "yes" : "no");
    SDL_Log("Output format: %s", binaryOutput ? "binary" : "per-LOD files");

    MeshSimplifier simplifier;

    // Determine file type and load
    std::filesystem::path filepath(inputPath);
    std::string ext = filepath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    SDL_Log("Loading mesh...");

    bool loaded = false;
    if (ext == ".gltf" || ext == ".glb") {
        loaded = simplifier.loadGLTF(inputPath);
    } else if (ext == ".fbx") {
        loaded = simplifier.loadFBX(inputPath);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Unsupported file format: %s (use .gltf, .glb, or .fbx)", ext.c_str());
        return 1;
    }

    if (!loaded) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load mesh!");
        return 1;
    }

    // Generate LODs
    SDL_Log("Generating LODs...");

    bool success = simplifier.generateLODs(config, [](float progress, const std::string& status) {
        SDL_Log("[%3.0f%%] %s", progress * 100.0f, status.c_str());
    });

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LOD generation failed!");
        return 1;
    }

    // Save output
    if (binaryOutput) {
        std::string binaryPath = outputDir + "/" + simplifier.getLODs().name + "_lods.smld";
        if (!simplifier.saveBinary(binaryPath)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save binary output!");
            return 1;
        }
    } else {
        if (!simplifier.saveGLTF(outputDir)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save output files!");
            return 1;
        }
    }

    // Print statistics
    const auto& stats = simplifier.getStatistics();
    SDL_Log("");
    SDL_Log("LOD Generation Complete!");
    SDL_Log("========================");
    SDL_Log("Original mesh: %zu vertices, %zu triangles",
            stats.originalVertices, stats.originalTriangles);
    SDL_Log("Skeleton: %zu joints", stats.skeletonJoints);
    SDL_Log("");
    SDL_Log("LOD Statistics:");

    for (size_t i = 0; i < stats.lodVertices.size(); ++i) {
        float vertRatio = 100.0f * stats.lodVertices[i] / stats.originalVertices;
        float triRatio = 100.0f * stats.lodTriangles[i] / stats.originalTriangles;
        SDL_Log("  LOD %zu: %zu verts (%.1f%%), %zu tris (%.1f%%)",
                i, stats.lodVertices[i], vertRatio, stats.lodTriangles[i], triRatio);
    }

    return 0;
}
