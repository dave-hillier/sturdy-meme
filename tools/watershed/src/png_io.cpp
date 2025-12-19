#include "png_io.h"
#include <lodepng.h>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <limits>
#include <fstream>

ElevationGrid downsample_elevation(const ElevationGrid& src, int target_size) {
    // Calculate scale to fit largest dimension into target_size
    int max_dim = std::max(src.width, src.height);
    if (target_size <= 0 || target_size >= max_dim) {
        return src; // No downsampling needed
    }

    float scale = static_cast<float>(target_size) / max_dim;
    int new_width = static_cast<int>(src.width * scale);
    int new_height = static_cast<int>(src.height * scale);

    // Ensure at least 1x1
    new_width = std::max(1, new_width);
    new_height = std::max(1, new_height);

    ElevationGrid dst;
    dst.width = new_width;
    dst.height = new_height;
    dst.data.resize(new_width * new_height);

    float x_ratio = static_cast<float>(src.width) / new_width;
    float y_ratio = static_cast<float>(src.height) / new_height;

    // Use max-pooling to preserve ridges and peaks (important for watershed)
    for (int dy = 0; dy < new_height; ++dy) {
        for (int dx = 0; dx < new_width; ++dx) {
            int sx_start = static_cast<int>(dx * x_ratio);
            int sy_start = static_cast<int>(dy * y_ratio);
            int sx_end = static_cast<int>((dx + 1) * x_ratio);
            int sy_end = static_cast<int>((dy + 1) * y_ratio);

            sx_end = std::min(sx_end, src.width);
            sy_end = std::min(sy_end, src.height);

            uint16_t max_val = 0;
            for (int sy = sy_start; sy < sy_end; ++sy) {
                for (int sx = sx_start; sx < sx_end; ++sx) {
                    max_val = std::max(max_val, src.at(sx, sy));
                }
            }
            dst.data[dy * new_width + dx] = max_val;
        }
    }

    return dst;
}

ElevationGrid read_elevation_png(const std::string& filename) {
    std::vector<unsigned char> image;
    unsigned width, height;

    // Try to load as 16-bit first
    lodepng::State state;
    std::vector<unsigned char> png_data;
    unsigned error = lodepng::load_file(png_data, filename);
    if (error) {
        throw std::runtime_error("Cannot open file: " + filename + " - " + lodepng_error_text(error));
    }

    error = lodepng::decode(image, width, height, state, png_data);
    if (error) {
        throw std::runtime_error("PNG decode error: " + std::string(lodepng_error_text(error)));
    }

    ElevationGrid grid;
    grid.width = static_cast<int>(width);
    grid.height = static_cast<int>(height);
    grid.data.resize(width * height);

    LodePNGColorType colorType = state.info_png.color.colortype;
    unsigned bitDepth = state.info_png.color.bitdepth;

    // Handle different color types and bit depths
    if (bitDepth == 16) {
        // 16-bit image - lodepng decodes to RGBA by default, but we can access raw
        // Re-decode with raw settings for 16-bit
        state.info_raw.colortype = LCT_GREY;
        state.info_raw.bitdepth = 16;
        state.decoder.color_convert = 1;

        image.clear();
        error = lodepng::decode(image, width, height, state, png_data);
        if (error) {
            throw std::runtime_error("PNG 16-bit decode error: " + std::string(lodepng_error_text(error)));
        }

        // Convert from big-endian 16-bit to native
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                size_t idx = (y * width + x) * 2;
                uint16_t value = (static_cast<uint16_t>(image[idx]) << 8) | image[idx + 1];
                grid.data[y * width + x] = value;
            }
        }
    } else {
        // 8-bit image - decode as grayscale and scale up
        state.info_raw.colortype = LCT_GREY;
        state.info_raw.bitdepth = 8;
        state.decoder.color_convert = 1;

        image.clear();
        error = lodepng::decode(image, width, height, state, png_data);
        if (error) {
            throw std::runtime_error("PNG 8-bit decode error: " + std::string(lodepng_error_text(error)));
        }

        // Scale 8-bit to 16-bit range
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                uint8_t value8 = image[y * width + x];
                grid.data[y * width + x] = static_cast<uint16_t>(value8) * 257; // Scale to 16-bit
            }
        }
    }

    return grid;
}

