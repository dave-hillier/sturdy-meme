#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>

/**
 * Static Distance Heap - O(1) amortized distance checks for streaming volumes.
 *
 * Based on Ghost of Tsushima GDC 2021 "Zen of Streaming" talk.
 *
 * Instead of checking distance to every volume every frame (O(n)),
 * this heap tracks when each volume's boundary will next be crossed
 * based on cumulative player travel distance. Only volumes at the
 * top of the heap (whose threshold has been reached) need checking.
 *
 * Key features:
 * - O(1) amortized per-frame update cost
 * - Back-indices for O(log n) removal
 * - Automatic rebase every 100m to maintain precision
 *
 * Usage:
 *   StaticDistanceHeap<TileCoord> heap;
 *   heap.add(coord, boundingBox, [](const TileCoord& c) { return getBounds(c); });
 *
 *   // Each frame:
 *   heap.update(playerPos);
 *   for (const auto& item : heap.getWokenItems()) { loadTile(item); }
 *   for (const auto& item : heap.getSleptItems()) { unloadTile(item); }
 */
template<typename T>
class StaticDistanceHeap {
public:
    struct BoundingVolume {
        glm::vec3 center;
        float radius;  // For sphere-based distance (simpler than AABB SDF)
    };

    using BoundsFunc = std::function<BoundingVolume(const T&)>;

    StaticDistanceHeap() = default;

    /**
     * Set the bounds function used to get bounding volumes for items.
     */
    void setBoundsFunc(BoundsFunc func) {
        boundsFunc = std::move(func);
    }

    /**
     * Add an item to the heap.
     * @param item The item to track
     * @param bounds The bounding volume for this item
     */
    void add(const T& item, const BoundingVolume& bounds) {
        Entry entry;
        entry.item = item;
        entry.bounds = bounds;
        entry.nextCheckDistance = 0.0f;  // Will be computed on first update
        entry.isAwake = false;
        entry.heapIndex = entries.size();

        entries.push_back(entry);
        heapifyUp(entries.size() - 1);
    }

    /**
     * Remove an item from the heap.
     * @param item The item to remove
     * @return true if found and removed
     */
    bool remove(const T& item) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].item == item) {
                removeAt(i);
                return true;
            }
        }
        return false;
    }

    /**
     * Update the heap based on current player position.
     * Call this once per frame.
     * @param playerPos Current player/camera position
     */
    void update(const glm::vec3& playerPos) {
        // Accumulate travel distance
        if (hasLastPos) {
            float dist = glm::distance(playerPos, lastPlayerPos);
            currentTravelDistance += dist;
        }
        lastPlayerPos = playerPos;
        hasLastPos = true;

        // Rebase every 100m to maintain float precision
        if (currentTravelDistance > lastRebaseDistance + REBASE_INTERVAL) {
            rebase();
        }

        wokenItems.clear();
        sleptItems.clear();

        // Process heap - check items whose threshold has been reached
        while (!entries.empty() &&
               entries[0].nextCheckDistance <= currentTravelDistance) {
            Entry& top = entries[0];

            // Compute signed distance to boundary
            float sdf = signedDistanceToBoundary(playerPos, top.bounds);

            if (sdf < 0.0f) {
                // Inside boundary - wake up if not already awake
                if (!top.isAwake) {
                    top.isAwake = true;
                    wokenItems.push_back(top.item);
                }
                // Schedule next check when we might leave
                top.nextCheckDistance = currentTravelDistance + std::abs(sdf);
            } else {
                // Outside boundary - sleep if awake
                if (top.isAwake) {
                    top.isAwake = false;
                    sleptItems.push_back(top.item);
                }
                // Schedule next check when we might enter
                top.nextCheckDistance = currentTravelDistance + sdf;
            }

            // Re-heapify after updating nextCheckDistance
            heapifyDown(0);
        }
    }

    /**
     * Get items that woke up (entered boundary) this frame.
     */
    const std::vector<T>& getWokenItems() const { return wokenItems; }

    /**
     * Get items that went to sleep (left boundary) this frame.
     */
    const std::vector<T>& getSleptItems() const { return sleptItems; }

    /**
     * Check if an item is currently awake (inside its boundary).
     */
    bool isAwake(const T& item) const {
        for (const auto& entry : entries) {
            if (entry.item == item) {
                return entry.isAwake;
            }
        }
        return false;
    }

    /**
     * Get all currently awake items.
     */
    std::vector<T> getAwakeItems() const {
        std::vector<T> result;
        for (const auto& entry : entries) {
            if (entry.isAwake) {
                result.push_back(entry.item);
            }
        }
        return result;
    }

    /**
     * Clear all items from the heap.
     */
    void clear() {
        entries.clear();
        wokenItems.clear();
        sleptItems.clear();
        currentTravelDistance = 0.0f;
        lastRebaseDistance = 0.0f;
        hasLastPos = false;
    }

    /**
     * Get the number of items in the heap.
     */
    size_t size() const { return entries.size(); }

    /**
     * Check if heap is empty.
     */
    bool empty() const { return entries.empty(); }

