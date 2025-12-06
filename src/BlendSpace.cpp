#include "BlendSpace.h"
#include <cmath>
#include <limits>

// ============================================================================
// BlendSpace1D Implementation
// ============================================================================

void BlendSpace1D::addSample(float position, const AnimationClip* clip) {
    Sample sample;
    sample.position = position;
    sample.clip = clip;
    sample.time = 0.0f;
    sample.playbackSpeed = 1.0f;

    // Insert in sorted order by position
    auto it = std::lower_bound(samples.begin(), samples.end(), sample,
        [](const Sample& a, const Sample& b) { return a.position < b.position; });
    samples.insert(it, sample);
}

float BlendSpace1D::getMinParameter() const {
    if (samples.empty()) return 0.0f;
    return samples.front().position;
}

float BlendSpace1D::getMaxParameter() const {
    if (samples.empty()) return 0.0f;
    return samples.back().position;
}

void BlendSpace1D::findBlendSamples(size_t& outLower, size_t& outUpper, float& outBlend) const {
    if (samples.empty()) {
        outLower = outUpper = 0;
        outBlend = 0.0f;
        return;
    }

    if (samples.size() == 1) {
        outLower = outUpper = 0;
        outBlend = 0.0f;
        return;
    }

    // Clamp parameter to valid range
    float clampedParam = std::clamp(parameter, samples.front().position, samples.back().position);

    // Find the two samples to blend between
    for (size_t i = 0; i < samples.size() - 1; ++i) {
        if (clampedParam >= samples[i].position && clampedParam <= samples[i + 1].position) {
            outLower = i;
            outUpper = i + 1;

            float range = samples[i + 1].position - samples[i].position;
            if (range > 0.0001f) {
                outBlend = (clampedParam - samples[i].position) / range;
            } else {
                outBlend = 0.0f;
            }
            return;
        }
    }

    // Fallback to last sample
    outLower = outUpper = samples.size() - 1;
    outBlend = 0.0f;
}

void BlendSpace1D::update(float deltaTime) {
    if (samples.empty()) return;

    // Compute master normalized time based on blend position
    size_t lower, upper;
    float blend;
    findBlendSamples(lower, upper, blend);

    if (syncTime && samples[lower].clip && samples[upper].clip) {
        // Use weighted average duration
        float lowerDur = samples[lower].clip->duration;
        float upperDur = samples[upper].clip->duration;
        float blendedDuration = glm::mix(lowerDur, upperDur, blend);

        if (blendedDuration > 0.0f) {
            // Advance normalized time
            float normalizedDelta = deltaTime / blendedDuration;

            // Update all sample times based on normalized time
            for (auto& sample : samples) {
                if (sample.clip && sample.clip->duration > 0.0f) {
                    float normalizedTime = sample.time / sample.clip->duration;
                    normalizedTime += normalizedDelta * sample.playbackSpeed;
                    normalizedTime = std::fmod(normalizedTime, 1.0f);
                    if (normalizedTime < 0.0f) normalizedTime += 1.0f;
                    sample.time = normalizedTime * sample.clip->duration;
                }
            }
        }
    } else {
        // Independent time update
        for (auto& sample : samples) {
            if (sample.clip && sample.clip->duration > 0.0f) {
                sample.time += deltaTime * sample.playbackSpeed;
                sample.time = std::fmod(sample.time, sample.clip->duration);
            }
        }
    }
}