static void hsv_to_rgb(float h, float s, float v, uint16_t& r, uint16_t& g, uint16_t& b) {
    float c = v * s;
    float x = c * (1 - std::abs(std::fmod(h / 60.0f, 2) - 1));
    float m = v - c;

    float rf, gf, bf;
    if (h < 60) { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else { rf = c; gf = 0; bf = x; }

    r = static_cast<uint16_t>((rf + m) * 65535);
    g = static_cast<uint16_t>((gf + m) * 65535);
    b = static_cast<uint16_t>((bf + m) * 65535);
}

static void label_to_color(uint32_t label, uint16_t& r, uint16_t& g, uint16_t& b) {
    if (label == 0) {
        r = g = b = 0;
        return;
    }

    // Use golden ratio to spread colors evenly
    float hue = std::fmod(label * 137.508f, 360.0f);
    float sat = 0.6f + 0.4f * ((label * 31) % 100) / 100.0f;
    float val = 0.7f + 0.3f * ((label * 17) % 100) / 100.0f;

    hsv_to_rgb(hue, sat, val, r, g, b);
}

void write_watershed_png(const std::string& filename, const WatershedResult& watersheds) {
    std::vector<unsigned char> image(watersheds.width * watersheds.height * 3);

    for (int y = 0; y < watersheds.height; ++y) {
        for (int x = 0; x < watersheds.width; ++x) {
            uint32_t label = watersheds.labels[y * watersheds.width + x];
            uint16_t r, g, b;
            label_to_color(label, r, g, b);

            size_t idx = (y * watersheds.width + x) * 3;
            image[idx + 0] = static_cast<unsigned char>(r >> 8);
            image[idx + 1] = static_cast<unsigned char>(g >> 8);
            image[idx + 2] = static_cast<unsigned char>(b >> 8);
        }
    }

    unsigned error = lodepng::encode(filename, image, watersheds.width, watersheds.height, LCT_RGB, 8);
    if (error) {
        throw std::runtime_error("Cannot write file: " + filename + " - " + lodepng_error_text(error));
    }
}

void write_flow_accumulation_png(const std::string& filename, const D8Result& d8) {
    // Find max for log scaling
    uint32_t max_acc = 1;
    for (const auto& acc : d8.flow_accumulation) {
        max_acc = std::max(max_acc, acc);
    }
    double log_max = std::log(max_acc + 1);

    std::vector<unsigned char> image(d8.width * d8.height);

    for (int y = 0; y < d8.height; ++y) {
        for (int x = 0; x < d8.width; ++x) {
            uint32_t acc = d8.flow_accumulation[y * d8.width + x];
            double normalized = std::log(acc + 1) / log_max;
            image[y * d8.width + x] = static_cast<unsigned char>(normalized * 255);
        }
    }

    unsigned error = lodepng::encode(filename, image, d8.width, d8.height, LCT_GREY, 8);
    if (error) {
        throw std::runtime_error("Cannot write file: " + filename + " - " + lodepng_error_text(error));
    }
}

void write_rivers_png(const std::string& filename, const D8Result& d8, uint32_t threshold) {
    // Find max accumulation for intensity scaling
    uint32_t max_acc = 1;
    for (const auto& acc : d8.flow_accumulation) {
        if (acc >= threshold) {
            max_acc = std::max(max_acc, acc);
        }
    }
    double log_threshold = std::log(static_cast<double>(threshold));
    double log_max = std::log(static_cast<double>(max_acc));
    double log_range = log_max - log_threshold;
    if (log_range < 0.001) log_range = 1.0;

    std::vector<unsigned char> image(d8.width * d8.height);

    for (int y = 0; y < d8.height; ++y) {
        for (int x = 0; x < d8.width; ++x) {
            uint32_t acc = d8.flow_accumulation[y * d8.width + x];
            uint8_t value = 0;
            if (acc >= threshold) {
                double normalized = (std::log(static_cast<double>(acc)) - log_threshold) / log_range;
                value = static_cast<uint8_t>(16 + normalized * 239);
            }
            image[y * d8.width + x] = value;
        }
    }

    unsigned error = lodepng::encode(filename, image, d8.width, d8.height, LCT_GREY, 8);
    if (error) {
        throw std::runtime_error("Cannot write file: " + filename + " - " + lodepng_error_text(error));
    }
}

void write_traced_rivers_png(
    const std::string& filename,
    const std::vector<uint32_t>& river_map,
    int width, int height
) {
    // Find min/max for scaling (only non-zero values)
    uint32_t min_acc = std::numeric_limits<uint32_t>::max();
    uint32_t max_acc = 1;
    for (const auto& acc : river_map) {
        if (acc > 0) {
            min_acc = std::min(min_acc, acc);
            max_acc = std::max(max_acc, acc);
        }
    }
    if (min_acc == std::numeric_limits<uint32_t>::max()) min_acc = 1;

    double log_min = std::log(static_cast<double>(min_acc));
    double log_max = std::log(static_cast<double>(max_acc));
    double log_range = log_max - log_min;
    if (log_range < 0.001) log_range = 1.0;

    std::vector<unsigned char> image(width * height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t acc = river_map[y * width + x];
            uint8_t value = 0;
            if (acc > 0) {
                double normalized = (std::log(static_cast<double>(acc)) - log_min) / log_range;
                value = static_cast<uint8_t>(16 + normalized * 239);
            }
            image[y * width + x] = value;
        }
    }

    unsigned error = lodepng::encode(filename, image, width, height, LCT_GREY, 8);
    if (error) {
        throw std::runtime_error("Cannot write file: " + filename + " - " + lodepng_error_text(error));
    }
}

