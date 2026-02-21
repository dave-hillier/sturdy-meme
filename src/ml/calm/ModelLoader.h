#pragma once

#include "../ModelLoader.h"
#include "LowLevelController.h"
#include "../TaskController.h"
#include "../LatentSpace.h"
#include "../CharacterConfig.h"
#include <string>

namespace ml::calm {

// Loads all CALM model components from a directory exported by calm_export.py.
//
// Expected directory layout:
//   <dir>/llc_style.bin       - Style MLP weights
//   <dir>/llc_main.bin        - Main policy MLP weights
//   <dir>/llc_mu_head.bin     - Action head weights
//   <dir>/encoder.bin         - Motion encoder (optional)
//   <dir>/hlc_heading.bin     - Heading HLC (optional)
//   <dir>/hlc_location.bin    - Location HLC (optional)
//   <dir>/hlc_strike.bin      - Strike HLC (optional)
//   <dir>/latent_library.json - Pre-encoded behavior latents (optional)
//   <dir>/retarget_map.json   - Skeleton joint retargeting map (optional)
//
// Usage:
//   calm::LowLevelController llc;
//   ml::LatentSpace latentSpace(64);
//   if (calm::ModelLoader::loadLLC("data/calm/models", llc)) { ... }
//   if (calm::ModelLoader::loadLatentLibrary("data/calm/models", latentSpace)) { ... }
class ModelLoader {
public:
    // Load the LLC (style MLP + main MLP + mu head) from three .bin files.
    static bool loadLLC(const std::string& modelDir, LowLevelController& llc);

    // Load the encoder network into a latent space.
    static bool loadEncoder(const std::string& modelDir, LatentSpace& latentSpace);

    // Load the latent library JSON into a latent space.
    static bool loadLatentLibrary(const std::string& modelDir, LatentSpace& latentSpace);

    // Load a task controller from a .bin file.
    // taskName: "heading", "location", "strike", etc.
    static bool loadHLC(const std::string& modelDir, const std::string& taskName,
                        TaskController& hlc);

    // Load a skeleton retarget map from JSON.
    // Returns a map from training joint names to engine joint names.
    struct RetargetMap {
        std::unordered_map<std::string, std::string> jointMap;
        float scaleFactor = 1.0f;
    };
    static bool loadRetargetMap(const std::string& path, RetargetMap& map);

    // Convenience: load everything from a model directory.
    // Returns true if at least the LLC loads successfully.
    struct ModelSet {
        LowLevelController llc;
        LatentSpace latentSpace;
        TaskController headingHLC;
        TaskController locationHLC;
        TaskController strikeHLC;
        bool hasEncoder = false;
        bool hasLibrary = false;
        bool hasHeadingHLC = false;
        bool hasLocationHLC = false;
        bool hasStrikeHLC = false;
    };
    static bool loadAll(const std::string& modelDir, ModelSet& models, int latentDim = 64);
};

} // namespace ml::calm
