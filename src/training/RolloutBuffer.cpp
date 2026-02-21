#include "RolloutBuffer.h"

#include <algorithm>
#include <cmath>
#include <numeric>

RolloutBuffer::RolloutBuffer(size_t capacity, size_t obsDim, size_t actDim)
    : capacity_(capacity)
    , obsDim_(obsDim)
    , actDim_(actDim)
{
    observations_.resize(capacity * obsDim, 0.0f);
    actions_.resize(capacity * actDim, 0.0f);
    rewards_.resize(capacity, 0.0f);
    values_.resize(capacity, 0.0f);
    logProbs_.resize(capacity, 0.0f);
    dones_.resize(capacity, 0);
    advantages_.resize(capacity, 0.0f);
    returns_.resize(capacity, 0.0f);
}

void RolloutBuffer::clear() {
    size_ = 0;
}

void RolloutBuffer::addTransition(const Transition& t) {
    if (size_ >= capacity_) return;

    size_t obsOffset = size_ * obsDim_;
    size_t actOffset = size_ * actDim_;

    std::copy(t.observation.begin(), t.observation.end(), observations_.begin() + obsOffset);
    std::copy(t.action.begin(), t.action.end(), actions_.begin() + actOffset);
    rewards_[size_] = t.reward;
    values_[size_] = t.value;
    logProbs_[size_] = t.logProb;
    dones_[size_] = t.done ? 1 : 0;

    ++size_;
}

void RolloutBuffer::computeGAE(float lastValue, float gamma, float lambda) {
    if (size_ == 0) return;

    float gae = 0.0f;
    float nextValue = lastValue;

    for (size_t ti = size_; ti > 0; --ti) {
        size_t t = ti - 1;
        float nonTerminal = dones_[t] ? 0.0f : 1.0f;
        float delta = rewards_[t] + gamma * nextValue * nonTerminal - values_[t];
        gae = delta + gamma * lambda * nonTerminal * gae;

        advantages_[t] = gae;
        returns_[t] = gae + values_[t];

        nextValue = values_[t];
    }

    // Normalize advantages
    if (size_ > 1) {
        float mean = std::accumulate(advantages_.begin(), advantages_.begin() + size_, 0.0f)
                   / static_cast<float>(size_);
        float var = 0.0f;
        for (size_t i = 0; i < size_; ++i) {
            float d = advantages_[i] - mean;
            var += d * d;
        }
        var /= static_cast<float>(size_);
        float stddev = std::sqrt(var + 1e-8f);

        for (size_t i = 0; i < size_; ++i) {
            advantages_[i] = (advantages_[i] - mean) / stddev;
        }
    }
}