void BlendSpace1D::sampleClipToPose(const AnimationClip* clip, float time,
                                     const Skeleton& bindPose, SkeletonPose& outPose) const {
    if (!clip) return;

    outPose.resize(bindPose.joints.size());

    // Start with bind pose
    for (size_t i = 0; i < bindPose.joints.size(); ++i) {
        outPose[i] = BonePose::fromMatrix(bindPose.joints[i].localTransform,
                                          bindPose.joints[i].preRotation);
    }

    // Apply animation channels
    for (const auto& channel : clip->channels) {
        if (channel.jointIndex < 0 || static_cast<size_t>(channel.jointIndex) >= outPose.size()) {
            continue;
        }

        BonePose& pose = outPose[channel.jointIndex];
        if (channel.hasTranslation()) {
            pose.translation = channel.translation.sample(time);
        }
        if (channel.hasRotation()) {
            pose.rotation = channel.rotation.sample(time);
        }
        if (channel.hasScale()) {
            pose.scale = channel.scale.sample(time);
        }
    }
}

void BlendSpace1D::samplePose(const Skeleton& bindPose, SkeletonPose& outPose) const {
    if (samples.empty()) {
        outPose.resize(bindPose.joints.size());
        for (size_t i = 0; i < bindPose.joints.size(); ++i) {
            outPose[i] = BonePose::fromMatrix(bindPose.joints[i].localTransform,
                                              bindPose.joints[i].preRotation);
        }
        return;
    }

    size_t lower, upper;
    float blend;
    findBlendSamples(lower, upper, blend);

    if (lower == upper || blend < 0.001f) {
        // Use single sample
        sampleClipToPose(samples[lower].clip, samples[lower].time, bindPose, outPose);
    } else if (blend > 0.999f) {
        sampleClipToPose(samples[upper].clip, samples[upper].time, bindPose, outPose);
    } else {
        // Blend between two samples
        SkeletonPose lowerPose, upperPose;
        sampleClipToPose(samples[lower].clip, samples[lower].time, bindPose, lowerPose);
        sampleClipToPose(samples[upper].clip, samples[upper].time, bindPose, upperPose);
        AnimationBlend::blend(lowerPose, upperPose, blend, outPose);
    }
}

// ============================================================================
// BlendSpace2D Implementation
// ============================================================================

void BlendSpace2D::addSample(float x, float y, const AnimationClip* clip) {
    addSample(glm::vec2(x, y), clip);
}

void BlendSpace2D::addSample(const glm::vec2& position, const AnimationClip* clip) {
    Sample sample;
    sample.position = position;
    sample.clip = clip;
    sample.time = 0.0f;
    sample.playbackSpeed = 1.0f;
    samples.push_back(sample);
}

void BlendSpace2D::computeBlendWeights(std::vector<float>& outWeights) const {
    outWeights.resize(samples.size(), 0.0f);

    if (samples.empty()) return;

    if (samples.size() == 1) {
        outWeights[0] = 1.0f;
        return;
    }

    // Inverse distance weighting with Shepard's method
    // w_i = 1 / d_i^p, normalized so sum = 1
    const float power = 2.0f;
    const float epsilon = 0.0001f;

    float totalWeight = 0.0f;

    for (size_t i = 0; i < samples.size(); ++i) {
        float distance = glm::length(samples[i].position - parameters);

        if (distance < epsilon) {
            // Very close to this sample - use it exclusively
            std::fill(outWeights.begin(), outWeights.end(), 0.0f);
            outWeights[i] = 1.0f;
            return;
        }

        outWeights[i] = 1.0f / std::pow(distance, power);
        totalWeight += outWeights[i];
    }

    // Normalize
    if (totalWeight > epsilon) {
        for (float& w : outWeights) {
            w /= totalWeight;
        }
    }
}

