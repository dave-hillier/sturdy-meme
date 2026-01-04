#pragma once

#include <cmath>
#include <algorithm>

namespace town_generator2 {
namespace utils {

/**
 * MathUtils - Utility math functions matching Haxe TownGeneratorOS
 */
struct MathUtils {
    static double gate(double value, double min, double max) {
        return value < min ? min : (value < max ? value : max);
    }

    static int gatei(int value, int min, int max) {
        return value < min ? min : (value < max ? value : max);
    }

    static int sign(double value) {
        return value == 0 ? 0 : (value < 0 ? -1 : 1);
    }
};

} // namespace utils
} // namespace town_generator2
