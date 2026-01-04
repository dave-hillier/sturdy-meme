#pragma once

#include <cmath>
#include <algorithm>

namespace town_generator {
namespace utils {

/**
 * MathUtils - Faithful port from Haxe TownGeneratorOS
 */
class MathUtils {
public:
    // Clamp value between min and max (called "gate" in Haxe)
    static double gate(double value, double min, double max) {
        return value < min ? min : (value < max ? value : max);
    }

    static int gatei(int value, int min, int max) {
        return value < min ? min : (value < max ? value : max);
    }

    // Sign function
    static int sign(double value) {
        return value == 0 ? 0 : (value < 0 ? -1 : 1);
    }

    // Linear interpolation
    static double lerp(double a, double b, double t) {
        return a + (b - a) * t;
    }

    // Equality
    bool operator==(const MathUtils& other) const {
        return true; // Stateless utility class
    }

    bool operator!=(const MathUtils& other) const {
        return false;
    }
};

} // namespace utils
} // namespace town_generator
