#include "TrainingMLP.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>

// ─── ELU helpers ────────────────────────────────────────────────────────────────

static float elu(float x, float alpha = 1.0f) {
    return x > 0.0f ? x : alpha * (std::exp(x) - 1.0f);
}

static float eluDerivative(float x, float alpha = 1.0f) {
    return x > 0.0f ? 1.0f : alpha * std::exp(x);
}

// ─── TrainingMLP ────────────────────────────────────────────────────────────────

TrainingMLP::TrainingMLP(const Config& config)
    : config_(config)
{
    // Build layer dimensions: input -> hidden x N -> output
    std::vector<std::pair<size_t, size_t>> dims;
    dims.emplace_back(config.inputDim, config.hiddenDim);
    for (size_t i = 1; i < config.hiddenLayers; ++i) {
        dims.emplace_back(config.hiddenDim, config.hiddenDim);
    }
    dims.emplace_back(config.hiddenDim, config.outputDim);

    layers_.resize(dims.size());
    for (size_t i = 0; i < dims.size(); ++i) {
        auto& layer = layers_[i];
        layer.inputDim = dims[i].first;
        layer.outputDim = dims[i].second;

        size_t wCount = layer.outputDim * layer.inputDim;
        layer.weights.resize(wCount);
        layer.biases.resize(layer.outputDim, 0.0f);

        layer.dWeights.resize(wCount, 0.0f);
        layer.dBiases.resize(layer.outputDim, 0.0f);

        layer.mWeights.resize(wCount, 0.0f);
        layer.vWeights.resize(wCount, 0.0f);
        layer.mBiases.resize(layer.outputDim, 0.0f);
        layer.vBiases.resize(layer.outputDim, 0.0f);

        layer.preActivation.resize(layer.outputDim);
        layer.postActivation.resize(layer.outputDim);
    }

    inputCopy_.resize(config.inputDim);
    initXavier();
}

void TrainingMLP::initXavier() {
    std::mt19937 rng(42);
    for (auto& layer : layers_) {
        float stddev = std::sqrt(2.0f / static_cast<float>(layer.inputDim + layer.outputDim));
        std::normal_distribution<float> dist(0.0f, stddev);
        for (auto& w : layer.weights) w = dist(rng);
        std::fill(layer.biases.begin(), layer.biases.end(), 0.0f);
    }
}

const std::vector<float>& TrainingMLP::forward(const float* input) {
    std::copy(input, input + config_.inputDim, inputCopy_.data());

    const float* layerInput = inputCopy_.data();

    for (size_t l = 0; l < layers_.size(); ++l) {
        auto& layer = layers_[l];

        // y = W * x + b
        for (size_t row = 0; row < layer.outputDim; ++row) {
            float sum = layer.biases[row];
            const float* rowPtr = layer.weights.data() + row * layer.inputDim;
            for (size_t col = 0; col < layer.inputDim; ++col) {
                sum += rowPtr[col] * layerInput[col];
            }
            layer.preActivation[row] = sum;
        }

        // Apply ELU to hidden layers, linear for output
        if (l + 1 < layers_.size()) {
            for (size_t i = 0; i < layer.outputDim; ++i) {
                layer.postActivation[i] = elu(layer.preActivation[i]);
            }
        } else {
            layer.postActivation = layer.preActivation;
        }

        layerInput = layer.postActivation.data();
    }

    return layers_.back().postActivation;
}

void TrainingMLP::backward(const float* outputGrad) {
    // dL/d(post_activation) for the current layer
    std::vector<float> delta(outputGrad, outputGrad + config_.outputDim);

    for (int l = static_cast<int>(layers_.size()) - 1; l >= 0; --l) {
        auto& layer = layers_[l];

        // For hidden layers, multiply delta by activation derivative
        if (l + 1 < static_cast<int>(layers_.size())) {
            for (size_t i = 0; i < layer.outputDim; ++i) {
                delta[i] *= eluDerivative(layer.preActivation[i]);
            }
        }

        // Get input to this layer
        const float* layerInput = (l > 0) ? layers_[l - 1].postActivation.data()
                                          : inputCopy_.data();

        // Accumulate weight gradients: dW += delta * input^T
        for (size_t row = 0; row < layer.outputDim; ++row) {
            float* dRowPtr = layer.dWeights.data() + row * layer.inputDim;
            for (size_t col = 0; col < layer.inputDim; ++col) {
                dRowPtr[col] += delta[row] * layerInput[col];
            }
        }

        // Accumulate bias gradients: db += delta
        for (size_t i = 0; i < layer.outputDim; ++i) {
            layer.dBiases[i] += delta[i];
        }

        // Propagate: delta_prev = W^T * delta
        if (l > 0) {
            std::vector<float> prevDelta(layer.inputDim, 0.0f);
            for (size_t col = 0; col < layer.inputDim; ++col) {
                float sum = 0.0f;
                for (size_t row = 0; row < layer.outputDim; ++row) {
                    sum += layer.weights[row * layer.inputDim + col] * delta[row];
                }
                prevDelta[col] = sum;
            }
            delta = std::move(prevDelta);
        }
    }
}

