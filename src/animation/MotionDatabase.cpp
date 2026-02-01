#include "MotionDatabase.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

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
                                const std::vector<std::string>& tags,
                                float locomotionSpeed,
                                float costBias) {
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
    dbClip.locomotionSpeed = locomotionSpeed;
    dbClip.costBias = costBias;

    size_t index = clips_.size();
    clips_.push_back(dbClip);

    built_ = false; // Need to rebuild

    SDL_Log("MotionDatabase: Added clip '%s' (%.2fs, %s, locomotionSpeed=%.1f m/s)",
            name.c_str(), clip->duration, looping ? "looping" : "one-shot", locomotionSpeed);

    // Debug: Sample the clip to see what root velocity is actually in the animation
    if (initialized_ && clip->duration > 0.0f) {
        Skeleton tempSkel = skeleton_;
        clip->sample(0.0f, tempSkel, false);
        glm::vec3 pos0 = glm::vec3(tempSkel.joints[0].localTransform[3]);
        clip->sample(clip->duration * 0.5f, tempSkel, false);
        glm::vec3 pos1 = glm::vec3(tempSkel.joints[0].localTransform[3]);
        float dist = glm::length(pos1 - pos0);
        float estimatedSpeed = dist / (clip->duration * 0.5f);
        SDL_Log("  -> Root moves %.2fm in first half, estimated speed: %.2f m/s", dist, estimatedSpeed);
    }

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

    // Compute normalization statistics
    computeNormalization();

    // Build KD-tree for accelerated search
    if (options.buildKDTree) {
        buildKDTree();
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

        // Check if the extracted root velocity is too low (in-place animation)
        // If so, and we have a locomotion speed hint, use that instead
        float extractedRootSpeed = glm::length(pose.poseFeatures.rootVelocity);
        bool isInPlace = extractedRootSpeed < 0.3f;  // Less than 0.3 m/s considered in-place

        if (isInPlace && dbClip.locomotionSpeed > 0.0f) {
            // Override trajectory velocity and position with locomotion speed
            // Assume forward motion in the character's facing direction
            for (size_t j = 0; j < pose.trajectory.sampleCount; ++j) {
                auto& sample = pose.trajectory.samples[j];
                // Velocity is locomotion speed in facing direction
                sample.velocity = sample.facing * dbClip.locomotionSpeed;
                // Position is integrated from velocity over time offset
                sample.position = sample.facing * (dbClip.locomotionSpeed * sample.timeOffset);
            }

            // Also override root velocity in pose features
            glm::vec3 facing = pose.trajectory.sampleCount > 0 ?
                pose.trajectory.samples[0].facing : glm::vec3(0.0f, 0.0f, 1.0f);
            pose.poseFeatures.rootVelocity = facing * dbClip.locomotionSpeed;
        }

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
    normalization_ = FeatureNormalization{};
    kdTree_.clear();
    built_ = false;
}

KDPoint MotionDatabase::poseToKDPoint(const Trajectory& trajectory,
                                       const PoseFeatures& pose) const {
    KDPoint point;
    size_t idx = 0;

    // Trajectory features (normalized) - position and velocity magnitudes for each sample
    // Use up to 6 trajectory samples
    for (size_t i = 0; i < std::min(trajectory.sampleCount, size_t(6)); ++i) {
        const auto& sample = trajectory.samples[i];

        // Normalize position magnitude
        float posMag = glm::length(sample.position);
        if (normalization_.isComputed && normalization_.trajectoryPosition[i].stdDev > 0.001f) {
            posMag = (posMag - normalization_.trajectoryPosition[i].mean) /
                     normalization_.trajectoryPosition[i].stdDev;
        }
        point[idx++] = posMag;

        // Normalize velocity magnitude
        float velMag = glm::length(sample.velocity);
        if (normalization_.isComputed && normalization_.trajectoryVelocity[i].stdDev > 0.001f) {
            velMag = (velMag - normalization_.trajectoryVelocity[i].mean) /
                     normalization_.trajectoryVelocity[i].stdDev;
        }
        point[idx++] = velMag;
    }

    // Pad remaining trajectory slots with zeros
    while (idx < 12) {
        point[idx++] = 0.0f;
    }

    // Root velocity (normalized) - 3 components
    glm::vec3 rootVel = pose.rootVelocity;
    if (normalization_.isComputed && normalization_.rootVelocity.stdDev > 0.001f) {
        float rootVelMag = glm::length(rootVel);
        float normalizedMag = (rootVelMag - normalization_.rootVelocity.mean) /
                              normalization_.rootVelocity.stdDev;
        if (rootVelMag > 0.001f) {
            rootVel = glm::normalize(rootVel) * normalizedMag;
        } else {
            rootVel = glm::vec3(0.0f);
        }
    }
    point[idx++] = rootVel.x;
    point[idx++] = rootVel.y;
    point[idx++] = rootVel.z;

    // Root angular velocity (normalized)
    float angVel = pose.rootAngularVelocity;
    if (normalization_.isComputed && normalization_.rootAngularVelocity.stdDev > 0.001f) {
        angVel = (angVel - normalization_.rootAngularVelocity.mean) /
                 normalization_.rootAngularVelocity.stdDev;
    }
    point[idx++] = angVel;

    return point;
}

void MotionDatabase::buildKDTree() {
    if (poses_.empty()) {
        kdTree_.clear();
        return;
    }

    // Convert all poses to KD points
    std::vector<KDPoint> points;
    points.reserve(poses_.size());

    for (size_t i = 0; i < poses_.size(); ++i) {
        const auto& pose = poses_[i];
        KDPoint point = poseToKDPoint(pose.trajectory, pose.poseFeatures);
        point.poseIndex = i;
        points.push_back(point);
    }

    // Build the tree
    kdTree_.build(std::move(points));
}

void MotionDatabase::computeNormalization() {
    if (poses_.empty()) {
        normalization_ = FeatureNormalization{};
        return;
    }

    const size_t n = poses_.size();

    // Temporary accumulators for online mean/variance calculation (Welford's algorithm)
    struct Accumulator {
        double mean = 0.0;
        double m2 = 0.0;   // Sum of squared differences from mean
        size_t count = 0;

        void add(float value) {
            ++count;
            double delta = value - mean;
            mean += delta / count;
            double delta2 = value - mean;
            m2 += delta * delta2;
        }

        FeatureStats finalize() const {
            FeatureStats stats;
            stats.mean = static_cast<float>(mean);
            if (count > 1) {
                double variance = m2 / (count - 1);
                stats.stdDev = static_cast<float>(std::sqrt(variance));
                // Prevent division by zero - use minimum stdDev
                if (stats.stdDev < 0.001f) {
                    stats.stdDev = 1.0f;
                }
            }
            return stats;
        }
    };

    // Accumulators for each feature type
    std::array<Accumulator, MAX_TRAJECTORY_SAMPLES> trajPosAcc;
    std::array<Accumulator, MAX_TRAJECTORY_SAMPLES> trajVelAcc;
    std::array<Accumulator, MAX_FEATURE_BONES> bonePosAcc;
    std::array<Accumulator, MAX_FEATURE_BONES> boneVelAcc;
    Accumulator rootVelAcc;
    Accumulator rootAngVelAcc;

    // First pass: collect all values
    for (const auto& pose : poses_) {
        // Trajectory features
        for (size_t i = 0; i < pose.trajectory.sampleCount && i < MAX_TRAJECTORY_SAMPLES; ++i) {
            const auto& sample = pose.trajectory.samples[i];
            trajPosAcc[i].add(glm::length(sample.position));
            trajVelAcc[i].add(glm::length(sample.velocity));
        }

        // Bone features
        for (size_t i = 0; i < pose.poseFeatures.boneCount && i < MAX_FEATURE_BONES; ++i) {
            const auto& bone = pose.poseFeatures.boneFeatures[i];
            bonePosAcc[i].add(glm::length(bone.position));
            boneVelAcc[i].add(glm::length(bone.velocity));
        }

        // Root features
        rootVelAcc.add(glm::length(pose.poseFeatures.rootVelocity));
        rootAngVelAcc.add(std::abs(pose.poseFeatures.rootAngularVelocity));
    }

    // Finalize statistics
    for (size_t i = 0; i < MAX_TRAJECTORY_SAMPLES; ++i) {
        normalization_.trajectoryPosition[i] = trajPosAcc[i].finalize();
        normalization_.trajectoryVelocity[i] = trajVelAcc[i].finalize();
    }

    for (size_t i = 0; i < MAX_FEATURE_BONES; ++i) {
        normalization_.bonePosition[i] = bonePosAcc[i].finalize();
        normalization_.boneVelocity[i] = boneVelAcc[i].finalize();
    }

    normalization_.rootVelocity = rootVelAcc.finalize();
    normalization_.rootAngularVelocity = rootAngVelAcc.finalize();
    normalization_.isComputed = true;

    SDL_Log("MotionDatabase: Computed normalization (rootVel mean=%.2f stdDev=%.2f)",
            normalization_.rootVelocity.mean, normalization_.rootVelocity.stdDev);
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

    // Use KD-tree acceleration if available and enabled
    if (options.useKDTree && database_->hasKDTree() && options.kdTreeCandidates > 0) {
        // Convert query to KD point
        KDPoint queryPoint = database_->poseToKDPoint(queryTrajectory, queryPose);

        // Find K nearest neighbors in the tree
        auto candidates = database_->getKDTree().findKNearest(queryPoint, options.kdTreeCandidates);

        // Evaluate each candidate with full cost function
        for (const auto& candidate : candidates) {
            const DatabasePose& pose = database_->getPose(candidate.poseIndex);

            if (!passesFilters(pose, options)) {
                continue;
            }

            float cost = computeCost(candidate.poseIndex, queryTrajectory, queryPose, options);

            if (cost < best.cost) {
                best.poseIndex = candidate.poseIndex;
                best.cost = cost;
                best.pose = &pose;
                best.clip = &database_->getClip(pose.clipIndex);
            }
        }
    } else {
        // Fallback to brute-force search
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

    // Collect candidate poses with costs
    std::vector<std::pair<float, size_t>> candidates;

    // Use KD-tree acceleration if available
    if (options.useKDTree && database_->hasKDTree() && options.kdTreeCandidates > 0) {
        // Convert query to KD point
        KDPoint queryPoint = database_->poseToKDPoint(queryTrajectory, queryPose);

        // Find more candidates than we need to account for filtering
        size_t kdCandidates = std::max(options.kdTreeCandidates, count * 2);
        auto kdResults = database_->getKDTree().findKNearest(queryPoint, kdCandidates);

        for (const auto& kdResult : kdResults) {
            const DatabasePose& pose = database_->getPose(kdResult.poseIndex);

            if (!passesFilters(pose, options)) {
                continue;
            }

            float cost = computeCost(kdResult.poseIndex, queryTrajectory, queryPose, options);
            candidates.emplace_back(cost, kdResult.poseIndex);
        }
    } else {
        // Fallback to brute-force
        size_t poseCount = database_->getPoseCount();

        for (size_t i = 0; i < poseCount; ++i) {
            const DatabasePose& pose = database_->getPose(i);

            if (!passesFilters(pose, options)) {
                continue;
            }

            float cost = computeCost(i, queryTrajectory, queryPose, options);
            candidates.emplace_back(cost, i);
        }
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
    const DatabaseClip& clip = database_->getClip(pose.clipIndex);
    const FeatureConfig& config = database_->getFeatureExtractor().getConfig();
    const FeatureNormalization& norm = database_->getNormalization();

    // Trajectory cost (use normalized if available)
    float trajCost;
    if (norm.isComputed) {
        trajCost = queryTrajectory.computeNormalizedCost(
            pose.trajectory,
            norm,
            config.trajectoryPositionWeight,
            config.trajectoryVelocityWeight,
            config.trajectoryFacingWeight
        ) * options.trajectoryWeight * config.trajectoryWeight;
    } else {
        trajCost = queryTrajectory.computeCost(
            pose.trajectory,
            config.trajectoryPositionWeight,
            config.trajectoryVelocityWeight,
            config.trajectoryFacingWeight
        ) * options.trajectoryWeight * config.trajectoryWeight;
    }

    // Pose cost (use normalized if available)
    float poseCost;
    if (norm.isComputed) {
        poseCost = queryPose.computeNormalizedCost(
            pose.poseFeatures,
            norm,
            config.bonePositionWeight,
            config.rootVelocityWeight,
            config.angularVelocityWeight,
            config.phaseWeight
        ) * options.poseWeight * config.poseWeight;
    } else {
        poseCost = queryPose.computeCost(
            pose.poseFeatures,
            config.bonePositionWeight,
            config.rootVelocityWeight,
            config.angularVelocityWeight,
            config.phaseWeight
        ) * options.poseWeight * config.poseWeight;
    }

    // Heading/Strafe cost (Unreal-style heading channel)
    float headingCost = 0.0f;
    float effectiveHeadingWeight = options.headingWeight > 0.0f ? options.headingWeight : config.headingWeight;
    if (effectiveHeadingWeight > 0.0f) {
        // Compute heading cost
        headingCost = queryPose.computeHeadingCost(pose.poseFeatures, effectiveHeadingWeight);

        // In strafe mode, add extra weight for facing direction match
        if (options.strafeMode && glm::length(options.desiredMovement) > 0.001f) {
            // Compute strafe-specific cost: how well does this pose's heading
            // align with the desired facing direction (camera-locked strafe)
            glm::vec3 poseHeading = pose.poseFeatures.heading.direction;
            glm::vec3 desiredFacing = glm::normalize(options.desiredFacing);

            float facingDot = glm::dot(poseHeading, desiredFacing);
            // 0 for perfect match, 2 for opposite
            float strafeCost = (1.0f - facingDot) * options.strafeFacingWeight;
            headingCost += strafeCost;
        }
    }

    // Add cost bias from pose
    float totalCost = trajCost + poseCost + headingCost + pose.costBias;

    // Apply Continuing Pose Cost Bias (Unreal-style)
    // Negative bias = prefer staying in current animation (more stable)
    float biasCost = 0.0f;
    if (options.currentClipIndex != SIZE_MAX && pose.clipIndex == options.currentClipIndex) {
        // This pose is from the currently playing clip - apply continuing bias
        biasCost = options.continuingPoseCostBias;
        totalCost += biasCost;
    }

    // Apply looping animation bias
    if (clip.looping) {
        totalCost += options.loopingCostBias;
        biasCost += options.loopingCostBias;
    }

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
