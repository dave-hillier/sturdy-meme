#pragma once

#include <cstdint>
#include <ctime>

namespace town_generator {
namespace utils {

/**
 * Random - Seeded PRNG, faithful port from Haxe TownGeneratorOS
 * Uses linear congruential generator matching the original Haxe implementation
 */
class Random {
private:
    // LCG constants from original Haxe code
    static constexpr double g = 48271.0;
    static constexpr int64_t n = 2147483647;

    static int64_t seed_;
    static int64_t savedSeed_;  // For save/restore

    static int64_t next() {
        seed_ = static_cast<int64_t>(static_cast<double>(seed_) * g) % n;
        return seed_;
    }

public:
    // Save current seed state (faithful to mfcg.js C.save)
    static void save() {
        savedSeed_ = seed_;
    }

    // Restore saved seed state (faithful to mfcg.js C.restore)
    static void restore() {
        seed_ = savedSeed_;
    }

    // Reset with optional seed
    static void reset(int seed = -1) {
        if (seed != -1) {
            seed_ = seed;
        } else {
            seed_ = static_cast<int64_t>(std::time(nullptr)) % n;
        }
    }

    static int getSeed() {
        return static_cast<int>(seed_);
    }

    // Random float [0, 1)
    static double floatVal() {
        return static_cast<double>(next()) / static_cast<double>(n);
    }

    // Normal-ish distribution (average of 3 uniform randoms)
    static double normal() {
        return (floatVal() + floatVal() + floatVal()) / 3.0;
    }

    // Random integer in range [min, max)
    static int intVal(int min, int max) {
        return static_cast<int>(min + static_cast<double>(next()) / static_cast<double>(n) * (max - min));
    }

    // Random boolean with given probability
    static bool boolVal(double chance = 0.5) {
        return floatVal() < chance;
    }

    // Fuzzy value - blend between 0.5 and normal distribution
    static double fuzzy(double f = 1.0) {
        if (f == 0) {
            return 0.5;
        } else {
            return (1 - f) / 2 + f * normal();
        }
    }

    // Equality (for consistency - stateless singleton pattern)
    bool operator==(const Random& other) const {
        return true;
    }

    bool operator!=(const Random& other) const {
        return false;
    }
};

} // namespace utils
} // namespace town_generator