void TrainingMLP::adamStep(float lr, float beta1, float beta2, float epsilon) {
    ++adamT_;
    float bc1 = 1.0f - std::pow(beta1, static_cast<float>(adamT_));
    float bc2 = 1.0f - std::pow(beta2, static_cast<float>(adamT_));

    for (auto& layer : layers_) {
        size_t wCount = layer.weights.size();

        for (size_t i = 0; i < wCount; ++i) {
            layer.mWeights[i] = beta1 * layer.mWeights[i] + (1.0f - beta1) * layer.dWeights[i];
            layer.vWeights[i] = beta2 * layer.vWeights[i] + (1.0f - beta2) * layer.dWeights[i] * layer.dWeights[i];
            float mHat = layer.mWeights[i] / bc1;
            float vHat = layer.vWeights[i] / bc2;
            layer.weights[i] -= lr * mHat / (std::sqrt(vHat) + epsilon);
        }

        for (size_t i = 0; i < layer.outputDim; ++i) {
            layer.mBiases[i] = beta1 * layer.mBiases[i] + (1.0f - beta1) * layer.dBiases[i];
            layer.vBiases[i] = beta2 * layer.vBiases[i] + (1.0f - beta2) * layer.dBiases[i] * layer.dBiases[i];
            float mHat = layer.mBiases[i] / bc1;
            float vHat = layer.vBiases[i] / bc2;
            layer.biases[i] -= lr * mHat / (std::sqrt(vHat) + epsilon);
        }
    }
}

void TrainingMLP::zeroGrad() {
    for (auto& layer : layers_) {
        std::fill(layer.dWeights.begin(), layer.dWeights.end(), 0.0f);
        std::fill(layer.dBiases.begin(), layer.dBiases.end(), 0.0f);
    }
}

size_t TrainingMLP::parameterCount() const {
    size_t count = 0;
    for (const auto& layer : layers_) {
        count += layer.weights.size() + layer.biases.size();
    }
    return count;
}

bool TrainingMLP::saveWeights(const std::string& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TrainingMLP::saveWeights: cannot open '%s'", path.c_str());
        return false;
    }

    uint32_t magic = 0x4D4C5001; // MLPPolicy::MAGIC
    uint32_t numLayers = static_cast<uint32_t>(layers_.size());
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&numLayers), sizeof(numLayers));

    for (const auto& layer : layers_) {
        uint32_t inDim = static_cast<uint32_t>(layer.inputDim);
        uint32_t outDim = static_cast<uint32_t>(layer.outputDim);
        file.write(reinterpret_cast<const char*>(&inDim), sizeof(inDim));
        file.write(reinterpret_cast<const char*>(&outDim), sizeof(outDim));
        file.write(reinterpret_cast<const char*>(layer.weights.data()),
                   layer.weights.size() * sizeof(float));
        file.write(reinterpret_cast<const char*>(layer.biases.data()),
                   layer.biases.size() * sizeof(float));
    }

    SDL_Log("TrainingMLP saved: %zu layers to '%s'", layers_.size(), path.c_str());
    return true;
}

