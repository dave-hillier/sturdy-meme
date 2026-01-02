#include "DwellingHouse.h"
#include <algorithm>
#include <set>
#include <sstream>

namespace dwelling {

// Tetromino shapes (4 cells each)
static const std::vector<std::string> TETRO_SHAPES = {
    "## "
    "## "
    "   ",  // O

    "###"
    " # "
    "   ",  // T

    " ##"
    "## "
    "   ",  // S

    "## "
    " ##"
    "   ",  // Z

    "###"
    "#  "
    "   ",  // L

    "###"
    "  #"
    "   ",  // J

    "###"
    "   "
    "   "   // I (partial)
};

// Pentomino shapes (5 cells each)
static const std::vector<std::string> PENTO_SHAPES = {
    "## "
    "## "
    "#  ",  // P

    " # "
    "###"
    " # ",  // +

    "###"
    "# #"
    "   ",  // U

    " ##"
    "## "
    "#  ",  // W

    "###"
    "#  "
    "#  ",  // L

    "## "
    " # "
    " ##",  // Z (big)

    " # "
    "## "
    " ##",  // S (big)

    "###"
    "# #"
    "# #"   // Custom house shape
};

std::vector<Cell> Polyomino::createShape(int minSize, int maxSize, std::mt19937& rng) {
    // Create a larger grid to work with
    int gridSize = maxSize * 3 + 2;
    std::vector<std::vector<bool>> grid(gridSize, std::vector<bool>(gridSize, false));

    // Random parameters
    std::uniform_int_distribution<int> sizeDist(minSize, maxSize);
    std::uniform_int_distribution<int> boolDist(0, 1);

    bool mirrorX = boolDist(rng);
    bool mirrorY = boolDist(rng);
    bool rotate = boolDist(rng);

    // Generate column and row sizes
    std::vector<int> cols(3), rows(3);
    for (int i = 0; i < 3; ++i) {
        cols[i] = sizeDist(rng);
        rows[i] = sizeDist(rng);
    }

    // Pick a random base shape
    std::uniform_int_distribution<size_t> shapeDist(0, TETRO_SHAPES.size() + PENTO_SHAPES.size() - 1);
    size_t shapeIdx = shapeDist(rng);

    const std::string& shape = shapeIdx < TETRO_SHAPES.size()
        ? TETRO_SHAPES[shapeIdx]
        : PENTO_SHAPES[shapeIdx - TETRO_SHAPES.size()];

    // Draw the shape onto the grid
    auto setCell = [&](int x, int y) {
        if (mirrorX) x = 2 - x;
        if (mirrorY) y = 2 - y;
        if (rotate) std::swap(x, y);

        int startX = 1, startY = 1;
        for (int i = 0; i < x; ++i) startX += cols[i];
        for (int i = 0; i < y; ++i) startY += rows[i];

        // Fill rectangle with some random variation
        std::uniform_int_distribution<int> varDist(0, 2);
        int x1 = startX - varDist(rng);
        int x2 = startX + cols[x] + varDist(rng);
        int y1 = startY - varDist(rng);
        int y2 = startY + rows[y] + varDist(rng);

        for (int cy = std::max(0, y1); cy < std::min(gridSize, y2); ++cy) {
            for (int cx = std::max(0, x1); cx < std::min(gridSize, x2); ++cx) {
                grid[cy][cx] = true;
            }
        }
    };

    // Parse shape string and set cells
    for (size_t i = 0; i < shape.size(); ++i) {
        if (shape[i] != ' ' && shape[i] != '\n') {
            int x = static_cast<int>(i % 3);
            int y = static_cast<int>(i / 3);
            setCell(x, y);
        }
    }

    // Collect cells
    std::vector<Cell> cells;
    for (int y = 0; y < gridSize; ++y) {
        for (int x = 0; x < gridSize; ++x) {
            if (grid[y][x]) {
                cells.push_back(Cell{y, x});
            }
        }
    }

    // Normalize to start at (0,0)
    if (!cells.empty()) {
        int minI = cells[0].i, minJ = cells[0].j;
        for (const Cell& c : cells) {
            minI = std::min(minI, c.i);
            minJ = std::min(minJ, c.j);
        }
        for (Cell& c : cells) {
            c.i -= minI;
            c.j -= minJ;
        }
    }

    return cells;
}

House::House(const DwellingParams& params)
    : params_(params), rng_(params.seed)
{
    // Generate a name
    static const std::vector<std::string> prefixes = {
        "Oak", "Maple", "Stone", "River", "Hill", "Rose", "Ivy", "Cedar",
        "Willow", "Brook", "Glen", "Haven", "Crest", "Dale"
    };
    static const std::vector<std::string> suffixes = {
        "House", "Cottage", "Manor", "Lodge", "Villa", "Home", "Place"
    };

    std::uniform_int_distribution<size_t> prefixDist(0, prefixes.size() - 1);
    std::uniform_int_distribution<size_t> suffixDist(0, suffixes.size() - 1);
    name_ = prefixes[prefixDist(rng_)] + " " + suffixes[suffixDist(rng_)];
}

Plan* House::floor(int index) {
    if (index < 0 || index >= static_cast<int>(floors_.size())) return nullptr;
    return floors_[index].get();
}

const Plan* House::floor(int index) const {
    if (index < 0 || index >= static_cast<int>(floors_.size())) return nullptr;
    return floors_[index].get();
}

void House::generate() {
    createFootprint();
    createFloors();
}

void House::createFootprint() {
    // Create building footprint using polyomino
    std::vector<Cell> shapeCells = Polyomino::createShape(
        params_.minCellSize, params_.maxCellSize, rng_);

    if (shapeCells.empty()) return;

    // Find bounds
    int maxI = 0, maxJ = 0;
    for (const Cell& c : shapeCells) {
        maxI = std::max(maxI, c.i);
        maxJ = std::max(maxJ, c.j);
    }

    // Create grid
    grid_ = std::make_unique<Grid>(maxJ + 1, maxI + 1);

    // Set footprint cells
    footprint_.clear();
    for (const Cell& c : shapeCells) {
        Cell* gridCell = grid_->cell(c.i, c.j);
        if (gridCell) {
            footprint_.push_back(gridCell);
        }
    }
}

void House::createFloors() {
    floors_.clear();

    if (footprint_.empty()) return;

    // Create each floor
    for (int f = 0; f < params_.numFloors; ++f) {
        auto plan = std::make_unique<Plan>(grid_.get(), footprint_,
            params_.seed + static_cast<uint32_t>(f) * 1000);
        plan->setParams(params_);
        plan->generate();
        plan->assignRooms();
        plan->assignDoors();
        plan->spawnWindows();
        floors_.push_back(std::move(plan));
    }
}

} // namespace dwelling
