#pragma once

#include "DwellingPlan.h"
#include <vector>
#include <memory>
#include <string>

namespace dwelling {

// Polyomino shapes for building footprints (tetrominos and pentominos)
struct Polyomino {
    static std::vector<Cell> createShape(int minSize, int maxSize, std::mt19937& rng);

private:
    static constexpr const char* TETROS[] = {
        "## "   // L
        "## "
        "   ",

        "###"   // T
        " # "
        "   ",

        " ##"   // S
        "## "
        "   ",

        "###"   // I
        "   "
        "   ",

        "## "   // O
        "## "
        "   "
    };

    static constexpr const char* PENTOS[] = {
        "## "   // P
        "## "
        "#  ",

        " # "   // +
        "###"
        " # ",

        "###"   // U
        "# #"
        "   ",

        " ##"   // W
        "## "
        "#  ",

        "###"   // L
        "#  "
        "#  "
    };
};

// A complete dwelling with multiple floors
class House {
public:
    House(const DwellingParams& params);

    // Access floors
    const std::vector<std::unique_ptr<Plan>>& floors() const { return floors_; }
    Plan* floor(int index);
    const Plan* floor(int index) const;
    Plan* basement() { return basement_.get(); }
    const Plan* basement() const { return basement_.get(); }

    int numFloors() const { return static_cast<int>(floors_.size()); }
    const std::string& name() const { return name_; }

    // Get the underlying grid
    Grid* grid() { return grid_.get(); }
    const Grid* grid() const { return grid_.get(); }

    // Get grid dimensions (in cells)
    int gridWidth() const { return grid_ ? grid_->width() : 0; }
    int gridHeight() const { return grid_ ? grid_->height() : 0; }

    // Generate the house
    void generate();

private:
    void createFootprint();
    void createFloors();

    DwellingParams params_;
    std::unique_ptr<Grid> grid_;
    std::vector<Cell*> footprint_;
    std::vector<std::unique_ptr<Plan>> floors_;
    std::unique_ptr<Plan> basement_;
    std::string name_;
    std::mt19937 rng_;
};

} // namespace dwelling