bool TrainingMLP::loadWeights(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    uint32_t magic = 0, numLayers = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&numLayers), sizeof(numLayers));

    if (magic != 0x4D4C5001 || numLayers != layers_.size()) return false;

    for (size_t i = 0; i < numLayers; ++i) {
        uint32_t inDim = 0, outDim = 0;
        file.read(reinterpret_cast<char*>(&inDim), sizeof(inDim));
        file.read(reinterpret_cast<char*>(&outDim), sizeof(outDim));
        if (inDim != layers_[i].inputDim || outDim != layers_[i].outputDim) return false;

        file.read(reinterpret_cast<char*>(layers_[i].weights.data()),
                  layers_[i].weights.size() * sizeof(float));
        file.read(reinterpret_cast<char*>(layers_[i].biases.data()),
                  layers_[i].biases.size() * sizeof(float));
    }

    SDL_Log("TrainingMLP loaded weights from '%s'", path.c_str());
    return true;
}

// ─── GaussianPolicy ─────────────────────────────────────────────────────────────

GaussianPolicy::GaussianPolicy(const TrainingMLP::Config& config, float initialLogStd)
    : network(config)
    , logStd(config.outputDim, initialLogStd)
    , dLogStd(config.outputDim, 0.0f)
    , mLogStd(config.outputDim, 0.0f)
    , vLogStd(config.outputDim, 0.0f)
{
}

float GaussianPolicy::sampleAction(const float* observation, float* actionOut,
                                    std::mt19937& rng) {
    const auto& mean = network.forward(observation);
    size_t dim = mean.size();

    std::normal_distribution<float> dist(0.0f, 1.0f);
    float logProbSum = 0.0f;
    constexpr float LOG_2PI = 1.8378770664093453f;

    for (size_t i = 0; i < dim; ++i) {
        float std = std::exp(logStd[i]);
        float noise = dist(rng);
        actionOut[i] = mean[i] + std * noise;

        // log p(a) = -0.5 * (log(2pi) + 2*log_std + ((a - mu)/std)^2)
        float diff = (actionOut[i] - mean[i]) / std;
        logProbSum += -0.5f * (LOG_2PI + 2.0f * logStd[i] + diff * diff);
    }

    return logProbSum;
}

float GaussianPolicy::logProb(const float* observation, const float* action) {
    const auto& mean = network.forward(observation);
    size_t dim = mean.size();

    float logProbSum = 0.0f;
    constexpr float LOG_2PI = 1.8378770664093453f;

    for (size_t i = 0; i < dim; ++i) {
        float std = std::exp(logStd[i]);
        float diff = (action[i] - mean[i]) / std;
        logProbSum += -0.5f * (LOG_2PI + 2.0f * logStd[i] + diff * diff);
    }

    return logProbSum;
}

void GaussianPolicy::backward(const float* observation, const float* action, float gradScale) {
    // Recompute forward to get mean
    const auto& mean = network.forward(observation);
    size_t dim = mean.size();

    // d(log_prob)/d(mean_i) = (action_i - mean_i) / std_i^2
    // d(log_prob)/d(log_std_i) = (action_i - mean_i)^2 / std_i^2 - 1
    std::vector<float> dMean(dim);
    for (size_t i = 0; i < dim; ++i) {
        float std = std::exp(logStd[i]);
        float var = std * std;
        float diff = action[i] - mean[i];

        dMean[i] = gradScale * diff / var;
        dLogStd[i] += gradScale * (diff * diff / var - 1.0f);
    }

    network.backward(dMean.data());
}

void GaussianPolicy::adamStep(float lr, float beta1, float beta2) {
    network.adamStep(lr, beta1, beta2);

    // Adam for logStd
    static size_t t = 0;
    ++t;
    float bc1 = 1.0f - std::pow(beta1, static_cast<float>(t));
    float bc2 = 1.0f - std::pow(beta2, static_cast<float>(t));

    for (size_t i = 0; i < logStd.size(); ++i) {
        mLogStd[i] = beta1 * mLogStd[i] + (1.0f - beta1) * dLogStd[i];
        vLogStd[i] = beta2 * vLogStd[i] + (1.0f - beta2) * dLogStd[i] * dLogStd[i];
        float mHat = mLogStd[i] / bc1;
        float vHat = vLogStd[i] / bc2;
        logStd[i] -= lr * mHat / (std::sqrt(vHat) + 1e-8f);
    }
}

void GaussianPolicy::zeroGrad() {
    network.zeroGrad();
    std::fill(dLogStd.begin(), dLogStd.end(), 0.0f);
}

bool GaussianPolicy::saveWeights(const std::string& path) const {
    return network.saveWeights(path);
}

bool GaussianPolicy::loadWeights(const std::string& path) {
    return network.loadWeights(path);
}
