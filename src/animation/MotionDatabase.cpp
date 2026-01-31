#include "MotionDatabase.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace MotionMatching {

// MotionDatabase implementation

void MotionDatabase::initialize(const Skeleton& skeleton, const FeatureConfig& config) {
    skeleton_ = skeleton;
    config_ = config;
    featureExtractor_.initialize(skeleton_, config_);
    initialized_ = true;
    built_ = false;

    SDL_Log("MotionDatabase: Initialized with %zu joints", skeleton_.joints.size());
}

size_t MotionDatabase::addClip(const AnimationClip* clip,
                                const std::string& name,
                                bool looping,
                                float sampleRate,
                                const std::vector<std::string>& tags) {
    if (!initialized_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "MotionDatabase: Cannot add clip before initialization");
        return SIZE_MAX;
    }

    DatabaseClip dbClip;
    dbClip.name = name;
    dbClip.clip = clip;
    dbClip.duration = clip->duration;
    dbClip.looping = looping;
    dbClip.sampleRate = sampleRate;
    dbClip.tags = tags;

    size_t index = clips_.size();
    clips_.push_back(dbClip);

    built_ = false; // Need to rebuild

    SDL_Log("MotionDatabase: Added clip '%s' (%.2fs, %s)",
            name.c_str(), clip->duration, looping ? "looping" : "one-shot");

    return index;
}

void MotionDatabase::build(const DatabaseBuildOptions& options) {
    if (!initialized_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "MotionDatabase: Cannot build before initialization");
        return;
    }

    poses_.clear();
    size_t prunedCount = 0;

    for (size_t i = 0; i < clips_.size(); ++i) {
        size_t posesBeforeClip = poses_.size();
        indexClip(i, options);
        clips_[i].startPoseIndex = posesBeforeClip;
        clips_[i].poseCount = poses_.size() - posesBeforeClip;
    }

    // Prune poses if requested
    if (options.pruneStaticPoses) {
        std::vector<DatabasePose> prunedPoses;
        prunedPoses.reserve(poses_.size());

        for (auto& pose : poses_) {
            if (!shouldPrunePose(pose, options)) {
                prunedPoses.push_back(std::move(pose));
            } else {
                ++prunedCount;
            }
        }

        poses_ = std::move(prunedPoses);

        // Update clip pose indices - count poses per clip properly
        for (size_t clipIdx = 0; clipIdx < clips_.size(); ++clipIdx) {
            clips_[clipIdx].startPoseIndex = 0;
            clips_[clipIdx].poseCount = 0;
        }

        // First pass: count poses per clip
        for (const auto& pose : poses_) {
            if (pose.clipIndex < clips_.size()) {
                clips_[pose.clipIndex].poseCount++;
            }
        }

        // Second pass: compute start indices
        size_t runningIndex = 0;
        for (auto& clip : clips_) {
            clip.startPoseIndex = runningIndex;
            runningIndex += clip.poseCount;
        }
    }

    built_ = true;

    SDL_Log("MotionDatabase: Built with %zu poses from %zu clips (pruned %zu)",
            poses_.size(), clips_.size(), prunedCount);
}

void MotionDatabase::indexClip(size_t clipIndex, const DatabaseBuildOptions& options) {
    const DatabaseClip& dbClip = clips_[clipIndex];
    const AnimationClip* clip = dbClip.clip;

    if (!clip || clip->duration <= 0.0f) {
        return;
    }

    float sampleRate = dbClip.sampleRate > 0.0f ? dbClip.sampleRate : options.defaultSampleRate;
    float sampleInterval = 1.0f / sampleRate;

    // Ensure minimum interval
    if (options.minPoseInterval > 0.0f) {
        sampleInterval = std::max(sampleInterval, options.minPoseInterval);
    }

    float duration = clip->duration;
    size_t sampleCount = static_cast<size_t>(duration / sampleInterval) + 1;

    for (size_t i = 0; i < sampleCount; ++i) {
        float time = static_cast<float>(i) * sampleInterval;
        if (time > duration) {
            time = duration;
        }

        DatabasePose pose;
        pose.clipIndex = clipIndex;
        pose.time = time;
        pose.normalizedTime = duration > 0.0f ? time / duration : 0.0f;

        // Extract features
        pose.poseFeatures = featureExtractor_.extractFromClip(*clip, skeleton_, time);
        pose.trajectory = featureExtractor_.extractTrajectoryFromClip(*clip, skeleton_, time);

        // Apply clip bias
        pose.costBias = dbClip.costBias;

        // Copy tags from clip
        pose.tags = dbClip.tags;

        // Mark loop boundaries
        if (dbClip.looping) {
            pose.isLoopBoundary = (time < options.loopBoundaryMargin) ||
                                  (time > duration - options.loopBoundaryMargin);
        }

        poses_.push_back(pose);
    }
}

