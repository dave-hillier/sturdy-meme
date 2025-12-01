#include "BuildingModules.h"
#include <algorithm>
#include <random>
#include <queue>
#include <cmath>

void ModuleLibrary::init() {
    modules.clear();

    using CT = ConnectionType;
    using Dir = Direction;

    // Helper to create connection array
    auto conns = [](CT n, CT s, CT e, CT w, CT u, CT d) -> std::array<CT, 6> {
        return {n, s, e, w, u, d};  // North, South, East, West, Up, Down
    };

    // Air (empty space)
    addModule(ModuleType::Air, ModuleCategory::Empty, "Air",
              conns(CT::None, CT::None, CT::None, CT::None, CT::None, CT::None),
              0.1f, false, true);

    // Foundation modules (ground level only)
    addModule(ModuleType::FoundationWall, ModuleCategory::Foundation, "FoundationWall",
              conns(CT::None, CT::Wall, CT::Wall, CT::Wall, CT::Floor, CT::None),
              1.0f, true, false);

    addModule(ModuleType::FoundationCorner, ModuleCategory::Foundation, "FoundationCorner",
              conns(CT::None, CT::Wall, CT::Wall, CT::None, CT::Floor, CT::None),
              0.8f, true, false);

    addModule(ModuleType::FoundationDoor, ModuleCategory::Foundation, "FoundationDoor",
              conns(CT::None, CT::WallOpen, CT::Wall, CT::Wall, CT::Floor, CT::None),
              0.3f, true, false);

    // Wall modules
    addModule(ModuleType::WallPlain, ModuleCategory::Wall, "WallPlain",
              conns(CT::None, CT::Wall, CT::Wall, CT::Wall, CT::Floor, CT::Floor),
              2.0f, false, false);

    addModule(ModuleType::WallWindow, ModuleCategory::Wall, "WallWindow",
              conns(CT::None, CT::WallOpen, CT::Wall, CT::Wall, CT::Floor, CT::Floor),
              1.5f, false, false);

    addModule(ModuleType::WallHalfTimber, ModuleCategory::Wall, "WallHalfTimber",
              conns(CT::None, CT::Wall, CT::Wall, CT::Wall, CT::Floor, CT::Floor),
              1.2f, false, false);

    addModule(ModuleType::WallHalfTimberWindow, ModuleCategory::Wall, "WallHalfTimberWindow",
              conns(CT::None, CT::WallOpen, CT::Wall, CT::Wall, CT::Floor, CT::Floor),
              0.8f, false, false);

    // Corner modules
    addModule(ModuleType::CornerOuter, ModuleCategory::Corner, "CornerOuter",
              conns(CT::None, CT::Corner, CT::Corner, CT::None, CT::Floor, CT::Floor),
              1.0f, false, false);

    addModule(ModuleType::CornerInner, ModuleCategory::Corner, "CornerInner",
              conns(CT::Wall, CT::Wall, CT::Wall, CT::Wall, CT::Floor, CT::Floor),
              0.5f, false, false);

    // Floor module (interior)
    addModule(ModuleType::FloorPlain, ModuleCategory::Floor, "FloorPlain",
              conns(CT::Wall, CT::Wall, CT::Wall, CT::Wall, CT::Floor, CT::Floor),
              1.5f, false, false);

    // Roof modules
    addModule(ModuleType::RoofFlat, ModuleCategory::Roof, "RoofFlat",
              conns(CT::RoofEdge, CT::RoofEdge, CT::RoofEdge, CT::RoofEdge, CT::None, CT::Floor),
              0.5f, false, true);

    // Sloped roof pieces (with rotations handled explicitly)
    addModule(ModuleType::RoofSlopeN, ModuleCategory::Roof, "RoofSlopeN",
              conns(CT::RoofEdge, CT::RoofSlope, CT::RoofSlope, CT::RoofSlope, CT::None, CT::Floor),
              1.0f, false, true);

    addModule(ModuleType::RoofSlopeS, ModuleCategory::Roof, "RoofSlopeS",
              conns(CT::RoofSlope, CT::RoofEdge, CT::RoofSlope, CT::RoofSlope, CT::None, CT::Floor),
              1.0f, false, true);

    addModule(ModuleType::RoofSlopeE, ModuleCategory::Roof, "RoofSlopeE",
              conns(CT::RoofSlope, CT::RoofSlope, CT::RoofEdge, CT::RoofSlope, CT::None, CT::Floor),
              1.0f, false, true);

    addModule(ModuleType::RoofSlopeW, ModuleCategory::Roof, "RoofSlopeW",
              conns(CT::RoofSlope, CT::RoofSlope, CT::RoofSlope, CT::RoofEdge, CT::None, CT::Floor),
              1.0f, false, true);

    // Ridge pieces
    addModule(ModuleType::RoofRidgeNS, ModuleCategory::Roof, "RoofRidgeNS",
              conns(CT::RoofSlope, CT::RoofSlope, CT::RoofEdge, CT::RoofEdge, CT::None, CT::Floor),
              0.8f, false, true);

    addModule(ModuleType::RoofRidgeEW, ModuleCategory::Roof, "RoofRidgeEW",
              conns(CT::RoofEdge, CT::RoofEdge, CT::RoofSlope, CT::RoofSlope, CT::None, CT::Floor),
              0.8f, false, true);

    // Hip corners
    addModule(ModuleType::RoofHipNE, ModuleCategory::RoofCorner, "RoofHipNE",
              conns(CT::RoofEdge, CT::RoofSlope, CT::RoofEdge, CT::RoofSlope, CT::None, CT::Floor),
              0.6f, false, true);

    addModule(ModuleType::RoofHipNW, ModuleCategory::RoofCorner, "RoofHipNW",
              conns(CT::RoofEdge, CT::RoofSlope, CT::RoofSlope, CT::RoofEdge, CT::None, CT::Floor),
              0.6f, false, true);

    addModule(ModuleType::RoofHipSE, ModuleCategory::RoofCorner, "RoofHipSE",
              conns(CT::RoofSlope, CT::RoofEdge, CT::RoofEdge, CT::RoofSlope, CT::None, CT::Floor),
              0.6f, false, true);

    addModule(ModuleType::RoofHipSW, ModuleCategory::RoofCorner, "RoofHipSW",
              conns(CT::RoofSlope, CT::RoofEdge, CT::RoofSlope, CT::RoofEdge, CT::None, CT::Floor),
              0.6f, false, true);

    // Gable ends
    addModule(ModuleType::RoofGableN, ModuleCategory::Roof, "RoofGableN",
              conns(CT::RoofEdge, CT::Wall, CT::RoofSlope, CT::RoofSlope, CT::None, CT::Floor),
              0.5f, false, true);

    addModule(ModuleType::RoofGableS, ModuleCategory::Roof, "RoofGableS",
              conns(CT::Wall, CT::RoofEdge, CT::RoofSlope, CT::RoofSlope, CT::None, CT::Floor),
              0.5f, false, true);

    addModule(ModuleType::RoofGableE, ModuleCategory::Roof, "RoofGableE",
              conns(CT::RoofSlope, CT::RoofSlope, CT::RoofEdge, CT::Wall, CT::None, CT::Floor),
              0.5f, false, true);

    addModule(ModuleType::RoofGableW, ModuleCategory::Roof, "RoofGableW",
              conns(CT::RoofSlope, CT::RoofSlope, CT::Wall, CT::RoofEdge, CT::None, CT::Floor),
              0.5f, false, true);

    // Decorative
    addModule(ModuleType::Chimney, ModuleCategory::Decorative, "Chimney",
              conns(CT::None, CT::None, CT::None, CT::None, CT::None, CT::RoofSlope),
              0.1f, false, true);
}

