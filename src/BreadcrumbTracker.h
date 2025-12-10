#pragma once

#include <glm/glm.hpp>
#include <deque>
#include <optional>
#include <functional>

/**
 * Breadcrumb Tracker - Track safe positions for faster respawns.
 *
 * Based on Ghost of Tsushima GDC 2021 "Zen of Streaming" talk.
 *
 * By respawning players near their death location (at a safe breadcrumb),
 * most streaming content is already loaded, resulting in faster reload times.
 *
 * Usage:
 *   BreadcrumbTracker tracker;
 *   tracker.setSafetyCheck([](const glm::vec3& pos) {
 *       return !isInWater(pos) && !isInCombatZone(pos);
 *   });
 *
 *   // Each frame while player is alive:
 *   tracker.update(playerPos);
 *
 *   // On death:
 *   auto respawnPos = tracker.getNearestSafeBreadcrumb(deathPos);
 */
class BreadcrumbTracker {
public:
    using SafetyCheckFunc = std::function<bool(const glm::vec3&)>;

    BreadcrumbTracker() = default;

    /**
     * Set the function used to determine if a position is safe for respawning.
     * If not set, all positions are considered safe.
     */
    void setSafetyCheck(SafetyCheckFunc func) {
        safetyCheck = std::move(func);
    }

    /**
     * Set the minimum distance between breadcrumbs.
     * @param distance Minimum distance in world units (default: 10.0)
     */
    void setMinDistance(float distance) {
        minDistance = distance;
    }

    /**
     * Set the maximum number of breadcrumbs to store.
     * @param count Maximum breadcrumbs (default: 100)
     */
    void setMaxBreadcrumbs(size_t count) {
        maxBreadcrumbs = count;
    }

    /**
     * Update breadcrumb tracking with current player position.
     * Call this each frame while the player is alive and in a valid state.
     * @param playerPos Current player position
     */
    void update(const glm::vec3& playerPos) {
        // Skip if too close to last breadcrumb
        if (!breadcrumbs.empty()) {
            float dist = glm::distance(playerPos, breadcrumbs.back());
            if (dist < minDistance) {
                return;
            }
        }

        // Check if position is safe
        if (safetyCheck && !safetyCheck(playerPos)) {
            return;
        }

        // Add new breadcrumb
        breadcrumbs.push_back(playerPos);

        // Remove oldest if over limit
        while (breadcrumbs.size() > maxBreadcrumbs) {
            breadcrumbs.pop_front();
        }
    }

    /**
     * Get the nearest safe breadcrumb to a position (typically death location).
     * @param position The reference position (e.g., where player died)
     * @return The nearest safe breadcrumb, or nullopt if no breadcrumbs exist
     */
    std::optional<glm::vec3> getNearestSafeBreadcrumb(const glm::vec3& position) const {
        if (breadcrumbs.empty()) {
            return std::nullopt;
        }

        float bestDist = std::numeric_limits<float>::max();
        std::optional<glm::vec3> best;

        for (const auto& crumb : breadcrumbs) {
            float dist = glm::distance(position, crumb);
            if (dist < bestDist) {
                bestDist = dist;
                best = crumb;
            }
        }

        return best;
    }

    /**
     * Get the most recent safe breadcrumb.
     * @return The most recent breadcrumb, or nullopt if none exist
     */
    std::optional<glm::vec3> getMostRecentBreadcrumb() const {
        if (breadcrumbs.empty()) {
            return std::nullopt;
        }
        return breadcrumbs.back();
    }

    /**
     * Get a breadcrumb that is at least minDistance away from position.
     * Useful for ensuring respawn isn't too close to a hazard.
     * @param position Reference position to stay away from
     * @param minSafeDistance Minimum distance from position
     * @return A safe breadcrumb, or nullopt if none qualify
     */
    std::optional<glm::vec3> getSafeBreadcrumbAwayFrom(
        const glm::vec3& position,
        float minSafeDistance) const {

        // Search from most recent backwards
        for (auto it = breadcrumbs.rbegin(); it != breadcrumbs.rend(); ++it) {
            float dist = glm::distance(position, *it);
            if (dist >= minSafeDistance) {
                return *it;
            }
        }
        return std::nullopt;
    }

    /**
     * Clear all breadcrumbs.
     * Call this on level transitions or teleports.
     */
    void clear() {
        breadcrumbs.clear();
    }

    /**
     * Get the number of stored breadcrumbs.
     */
    size_t getBreadcrumbCount() const {
        return breadcrumbs.size();
    }

    /**
     * Check if any breadcrumbs are stored.
     */
    bool hasBreadcrumbs() const {
        return !breadcrumbs.empty();
    }

    /**
     * Get all breadcrumbs (for debugging/visualization).
     */
    const std::deque<glm::vec3>& getAllBreadcrumbs() const {
        return breadcrumbs;
    }

private:
    std::deque<glm::vec3> breadcrumbs;
    SafetyCheckFunc safetyCheck;

    float minDistance = 10.0f;
    size_t maxBreadcrumbs = 100;
};