bool MotionDatabase::shouldPrunePose(const DatabasePose& pose,
                                       const DatabaseBuildOptions& options) const {
    if (!options.pruneStaticPoses) {
        return false;
    }

    // Check if pose has significant motion
    float totalVelocity = glm::length(pose.poseFeatures.rootVelocity);
    for (size_t i = 0; i < pose.poseFeatures.boneCount; ++i) {
        totalVelocity += glm::length(pose.poseFeatures.boneFeatures[i].velocity);
    }

    return totalVelocity < options.staticThreshold;
}

std::vector<const DatabasePose*> MotionDatabase::getPosesFromClip(size_t clipIndex) const {
    std::vector<const DatabasePose*> result;

    if (clipIndex >= clips_.size()) {
        return result;
    }

    const DatabaseClip& clip = clips_[clipIndex];
    result.reserve(clip.poseCount);

    for (size_t i = clip.startPoseIndex; i < clip.startPoseIndex + clip.poseCount; ++i) {
        if (i < poses_.size()) {
            result.push_back(&poses_[i]);
        }
    }

    return result;
}

std::vector<const DatabasePose*> MotionDatabase::getPosesWithTag(const std::string& tag) const {
    std::vector<const DatabasePose*> result;

    for (const auto& pose : poses_) {
        if (pose.hasTag(tag)) {
            result.push_back(&pose);
        }
    }

    return result;
}

void MotionDatabase::clear() {
    clips_.clear();
    poses_.clear();
    built_ = false;
}

MotionDatabase::Stats MotionDatabase::getStats() const {
    Stats stats;
    stats.totalPoses = poses_.size();
    stats.totalClips = clips_.size();

    for (const auto& clip : clips_) {
        stats.totalDuration += clip.duration;
    }

    return stats;
}

// MotionMatcher implementation

MatchResult MotionMatcher::findBestMatch(const Trajectory& queryTrajectory,
                                           const PoseFeatures& queryPose,
                                           const SearchOptions& options) const {
    MatchResult best;
    best.cost = std::numeric_limits<float>::max();

    if (!database_ || !database_->isBuilt()) {
        return best;
    }

    size_t poseCount = database_->getPoseCount();
    for (size_t i = 0; i < poseCount; ++i) {
        const DatabasePose& pose = database_->getPose(i);

        if (!passesFilters(pose, options)) {
            continue;
        }

        float cost = computeCost(i, queryTrajectory, queryPose, options);

        if (cost < best.cost) {
            best.poseIndex = i;
            best.cost = cost;
            best.pose = &pose;
            best.clip = &database_->getClip(pose.clipIndex);
        }
    }

    // Compute cost breakdown for best match
    if (best.isValid()) {
        best.trajectoryCost = queryTrajectory.computeCost(
            best.pose->trajectory,
            database_->getFeatureExtractor().getConfig().trajectoryPositionWeight,
            database_->getFeatureExtractor().getConfig().trajectoryVelocityWeight,
            database_->getFeatureExtractor().getConfig().trajectoryFacingWeight
        );
        best.poseCost = queryPose.computeCost(
            best.pose->poseFeatures,
            database_->getFeatureExtractor().getConfig().bonePositionWeight,
            database_->getFeatureExtractor().getConfig().rootVelocityWeight,
            database_->getFeatureExtractor().getConfig().angularVelocityWeight,
            database_->getFeatureExtractor().getConfig().phaseWeight
        );
    }

    return best;
}