void BlendSpace2D::update(float deltaTime) {
    if (samples.empty()) return;

    std::vector<float> weights;
    computeBlendWeights(weights);

    if (syncTime) {
        // Compute weighted average duration
        float weightedDuration = 0.0f;
        for (size_t i = 0; i < samples.size(); ++i) {
            if (samples[i].clip) {
                weightedDuration += samples[i].clip->duration * weights[i];
            }
        }

        if (weightedDuration > 0.0f) {
            float normalizedDelta = deltaTime / weightedDuration;

            // Update all sample times synchronized
            for (auto& sample : samples) {
                if (sample.clip && sample.clip->duration > 0.0f) {
                    float normalizedTime = sample.time / sample.clip->duration;
                    normalizedTime += normalizedDelta * sample.playbackSpeed;
                    normalizedTime = std::fmod(normalizedTime, 1.0f);
                    if (normalizedTime < 0.0f) normalizedTime += 1.0f;
                    sample.time = normalizedTime * sample.clip->duration;
                }
            }
        }
    } else {
        // Independent time update
        for (auto& sample : samples) {
            if (sample.clip && sample.clip->duration > 0.0f) {
                sample.time += deltaTime * sample.playbackSpeed;
                sample.time = std::fmod(sample.time, sample.clip->duration);
            }
        }
    }
}

void BlendSpace2D::sampleClipToPose(const AnimationClip* clip, float time,
                                     const Skeleton& bindPose, SkeletonPose& outPose) const {
    if (!clip) return;

    outPose.resize(bindPose.joints.size());

    for (size_t i = 0; i < bindPose.joints.size(); ++i) {
        outPose[i] = BonePose::fromMatrix(bindPose.joints[i].localTransform,
                                          bindPose.joints[i].preRotation);
    }

    for (const auto& channel : clip->channels) {
        if (channel.jointIndex < 0 || static_cast<size_t>(channel.jointIndex) >= outPose.size()) {
            continue;
        }

        BonePose& pose = outPose[channel.jointIndex];
        if (channel.hasTranslation()) {
            pose.translation = channel.translation.sample(time);
        }
        if (channel.hasRotation()) {
            pose.rotation = channel.rotation.sample(time);
        }
        if (channel.hasScale()) {
            pose.scale = channel.scale.sample(time);
        }
    }
}

void BlendSpace2D::samplePose(const Skeleton& bindPose, SkeletonPose& outPose) const {
    if (samples.empty()) {
        outPose.resize(bindPose.joints.size());
        for (size_t i = 0; i < bindPose.joints.size(); ++i) {
            outPose[i] = BonePose::fromMatrix(bindPose.joints[i].localTransform,
                                              bindPose.joints[i].preRotation);
        }
        return;
    }

    // Compute blend weights
    std::vector<float> weights;
    computeBlendWeights(weights);

    // Find samples with significant weight
    std::vector<size_t> significantSamples;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] > 0.001f) {
            significantSamples.push_back(i);
        }
    }

    if (significantSamples.empty()) {
        // Shouldn't happen, but fallback to first sample
        sampleClipToPose(samples[0].clip, samples[0].time, bindPose, outPose);
        return;
    }

    if (significantSamples.size() == 1) {
        // Single dominant sample
        size_t idx = significantSamples[0];
        sampleClipToPose(samples[idx].clip, samples[idx].time, bindPose, outPose);
        return;
    }

    // Blend multiple samples
    // Start with first significant sample
    size_t firstIdx = significantSamples[0];
    sampleClipToPose(samples[firstIdx].clip, samples[firstIdx].time, bindPose, outPose);

    // Renormalize weights for significant samples
    float totalSigWeight = 0.0f;
    for (size_t idx : significantSamples) {
        totalSigWeight += weights[idx];
    }

    float accumulatedWeight = weights[firstIdx] / totalSigWeight;

    // Blend in remaining samples
    for (size_t i = 1; i < significantSamples.size(); ++i) {
        size_t idx = significantSamples[i];
        float normalizedWeight = weights[idx] / totalSigWeight;

        SkeletonPose samplePose;
        sampleClipToPose(samples[idx].clip, samples[idx].time, bindPose, samplePose);

        // Blend factor: how much of this sample in the accumulated result
        float blendT = normalizedWeight / (accumulatedWeight + normalizedWeight);
        AnimationBlend::blend(outPose, samplePose, blendT, outPose);

        accumulatedWeight += normalizedWeight;
    }
}