void write_terrain_rivers_png(
    const std::string& filename,
    const ElevationGrid& elevation,
    const D8Result& d8,
    uint32_t river_threshold,
    uint16_t sea_level
) {
    // Find max accumulation for river intensity scaling
    uint32_t max_acc = 1;
    for (const auto& acc : d8.flow_accumulation) {
        if (acc >= river_threshold) {
            max_acc = std::max(max_acc, acc);
        }
    }
    double log_threshold = std::log(static_cast<double>(river_threshold));
    double log_max = std::log(static_cast<double>(max_acc));
    double log_range = log_max - log_threshold;
    if (log_range < 0.001) log_range = 1.0;

    std::vector<unsigned char> image(elevation.width * elevation.height * 3);

    for (int y = 0; y < elevation.height; ++y) {
        for (int x = 0; x < elevation.width; ++x) {
            uint16_t elev_value = elevation.data[y * elevation.width + x];
            uint8_t red_value = 0;
            uint8_t green_value = 0;
            uint8_t blue_value = 0;

            if (elev_value <= sea_level) {
                blue_value = 255;
            } else {
                green_value = static_cast<uint8_t>(elev_value >> 8);

                uint32_t acc = d8.flow_accumulation[y * elevation.width + x];
                if (acc >= river_threshold) {
                    double normalized = (std::log(static_cast<double>(acc)) - log_threshold) / log_range;
                    red_value = static_cast<uint8_t>(16 + normalized * 239);
                }
            }

            size_t idx = (y * elevation.width + x) * 3;
            image[idx + 0] = red_value;
            image[idx + 1] = green_value;
            image[idx + 2] = blue_value;
        }
    }

    unsigned error = lodepng::encode(filename, image, elevation.width, elevation.height, LCT_RGB, 8);
    if (error) {
        throw std::runtime_error("Cannot write file: " + filename + " - " + lodepng_error_text(error));
    }
}