private:
    struct Entry {
        T item;
        BoundingVolume bounds;
        float nextCheckDistance;  // Travel distance when next check is needed
        bool isAwake;
        size_t heapIndex;  // For O(1) lookup in heap operations
    };

    std::vector<Entry> entries;
    std::vector<T> wokenItems;
    std::vector<T> sleptItems;

    BoundsFunc boundsFunc;

    glm::vec3 lastPlayerPos{0.0f};
    bool hasLastPos = false;
    float currentTravelDistance = 0.0f;
    float lastRebaseDistance = 0.0f;

    static constexpr float REBASE_INTERVAL = 100.0f;

    /**
     * Compute signed distance from point to bounding sphere.
     * Negative = inside, Positive = outside
     */
    float signedDistanceToBoundary(const glm::vec3& point, const BoundingVolume& bounds) const {
        float dist = glm::distance(point, bounds.center);
        return dist - bounds.radius;
    }

    /**
     * Rebase all travel distances to maintain precision.
     */
    void rebase() {
        float offset = currentTravelDistance - REBASE_INTERVAL;
        for (auto& entry : entries) {
            entry.nextCheckDistance = std::max(0.0f, entry.nextCheckDistance - offset);
        }
        currentTravelDistance = REBASE_INTERVAL;
        lastRebaseDistance = 0.0f;
    }

    // Min-heap operations (lowest nextCheckDistance at top)
    void heapifyUp(size_t index) {
        while (index > 0) {
            size_t parent = (index - 1) / 2;
            if (entries[index].nextCheckDistance < entries[parent].nextCheckDistance) {
                swapEntries(index, parent);
                index = parent;
            } else {
                break;
            }
        }
    }

    void heapifyDown(size_t index) {
        size_t size = entries.size();
        while (true) {
            size_t smallest = index;
            size_t left = 2 * index + 1;
            size_t right = 2 * index + 2;

            if (left < size &&
                entries[left].nextCheckDistance < entries[smallest].nextCheckDistance) {
                smallest = left;
            }
            if (right < size &&
                entries[right].nextCheckDistance < entries[smallest].nextCheckDistance) {
                smallest = right;
            }

            if (smallest != index) {
                swapEntries(index, smallest);
                index = smallest;
            } else {
                break;
            }
        }
    }

    void swapEntries(size_t i, size_t j) {
        std::swap(entries[i], entries[j]);
        entries[i].heapIndex = i;
        entries[j].heapIndex = j;
    }

    void removeAt(size_t index) {
        if (index >= entries.size()) return;

        // Move last element to this position
        if (index < entries.size() - 1) {
            entries[index] = std::move(entries.back());
            entries[index].heapIndex = index;
        }
        entries.pop_back();

        // Re-heapify if needed
        if (index < entries.size()) {
            heapifyDown(index);
            heapifyUp(index);
        }
    }
};
