// SBSAR file renderer
// Processes Substance Archive (.sbsar) files to generate texture maps
// Uses Adobe's sbsrender CLI tool if available, otherwise generates fallback textures

#include <SDL3/SDL_log.h>
#include <glm/glm.hpp>
#include <lodepng.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct RenderConfig {
    std::string inputPath;
    std::string outputDir;
    std::string outputName;
    int resolution = 1024;
    bool generateFallback = true;
    bool verbose = false;
};

// Output map types that Substance materials can produce
struct OutputMap {
    std::string name;
    std::string identifier;
    glm::vec4 fallbackColor;
    bool isSRGB;
};

// Common Substance output map types with sensible fallback colors
static const std::vector<OutputMap> STANDARD_OUTPUTS = {
    {"basecolor", "basecolor", glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), true},
    {"normal", "normal", glm::vec4(0.5f, 0.5f, 1.0f, 1.0f), false},
    {"roughness", "roughness", glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), false},
    {"metallic", "metallic", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), false},
    {"height", "height", glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), false},
    {"ambientocclusion", "ambientocclusion", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), false},
    {"emissive", "emissive", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), true},
};

bool checkSbsrenderAvailable() {
    // Try to run sbsrender --version to check if it's available
#ifdef _WIN32
    int result = std::system("sbsrender --version >nul 2>&1");
#else
    int result = std::system("sbsrender --version >/dev/null 2>&1");
#endif
    return result == 0;
}

bool renderWithSbsrender(const RenderConfig& config) {
    SDL_Log("Rendering SBSAR with sbsrender: %s", config.inputPath.c_str());

    // Build the sbsrender command
    // sbsrender render <input.sbsar> --output-path <dir> --output-name <name>_{outputNodeName}
    //   --output-format png --set-value $outputsize@<log2(resolution)>,<log2(resolution)>

    int log2Res = 0;
    int res = config.resolution;
    while (res > 1) {
        res >>= 1;
        log2Res++;
    }

    std::string cmd = "sbsrender render \"" + config.inputPath + "\"";
    cmd += " --output-path \"" + config.outputDir + "\"";
    cmd += " --output-name \"" + config.outputName + "_{outputNodeName}\"";
    cmd += " --output-format png";
    cmd += " --set-value \"$outputsize@" + std::to_string(log2Res) + "," + std::to_string(log2Res) + "\"";

    if (config.verbose) {
        SDL_Log("Command: %s", cmd.c_str());
    }

    int result = std::system(cmd.c_str());

    if (result != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "sbsrender failed with exit code %d", result);
        return false;
    }

    SDL_Log("Successfully rendered SBSAR to %s", config.outputDir.c_str());
    return true;
}

void generateFallbackTexture(const std::string& path, int resolution,
                             const glm::vec4& color) {
    std::vector<unsigned char> imageData(resolution * resolution * 4);

    unsigned char r = static_cast<unsigned char>(glm::clamp(color.r * 255.0f, 0.0f, 255.0f));
    unsigned char g = static_cast<unsigned char>(glm::clamp(color.g * 255.0f, 0.0f, 255.0f));
    unsigned char b = static_cast<unsigned char>(glm::clamp(color.b * 255.0f, 0.0f, 255.0f));
    unsigned char a = static_cast<unsigned char>(glm::clamp(color.a * 255.0f, 0.0f, 255.0f));

    for (int y = 0; y < resolution; y++) {
        for (int x = 0; x < resolution; x++) {
            int idx = (y * resolution + x) * 4;
            imageData[idx + 0] = r;
            imageData[idx + 1] = g;
            imageData[idx + 2] = b;
            imageData[idx + 3] = a;
        }
    }

    unsigned error = lodepng::encode(path, imageData, resolution, resolution);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to write fallback texture %s: %s",
                     path.c_str(), lodepng_error_text(error));
    } else {
        SDL_Log("Generated fallback texture: %s", path.c_str());
    }
}

