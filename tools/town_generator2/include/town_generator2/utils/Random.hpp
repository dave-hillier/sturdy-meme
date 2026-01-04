#pragma once

#include <cstdint>
#include <chrono>

namespace town_generator2 {
namespace utils {

/**
 * Random - Deterministic RNG matching Haxe TownGeneratorOS behavior
 *
 * Uses a linear congruential generator with the same parameters as the
 * original Haxe implementation for reproducible results.
 */
class Random {
public:
    static void reset(int s = -1) {
        if (s != -1) {
            seed_ = s;
        } else {
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            seed_ = static_cast<int>(ms % n);
        }
    }

    static int getSeed() { return seed_; }

    static double getFloat() {
        return static_cast<double>(next()) / n;
    }

    // Sum of 3 uniform randoms for pseudo-normal distribution
    static double normal() {
        return (getFloat() + getFloat() + getFloat()) / 3.0;
    }

    static int getInt(int min, int max) {
        return min + static_cast<int>(next() / static_cast<double>(n) * (max - min));
    }

    static bool getBool(double chance = 0.5) {
        return getFloat() < chance;
    }

    // Fuzzy value: f=0 returns 0.5, f=1 returns normal distribution
    static double fuzzy(double f = 1.0) {
        if (f == 0) {
            return 0.5;
        }
        return (1 - f) / 2 + f * normal();
    }

private:
    static constexpr double g = 48271.0;
    static constexpr int64_t n = 2147483647;
    static inline int seed_ = 1;

    static int next() {
        seed_ = static_cast<int>(static_cast<int64_t>(seed_ * g) % n);
        return seed_;
    }
};

} // namespace utils
} // namespace town_generator2