std::vector<MatchResult> MotionMatcher::findTopMatches(const Trajectory& queryTrajectory,
                                                         const PoseFeatures& queryPose,
                                                         size_t count,
                                                         const SearchOptions& options) const {
    std::vector<MatchResult> results;

    if (!database_ || !database_->isBuilt() || count == 0) {
        return results;
    }

    // Collect all valid matches with costs
    std::vector<std::pair<float, size_t>> candidates;
    size_t poseCount = database_->getPoseCount();

    for (size_t i = 0; i < poseCount; ++i) {
        const DatabasePose& pose = database_->getPose(i);

        if (!passesFilters(pose, options)) {
            continue;
        }

        float cost = computeCost(i, queryTrajectory, queryPose, options);
        candidates.emplace_back(cost, i);
    }

    // Sort by cost
    std::partial_sort(candidates.begin(),
                      candidates.begin() + std::min(count, candidates.size()),
                      candidates.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

    // Build results
    size_t resultCount = std::min(count, candidates.size());
    results.reserve(resultCount);

    for (size_t i = 0; i < resultCount; ++i) {
        MatchResult result;
        result.poseIndex = candidates[i].second;
        result.cost = candidates[i].first;
        result.pose = &database_->getPose(result.poseIndex);
        result.clip = &database_->getClip(result.pose->clipIndex);

        // Compute cost breakdown
        result.trajectoryCost = queryTrajectory.computeCost(
            result.pose->trajectory,
            database_->getFeatureExtractor().getConfig().trajectoryPositionWeight,
            database_->getFeatureExtractor().getConfig().trajectoryVelocityWeight,
            database_->getFeatureExtractor().getConfig().trajectoryFacingWeight
        );
        result.poseCost = queryPose.computeCost(
            result.pose->poseFeatures,
            database_->getFeatureExtractor().getConfig().bonePositionWeight,
            database_->getFeatureExtractor().getConfig().rootVelocityWeight,
            database_->getFeatureExtractor().getConfig().angularVelocityWeight,
            database_->getFeatureExtractor().getConfig().phaseWeight
        );

        results.push_back(result);
    }

    return results;
}

float MotionMatcher::computeCost(size_t poseIndex,
                                   const Trajectory& queryTrajectory,
                                   const PoseFeatures& queryPose,
                                   const SearchOptions& options) const {
    if (!database_ || poseIndex >= database_->getPoseCount()) {
        return std::numeric_limits<float>::max();
    }

    const DatabasePose& pose = database_->getPose(poseIndex);
    const FeatureConfig& config = database_->getFeatureExtractor().getConfig();

    // Trajectory cost
    float trajCost = queryTrajectory.computeCost(
        pose.trajectory,
        config.trajectoryPositionWeight,
        config.trajectoryVelocityWeight,
        config.trajectoryFacingWeight
    ) * options.trajectoryWeight * config.trajectoryWeight;

    // Pose cost
    float poseCost = queryPose.computeCost(
        pose.poseFeatures,
        config.bonePositionWeight,
        config.rootVelocityWeight,
        config.angularVelocityWeight,
        config.phaseWeight
    ) * options.poseWeight * config.poseWeight;

    // Add cost bias
    float totalCost = trajCost + poseCost + pose.costBias;

    // Penalty for reselecting current pose too soon
    if (options.currentPoseIndex != SIZE_MAX) {
        const DatabasePose& currentPose = database_->getPose(options.currentPoseIndex);
        if (pose.clipIndex == currentPose.clipIndex) {
            float timeDiff = std::abs(pose.time - currentPose.time);
            if (timeDiff < options.minTimeSinceLastSelect) {
                totalCost += 1000.0f; // Large penalty
            }
        }
    }

    return totalCost;
}

bool MotionMatcher::passesFilters(const DatabasePose& pose, const SearchOptions& options) const {
    // Check loop boundary filter
    if (!options.allowLoopBoundaries && pose.isLoopBoundary) {
        return false;
    }

    // Check required tags
    for (const auto& tag : options.requiredTags) {
        if (!pose.hasTag(tag)) {
            return false;
        }
    }

    // Check excluded tags
    for (const auto& tag : options.excludedTags) {
        if (pose.hasTag(tag)) {
            return false;
        }
    }

    // Check transition flags
    if (!pose.canTransitionTo) {
        return false;
    }

    return true;
}

} // namespace MotionMatching
