#include "ModelLoader.h"
#include <fstream>
#include <cstdint>
#include <SDL3/SDL_log.h>

namespace ml {

static bool readUint32(std::ifstream& file, uint32_t& value) {
    file.read(reinterpret_cast<char*>(&value), sizeof(uint32_t));
    return file.good();
}

static bool readFloats(std::ifstream& file, std::vector<float>& data, size_t count) {
    data.resize(count);
    file.read(reinterpret_cast<char*>(data.data()), count * sizeof(float));
    return file.good();
}

static bool writeUint32(std::ofstream& file, uint32_t value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(uint32_t));
    return file.good();
}

static bool writeFloats(std::ofstream& file, const float* data, size_t count) {
    file.write(reinterpret_cast<const char*>(data), count * sizeof(float));
    return file.good();
}

static Activation uint32ToActivation(uint32_t v) {
    switch (v) {
        case 1: return Activation::ReLU;
        case 2: return Activation::Tanh;
        case 3: return Activation::ELU;
        default: return Activation::None;
    }
}

static uint32_t activationToUint32(Activation a) {
    switch (a) {
        case Activation::ReLU: return 1;
        case Activation::Tanh: return 2;
        case Activation::ELU:  return 3;
        case Activation::None: return 0;
    }
    return 0;
}

bool ModelLoader::loadMLP(const std::string& path, MLPNetwork& network) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: failed to open %s", path.c_str());
        return false;
    }

    uint32_t magic = 0, version = 0, numLayers = 0;
    if (!readUint32(file, magic) || !readUint32(file, version) || !readUint32(file, numLayers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: failed to read header from %s", path.c_str());
        return false;
    }

    if (magic != MAGIC) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: invalid magic 0x%08X in %s (expected 0x%08X)",
                     magic, path.c_str(), MAGIC);
        return false;
    }

    if (version != VERSION) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: unsupported version %u in %s", version, path.c_str());
        return false;
    }

    if (numLayers == 0 || numLayers > 100) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: invalid layer count %u in %s", numLayers, path.c_str());
        return false;
    }

    network = MLPNetwork();

    for (uint32_t i = 0; i < numLayers; ++i) {
        uint32_t inFeatures = 0, outFeatures = 0, activationType = 0;
        if (!readUint32(file, inFeatures) || !readUint32(file, outFeatures) || !readUint32(file, activationType)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: failed to read layer %u header from %s", i, path.c_str());
            return false;
        }

        Activation activation = uint32ToActivation(activationType);
        network.addLayer(static_cast<int>(inFeatures), static_cast<int>(outFeatures), activation);

        std::vector<float> weights, bias;
        size_t weightCount = static_cast<size_t>(inFeatures) * outFeatures;
        if (!readFloats(file, weights, weightCount)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: failed to read weights for layer %u from %s", i, path.c_str());
            return false;
        }
        if (!readFloats(file, bias, outFeatures)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: failed to read bias for layer %u from %s", i, path.c_str());
            return false;
        }

        network.setLayerWeights(i, std::move(weights), std::move(bias));
    }

    SDL_Log("ModelLoader: loaded %u-layer MLP from %s", numLayers, path.c_str());
    return true;
}

bool ModelLoader::saveMLP(const std::string& path, const MLPNetwork& network,
                           const std::vector<Activation>& activations) {
    if (network.numLayers() != activations.size()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: layer count mismatch in saveMLP");
        return false;
    }

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: failed to open %s for writing", path.c_str());
        return false;
    }

    writeUint32(file, MAGIC);
    writeUint32(file, VERSION);
    writeUint32(file, static_cast<uint32_t>(network.numLayers()));

    for (size_t i = 0; i < network.numLayers(); ++i) {
        const auto& layer = network.layer(i);
        writeUint32(file, static_cast<uint32_t>(layer.inFeatures));
        writeUint32(file, static_cast<uint32_t>(layer.outFeatures));
        writeUint32(file, activationToUint32(activations[i]));

        size_t weightCount = static_cast<size_t>(layer.inFeatures) * layer.outFeatures;
        writeFloats(file, layer.weights.data(), weightCount);
        writeFloats(file, layer.bias.data(), layer.outFeatures);
    }

    if (!file.good()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ModelLoader: write error to %s", path.c_str());
        return false;
    }

    SDL_Log("ModelLoader: saved %zu-layer MLP to %s", network.numLayers(), path.c_str());
    return true;
}

bool ModelLoader::loadStyleConditioned(const std::string& stylePath,
                                        const std::string& mainPath,
                                        StyleConditionedNetwork& network) {
    MLPNetwork styleMLP, mainMLP;
    if (!loadMLP(stylePath, styleMLP)) {
        return false;
    }
    if (!loadMLP(mainPath, mainMLP)) {
        return false;
    }
    network.setStyleMLP(std::move(styleMLP));
    network.setMainMLP(std::move(mainMLP));
    return true;
}

} // namespace ml
