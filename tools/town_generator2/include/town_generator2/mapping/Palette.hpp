#pragma once

#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>

namespace town_generator2 {
namespace mapping {

/**
 * Palette - Color scheme for map rendering
 */
struct Palette {
    uint32_t paper;   // Background
    uint32_t light;   // Buildings
    uint32_t medium;  // Parks, roads
    uint32_t dark;    // Walls, outlines

    Palette(uint32_t paper_, uint32_t light_, uint32_t medium_, uint32_t dark_)
        : paper(paper_), light(light_), medium(medium_), dark(dark_) {}

    // Convert color to SVG hex string
    static std::string toHex(uint32_t color) {
        std::ostringstream ss;
        ss << "#" << std::hex << std::setfill('0') << std::setw(6) << (color & 0xFFFFFF);
        return ss.str();
    }

    std::string paperHex() const { return toHex(paper); }
    std::string lightHex() const { return toHex(light); }
    std::string mediumHex() const { return toHex(medium); }
    std::string darkHex() const { return toHex(dark); }

    // Predefined palettes from original
    static Palette DEFAULT() { return Palette(0xccc5b8, 0x99948a, 0x67635c, 0x1a1917); }
    static Palette BLUEPRINT() { return Palette(0x455b8d, 0x7383aa, 0xa1abc6, 0xfcfbff); }
    static Palette BW() { return Palette(0xffffff, 0xcccccc, 0x888888, 0x000000); }
    static Palette INK() { return Palette(0xcccac2, 0x9a979b, 0x6c6974, 0x130f26); }
    static Palette NIGHT() { return Palette(0x000000, 0x402306, 0x674b14, 0x99913d); }
    static Palette ANCIENT() { return Palette(0xccc5a3, 0xa69974, 0x806f4d, 0x342414); }
    static Palette COLOUR() { return Palette(0xfff2c8, 0xd6a36e, 0x869a81, 0x4c5950); }
    static Palette SIMPLE() { return Palette(0xffffff, 0x000000, 0x000000, 0x000000); }
};

} // namespace mapping
} // namespace town_generator2