void write_terrain_traced_rivers_png(
    const std::string& filename,
    const ElevationGrid& elevation,
    const std::vector<uint32_t>& river_map,
    uint16_t sea_level
) {
    // Find min/max accumulation for river intensity scaling
    uint32_t min_acc = std::numeric_limits<uint32_t>::max();
    uint32_t max_acc = 1;
    for (const auto& acc : river_map) {
        if (acc > 0) {
            min_acc = std::min(min_acc, acc);
            max_acc = std::max(max_acc, acc);
        }
    }
    if (min_acc == std::numeric_limits<uint32_t>::max()) min_acc = 1;

    double log_min = std::log(static_cast<double>(min_acc));
    double log_max = std::log(static_cast<double>(max_acc));
    double log_range = log_max - log_min;
    if (log_range < 0.001) log_range = 1.0;

    std::vector<unsigned char> image(elevation.width * elevation.height * 3);

    for (int y = 0; y < elevation.height; ++y) {
        for (int x = 0; x < elevation.width; ++x) {
            uint16_t elev_value = elevation.data[y * elevation.width + x];
            uint8_t red_value = 0;
            uint8_t green_value = 0;
            uint8_t blue_value = 0;

            if (elev_value <= sea_level) {
                blue_value = 255;
            } else {
                green_value = static_cast<uint8_t>(elev_value >> 8);

                uint32_t acc = river_map[y * elevation.width + x];
                if (acc > 0) {
                    double normalized = (std::log(static_cast<double>(acc)) - log_min) / log_range;
                    red_value = static_cast<uint8_t>(16 + normalized * 239);
                }
            }

            size_t idx = (y * elevation.width + x) * 3;
            image[idx + 0] = red_value;
            image[idx + 1] = green_value;
            image[idx + 2] = blue_value;
        }
    }

    unsigned error = lodepng::encode(filename, image, elevation.width, elevation.height, LCT_RGB, 8);
    if (error) {
        throw std::runtime_error("Cannot write file: " + filename + " - " + lodepng_error_text(error));
    }
}

// Binary output functions for biome_preprocess compatibility

void write_flow_accumulation_bin(const std::string& filename, const D8Result& d8) {
    // Find max for normalization
    uint32_t max_acc = 1;
    for (const auto& acc : d8.flow_accumulation) {
        max_acc = std::max(max_acc, acc);
    }

    // Write header: width, height as uint32_t
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + filename);
    }

    uint32_t width = static_cast<uint32_t>(d8.width);
    uint32_t height = static_cast<uint32_t>(d8.height);
    file.write(reinterpret_cast<const char*>(&width), sizeof(width));
    file.write(reinterpret_cast<const char*>(&height), sizeof(height));

    // Write normalized flow accumulation as floats [0,1]
    std::vector<float> normalized(d8.flow_accumulation.size());
    for (size_t i = 0; i < d8.flow_accumulation.size(); ++i) {
        normalized[i] = static_cast<float>(d8.flow_accumulation[i]) / static_cast<float>(max_acc);
    }
    file.write(reinterpret_cast<const char*>(normalized.data()), normalized.size() * sizeof(float));

    if (!file.good()) {
        throw std::runtime_error("Error writing to file: " + filename);
    }
}

void write_flow_direction_bin(const std::string& filename, const D8Result& d8) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + filename);
    }

    uint32_t width = static_cast<uint32_t>(d8.width);
    uint32_t height = static_cast<uint32_t>(d8.height);
    file.write(reinterpret_cast<const char*>(&width), sizeof(width));
    file.write(reinterpret_cast<const char*>(&height), sizeof(height));

    // Convert uint8_t to int8_t (8 = no flow becomes -1)
    std::vector<int8_t> directions(d8.flow_direction.size());
    for (size_t i = 0; i < d8.flow_direction.size(); ++i) {
        if (d8.flow_direction[i] == 8) {
            directions[i] = -1;  // No flow / pit
        } else {
            directions[i] = static_cast<int8_t>(d8.flow_direction[i]);
        }
    }
    file.write(reinterpret_cast<const char*>(directions.data()), directions.size() * sizeof(int8_t));

    if (!file.good()) {
        throw std::runtime_error("Error writing to file: " + filename);
    }
}

void write_watershed_labels_bin(const std::string& filename, const WatershedResult& watersheds) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file for writing: " + filename);
    }

    uint32_t width = static_cast<uint32_t>(watersheds.width);
    uint32_t height = static_cast<uint32_t>(watersheds.height);
    file.write(reinterpret_cast<const char*>(&width), sizeof(width));
    file.write(reinterpret_cast<const char*>(&height), sizeof(height));

    file.write(reinterpret_cast<const char*>(watersheds.labels.data()),
               watersheds.labels.size() * sizeof(uint32_t));

    if (!file.good()) {
        throw std::runtime_error("Error writing to file: " + filename);
    }
}
