#pragma once

#include "town_generator/utils/Random.h"
#include <vector>
#include <cmath>
#include <array>

namespace town_generator {
namespace utils {

/**
 * Perlin - 2D Perlin noise generator
 * Faithful port from mfcg.js Se (Perlin) class
 */
class Perlin {
public:
    double offsetX = 0;
    double offsetY = 0;
    double gridSize = 1;
    double amplitude = 1;

    explicit Perlin(int seed = 0) {
        // Initialize permutation table (faithful to mfcg.js)
        for (int i = 0; i < 256; ++i) {
            p_[i] = PERMUTATION[(i + seed) % 256];
        }
        // Double the table for easy wrapping
        for (int i = 0; i < 256; ++i) {
            p_[i + 256] = p_[i];
        }

        // Precompute smoothstep lookup table
        initSmooth();
    }

    double get(double x, double y) const {
        // Scale by grid size and add offset
        x = x * gridSize + offsetX;
        if (x < 0) x += 256;
        y = y * gridSize + offsetY;
        if (y < 0) y += 256;

        // Grid cell coordinates
        int xi = static_cast<int>(std::floor(x));
        int xi1 = xi + 1;
        double xf = x - xi;
        double u = smooth(xf);

        int yi = static_cast<int>(std::floor(y));
        int yi1 = yi + 1;
        double yf = y - yi;
        double v = smooth(yf);

        // Hash coordinates of 4 corners
        int aa = p_[p_[xi & 255] + (yi & 255)];
        int ba = p_[p_[xi1 & 255] + (yi & 255)];
        int ab = p_[p_[xi & 255] + (yi1 & 255)];
        int bb = p_[p_[xi1 & 255] + (yi1 & 255)];

        // Compute gradients
        double n00 = grad(aa, xf, yf);
        double n10 = grad(ba, xf - 1, yf);
        double n01 = grad(ab, xf, yf - 1);
        double n11 = grad(bb, xf - 1, yf - 1);

        // Bilinear interpolation
        double nx0 = n00 + u * (n10 - n00);
        double nx1 = n01 + u * (n11 - n01);
        double result = nx0 + v * (nx1 - nx0);

        return amplitude * result;
    }

private:
    std::array<int, 512> p_;
    static std::vector<double> smooth_;
    static bool smoothInitialized_;

    // Ken Perlin's improved permutation table
    static constexpr int PERMUTATION[256] = {
        151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225,
        140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148,
        247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32,
        57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175,
        74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122,
        60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54,
        65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169,
        200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64,
        52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212,
        207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213,
        119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
        129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104,
        218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241,
        81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31, 181, 199, 106, 157,
        184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93,
        222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180
    };

    static void initSmooth() {
        if (smoothInitialized_) return;
        smooth_.resize(4096);
        for (int i = 0; i < 4096; ++i) {
            double t = static_cast<double>(i) / 4096.0;
            // Smoothstep: 6t^5 - 15t^4 + 10t^3
            smooth_[i] = t * t * t * (t * (6 * t - 15) + 10);
        }
        smoothInitialized_ = true;
    }

    double smooth(double t) const {
        int idx = static_cast<int>(t * 4096);
        if (idx < 0) idx = 0;
        if (idx >= 4096) idx = 4095;
        return smooth_[idx];
    }

    // Gradient function (faithful to mfcg.js)
    static double grad(int hash, double x, double y) {
        switch (hash & 3) {
            case 0: return x + y;
            case 1: return x - y;
            case 2: return -x + y;
            case 3: return -x - y;
            default: return 0;
        }
    }
};

// Static member initialization
inline std::vector<double> Perlin::smooth_;
inline bool Perlin::smoothInitialized_ = false;

/**
 * FractalNoise - Multi-octave fractal noise
 * Faithful port from mfcg.js pg (Noise) class with fractal() factory
 */
class FractalNoise {
public:
    std::vector<Perlin> components;

    /**
     * Create fractal noise with multiple octaves
     * @param octaves Number of noise layers (default 1)
     * @param gridSize Initial grid size (default 1)
     * @param persistence Amplitude falloff per octave (default 0.5)
     */
    static FractalNoise create(int octaves = 1, double gridSize = 1.0, double persistence = 0.5) {
        FractalNoise noise;
        double amplitude = 1.0;
        double currentGridSize = gridSize;

        for (int i = 0; i < octaves; ++i) {
            // Generate seed from RNG (faithful to mfcg.js)
            int seed = Random::intVal(0, 2147483647);
            Perlin perlin(seed);
            perlin.gridSize = currentGridSize;
            perlin.amplitude = amplitude;
            noise.components.push_back(perlin);

            currentGridSize *= 2.0;
            amplitude *= persistence;
        }

        return noise;
    }

    double get(double x, double y) const {
        double result = 0;
        for (const auto& component : components) {
            result += component.get(x, y);
        }
        return result;
    }
};

} // namespace utils
} // namespace town_generator