void ModuleLibrary::addModule(ModuleType type, ModuleCategory cat, const std::string& name,
                              const std::array<ConnectionType, 6>& conns, float weight,
                              bool ground, bool top) {
    BuildingModule mod;
    mod.type = type;
    mod.category = cat;
    mod.name = name;
    mod.connections = conns;
    mod.weight = weight;
    mod.allowedAtGround = ground;
    mod.allowedAtTop = top;
    mod.rotation = 0;
    modules.push_back(mod);
}

std::vector<size_t> ModuleLibrary::getGroundModules() const {
    std::vector<size_t> result;
    for (size_t i = 0; i < modules.size(); ++i) {
        if (modules[i].allowedAtGround) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<size_t> ModuleLibrary::getTopModules() const {
    std::vector<size_t> result;
    for (size_t i = 0; i < modules.size(); ++i) {
        if (modules[i].allowedAtTop) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<size_t> ModuleLibrary::getModulesByCategory(ModuleCategory cat) const {
    std::vector<size_t> result;
    for (size_t i = 0; i < modules.size(); ++i) {
        if (modules[i].category == cat) {
            result.push_back(i);
        }
    }
    return result;
}

// WFC Implementation

void BuildingWFC::init(const ModuleLibrary& lib, int width, int height, int depth) {
    library = &lib;
    gridSize = glm::ivec3(width, height, depth);

    grid.resize(width * height * depth);
    for (auto& cell : grid) {
        cell.init(library->getModuleCount());
    }
}

bool BuildingWFC::isValid(const glm::ivec3& pos) const {
    return pos.x >= 0 && pos.x < gridSize.x &&
           pos.y >= 0 && pos.y < gridSize.y &&
           pos.z >= 0 && pos.z < gridSize.z;
}

WFCCell& BuildingWFC::cellAt(const glm::ivec3& pos) {
    return grid[pos.x + pos.y * gridSize.x + pos.z * gridSize.x * gridSize.y];
}

const WFCCell& BuildingWFC::cellAt(const glm::ivec3& pos) const {
    return grid[pos.x + pos.y * gridSize.x + pos.z * gridSize.x * gridSize.y];
}

const WFCCell& BuildingWFC::getCell(int x, int y, int z) const {
    return cellAt(glm::ivec3(x, y, z));
}

void BuildingWFC::setFootprint(const std::vector<glm::ivec2>& footprint) {
    buildingFootprint = footprint;

    // Apply footprint constraints: cells outside footprint can only be Air
    for (int y = 0; y < gridSize.y; ++y) {
        for (int z = 0; z < gridSize.z; ++z) {
            for (int x = 0; x < gridSize.x; ++x) {
                glm::ivec2 pos2D(x, z);
                bool inFootprint = std::find(footprint.begin(), footprint.end(), pos2D) != footprint.end();

                if (!inFootprint) {
                    // Outside footprint - only Air allowed
                    WFCCell& cell = cellAt(glm::ivec3(x, y, z));
                    for (size_t m = 0; m < library->getModuleCount(); ++m) {
                        if (library->getModule(m).type != ModuleType::Air) {
                            cell.eliminate(m);
                        }
                    }
                }
            }
        }
    }
}

void BuildingWFC::setHeight(int minH, int maxH) {
    minBuildingHeight = minH;
    maxBuildingHeight = maxH;

    // Apply height constraints
    // Ground level: only foundation modules
    for (int z = 0; z < gridSize.z; ++z) {
        for (int x = 0; x < gridSize.x; ++x) {
            WFCCell& groundCell = cellAt(glm::ivec3(x, 0, z));
            for (size_t m = 0; m < library->getModuleCount(); ++m) {
                const auto& mod = library->getModule(m);
                if (!mod.allowedAtGround && mod.type != ModuleType::Air) {
                    groundCell.eliminate(m);
                }
            }
        }
    }

    // Top level: only roof modules or air
    for (int z = 0; z < gridSize.z; ++z) {
        for (int x = 0; x < gridSize.x; ++x) {
            WFCCell& topCell = cellAt(glm::ivec3(x, gridSize.y - 1, z));
            for (size_t m = 0; m < library->getModuleCount(); ++m) {
                const auto& mod = library->getModule(m);
                if (!mod.allowedAtTop) {
                    topCell.eliminate(m);
                }
            }
        }
    }
}

glm::ivec3 BuildingWFC::findMinEntropyCell() const {
    glm::ivec3 minPos(-1);
    float minEntropy = std::numeric_limits<float>::max();

    for (int z = 0; z < gridSize.z; ++z) {
        for (int y = 0; y < gridSize.y; ++y) {
            for (int x = 0; x < gridSize.x; ++x) {
                const WFCCell& cell = cellAt(glm::ivec3(x, y, z));
                if (!cell.collapsed && cell.possibleCount > 0) {
                    float entropy = cell.entropy();
                    if (entropy < minEntropy) {
                        minEntropy = entropy;
                        minPos = glm::ivec3(x, y, z);
                    }
                }
            }
        }
    }

    return minPos;
}

size_t BuildingWFC::weightedRandomChoice(const WFCCell& cell, float random) const {
    // Calculate total weight of possible modules
    float totalWeight = 0.0f;
    for (size_t m = 0; m < library->getModuleCount(); ++m) {
        if (cell.isPossible(m)) {
            totalWeight += library->getModule(m).weight;
        }
    }

    if (totalWeight <= 0.0f) return 0;

    // Pick based on weighted random
    float target = random * totalWeight;
    float accumulated = 0.0f;

    for (size_t m = 0; m < library->getModuleCount(); ++m) {
        if (cell.isPossible(m)) {
            accumulated += library->getModule(m).weight;
            if (accumulated >= target) {
                return m;
            }
        }
    }

    // Fallback: return first possible
    for (size_t m = 0; m < library->getModuleCount(); ++m) {
        if (cell.isPossible(m)) return m;
    }

    return 0;
}

void BuildingWFC::collapse(const glm::ivec3& pos, size_t moduleIndex) {
    WFCCell& cell = cellAt(pos);
    cell.collapsed = true;
    cell.chosenModule = moduleIndex;

    // Eliminate all other possibilities
    for (size_t m = 0; m < library->getModuleCount(); ++m) {
        if (m != moduleIndex) {
            cell.eliminate(m);
        }
    }
    cell.possibleCount = 1;
}

bool BuildingWFC::propagate(const glm::ivec3& startPos) {
    std::queue<glm::ivec3> worklist;
    worklist.push(startPos);

    while (!worklist.empty()) {
        glm::ivec3 pos = worklist.front();
        worklist.pop();

        const WFCCell& cell = cellAt(pos);

        // Check each direction
        for (int d = 0; d < 6; ++d) {
            Direction dir = static_cast<Direction>(d);
            glm::ivec3 neighborPos = pos + directionOffset(dir);

            if (!isValid(neighborPos)) continue;

            WFCCell& neighbor = cellAt(neighborPos);
            if (neighbor.collapsed) continue;

            // For each possible module in neighbor, check if any of our possibilities can connect to it
            bool changed = false;
            for (size_t nm = 0; nm < library->getModuleCount(); ++nm) {
                if (!neighbor.isPossible(nm)) continue;

                const BuildingModule& neighborMod = library->getModule(nm);

                // Check if any of our possibilities can connect
                bool canConnect = false;
                for (size_t m = 0; m < library->getModuleCount(); ++m) {
                    if (!cell.isPossible(m)) continue;

                    const BuildingModule& mod = library->getModule(m);
                    if (mod.canConnectTo(neighborMod, dir)) {
                        canConnect = true;
                        break;
                    }
                }

                if (!canConnect) {
                    neighbor.eliminate(nm);
                    changed = true;
                }
            }

            if (neighbor.possibleCount == 0) {
                return false;  // Contradiction!
            }

            if (changed) {
                worklist.push(neighborPos);
            }
        }
    }

    return true;
}

bool BuildingWFC::solve(uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    int maxIterations = gridSize.x * gridSize.y * gridSize.z;
    int iterations = 0;

    while (iterations++ < maxIterations) {
        // Find cell with minimum entropy
        glm::ivec3 pos = findMinEntropyCell();

        if (pos.x < 0) {
            // All cells collapsed - success!
            return true;
        }

        WFCCell& cell = cellAt(pos);

        if (cell.possibleCount == 0) {
            // Contradiction
            return false;
        }

        // Collapse to a random possibility
        size_t choice = weightedRandomChoice(cell, dist(rng));
        collapse(pos, choice);

        // Propagate constraints
        if (!propagate(pos)) {
            return false;  // Contradiction during propagation
        }
    }

    return false;  // Ran out of iterations
}