bool generateFallbackTextures(const RenderConfig& config) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "sbsrender not available, generating fallback textures for: %s",
                config.inputPath.c_str());

    // Create output directory if it doesn't exist
    fs::create_directories(config.outputDir);

    // Generate a fallback texture for each standard output type
    for (const auto& output : STANDARD_OUTPUTS) {
        std::string outputPath = config.outputDir + "/" + config.outputName +
                                 "_" + output.name + ".png";
        generateFallbackTexture(outputPath, config.resolution, output.fallbackColor);
    }

    // Write a manifest file indicating fallbacks were generated
    std::string manifestPath = config.outputDir + "/" + config.outputName + "_manifest.txt";
    std::ofstream manifest(manifestPath);
    if (manifest.is_open()) {
        manifest << "# SBSAR Fallback Textures\n";
        manifest << "# Generated because sbsrender was not available\n";
        manifest << "# Install Adobe Substance Automation Toolkit for proper rendering\n";
        manifest << "source=" << config.inputPath << "\n";
        manifest << "resolution=" << config.resolution << "\n";
        manifest << "fallback=true\n";
        for (const auto& output : STANDARD_OUTPUTS) {
            manifest << "output=" << config.outputName << "_" << output.name << ".png\n";
        }
        manifest.close();
    }

    return true;
}

void printUsage(const char* programName) {
    SDL_Log("Usage: %s <input.sbsar> <output_dir> [options]", programName);
    SDL_Log(" ");
    SDL_Log("Renders Substance Archive (.sbsar) files to PNG texture maps.");
    SDL_Log("Requires Adobe Substance Automation Toolkit (sbsrender) for full quality.");
    SDL_Log("Falls back to placeholder textures if sbsrender is not available.");
    SDL_Log(" ");
    SDL_Log("Options:");
    SDL_Log("  --name <name>        Output file name prefix (default: input filename)");
    SDL_Log("  --resolution <n>     Texture resolution (default: 1024)");
    SDL_Log("  --no-fallback        Don't generate fallback textures if sbsrender fails");
    SDL_Log("  --verbose            Enable verbose output");
    SDL_Log("  --help               Show this help");
    SDL_Log(" ");
    SDL_Log("Output files:");
    SDL_Log("  <name>_basecolor.png       - Albedo/diffuse color (sRGB)");
    SDL_Log("  <name>_normal.png          - Normal map (linear, tangent space)");
    SDL_Log("  <name>_roughness.png       - Roughness map (linear)");
    SDL_Log("  <name>_metallic.png        - Metallic map (linear)");
    SDL_Log("  <name>_height.png          - Height/displacement map (linear)");
    SDL_Log("  <name>_ambientocclusion.png - Ambient occlusion (linear)");
    SDL_Log("  <name>_emissive.png        - Emissive map (sRGB)");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    RenderConfig config;

    // Parse positional arguments
    config.inputPath = argv[1];
    config.outputDir = argv[2];

    // Default output name from input filename
    fs::path inputFile(config.inputPath);
    config.outputName = inputFile.stem().string();

    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--name" && i + 1 < argc) {
            config.outputName = argv[++i];
        } else if (arg == "--resolution" && i + 1 < argc) {
            config.resolution = std::stoi(argv[++i]);
        } else if (arg == "--no-fallback") {
            config.generateFallback = false;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown option: %s", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    // Validate input file exists
    if (!fs::exists(config.inputPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Input file not found: %s", config.inputPath.c_str());
        return 1;
    }

    // Validate resolution is power of 2
    if ((config.resolution & (config.resolution - 1)) != 0 || config.resolution < 32) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Resolution must be a power of 2 >= 32 (got %d)", config.resolution);
        return 1;
    }

    SDL_Log("SBSAR Renderer");
    SDL_Log("==============");
    SDL_Log("Input: %s", config.inputPath.c_str());
    SDL_Log("Output: %s/%s_*.png", config.outputDir.c_str(), config.outputName.c_str());
    SDL_Log("Resolution: %d x %d", config.resolution, config.resolution);

    // Check if sbsrender is available
    bool sbsrenderAvailable = checkSbsrenderAvailable();

    if (sbsrenderAvailable) {
        SDL_Log("sbsrender found, using Substance rendering");
        if (renderWithSbsrender(config)) {
            return 0;
        }
        // If sbsrender failed, fall back to placeholders
        if (config.generateFallback) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "sbsrender failed, falling back to placeholder textures");
            if (generateFallbackTextures(config)) {
                return 0;
            }
        }
        return 1;
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "sbsrender not found in PATH");
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Install Adobe Substance Automation Toolkit for proper SBSAR rendering");
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Download from: https://www.adobe.com/products/substance3d-designer.html");

        if (config.generateFallback) {
            if (generateFallbackTextures(config)) {
                return 0;
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "No fallback generation requested, aborting");
            return 1;
        }
    }

    return 0;
}
