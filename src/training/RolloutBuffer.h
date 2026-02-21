#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Stores a single transition from one environment step.
struct Transition {
    std::vector<float> observation;
    std::vector<float> action;
    float reward = 0.0f;
    float value = 0.0f;     // V(s) from value network
    float logProb = 0.0f;   // log pi(a|s)
    bool done = false;
};

// Stores rollout data and computes Generalized Advantage Estimation (GAE).
class RolloutBuffer {
public:
    RolloutBuffer(size_t capacity, size_t obsDim, size_t actDim);

    void clear();
    void addTransition(const Transition& t);
    bool isFull() const { return size_ >= capacity_; }
    size_t size() const { return size_; }

    // Compute advantages using GAE(lambda) and returns-to-go.
    // lastValue: V(s_T+1) for the last state (bootstrap value).
    void computeGAE(float lastValue, float gamma = 0.99f, float lambda = 0.95f);

    // Access computed data for PPO update
    const std::vector<float>& observations() const { return observations_; }
    const std::vector<float>& actions() const { return actions_; }
    const std::vector<float>& advantages() const { return advantages_; }
    const std::vector<float>& returns() const { return returns_; }
    const std::vector<float>& oldLogProbs() const { return logProbs_; }
    const std::vector<float>& oldValues() const { return values_; }

    size_t obsDim() const { return obsDim_; }
    size_t actDim() const { return actDim_; }

private:
    size_t capacity_;
    size_t obsDim_;
    size_t actDim_;
    size_t size_ = 0;

    // Flat arrays: each transition is obsDim/actDim contiguous floats
    std::vector<float> observations_;
    std::vector<float> actions_;
    std::vector<float> rewards_;
    std::vector<float> values_;
    std::vector<float> logProbs_;
    std::vector<uint8_t> dones_;

    // Computed by computeGAE()
    std::vector<float> advantages_;
    std::vector<float> returns_;
};
