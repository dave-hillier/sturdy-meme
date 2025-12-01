#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <bitset>
#include <string>

// Connection types for module faces
enum class ConnectionType : uint8_t {
    None = 0,       // Empty space / air
    Wall,           // Solid wall connection
    WallOpen,       // Wall with opening (door/window)
    Floor,          // Horizontal floor connection
    RoofSlope,      // Sloped roof surface
    RoofEdge,       // Edge of roof
    Corner,         // Corner post
    COUNT
};

// Direction for module faces
enum class Direction : uint8_t {
    North = 0,  // -Z
    South,      // +Z
    East,       // +X
    West,       // -X
    Up,         // +Y
    Down,       // -Y
    COUNT
};

inline Direction oppositeDirection(Direction d) {
    switch (d) {
        case Direction::North: return Direction::South;
        case Direction::South: return Direction::North;
        case Direction::East: return Direction::West;
        case Direction::West: return Direction::East;
        case Direction::Up: return Direction::Down;
        case Direction::Down: return Direction::Up;
        default: return d;
    }
}

inline glm::ivec3 directionOffset(Direction d) {
    switch (d) {
        case Direction::North: return {0, 0, -1};
        case Direction::South: return {0, 0, 1};
        case Direction::East: return {1, 0, 0};
        case Direction::West: return {-1, 0, 0};
        case Direction::Up: return {0, 1, 0};
        case Direction::Down: return {0, -1, 0};
        default: return {0, 0, 0};
    }
}

// Module type categories
enum class ModuleCategory : uint8_t {
    Empty,
    Foundation,
    Wall,
    Corner,
    Floor,
    Roof,
    RoofCorner,
    Decorative
};

// Specific module types
enum class ModuleType : uint8_t {
    // Empty
    Air = 0,

    // Foundation (ground level)
    FoundationWall,
    FoundationCorner,
    FoundationDoor,

    // Walls
    WallPlain,
    WallWindow,
    WallHalfTimber,
    WallHalfTimberWindow,

    // Corners
    CornerOuter,
    CornerInner,

    // Interior
    FloorPlain,

    // Roof pieces
    RoofFlat,           // Flat section (for multi-level)
    RoofSlopeN,         // Slopes down toward North
    RoofSlopeS,         // Slopes down toward South
    RoofSlopeE,         // Slopes down toward East
    RoofSlopeW,         // Slopes down toward West
    RoofRidgeNS,        // Ridge running N-S
    RoofRidgeEW,        // Ridge running E-W
    RoofHipNE,          // Hip corner
    RoofHipNW,
    RoofHipSE,
    RoofHipSW,
    RoofGableN,         // Gable end
    RoofGableS,
    RoofGableE,
    RoofGableW,

    // Decorative
    Chimney,

    COUNT
};

// A building module with its connection rules
struct BuildingModule {
    ModuleType type;
    ModuleCategory category;
    std::string name;

    // Connection type for each face
    std::array<ConnectionType, 6> connections;  // Indexed by Direction

    // Weight for random selection (higher = more likely)
    float weight = 1.0f;

    // Can this module be at ground level?
    bool allowedAtGround = false;

    // Can this module be at the top?
    bool allowedAtTop = false;

    // Rotation variants (0-3, representing 90-degree rotations around Y)
    uint8_t rotation = 0;

    // For mesh generation - dimensions of module in local units
    glm::vec3 meshOffset{0.0f};

    ConnectionType getConnection(Direction dir) const {
        return connections[static_cast<size_t>(dir)];
    }

    bool canConnectTo(const BuildingModule& other, Direction toOther) const {
        ConnectionType myConn = getConnection(toOther);
        ConnectionType theirConn = other.getConnection(oppositeDirection(toOther));

        // Connection compatibility rules
        if (myConn == ConnectionType::None && theirConn == ConnectionType::None) return true;
        if (myConn == ConnectionType::Wall && theirConn == ConnectionType::Wall) return true;
        if (myConn == ConnectionType::WallOpen && theirConn == ConnectionType::WallOpen) return true;
        if (myConn == ConnectionType::Floor && theirConn == ConnectionType::Floor) return true;
        if (myConn == ConnectionType::RoofSlope && theirConn == ConnectionType::RoofSlope) return true;
        if (myConn == ConnectionType::RoofEdge && theirConn == ConnectionType::None) return true;
        if (myConn == ConnectionType::None && theirConn == ConnectionType::RoofEdge) return true;
        if (myConn == ConnectionType::Corner && theirConn == ConnectionType::Corner) return true;
        if (myConn == ConnectionType::Corner && theirConn == ConnectionType::Wall) return true;
        if (myConn == ConnectionType::Wall && theirConn == ConnectionType::Corner) return true;

        return false;
    }
};

// Module library - all available modules
class ModuleLibrary {
public:
    void init();

    const BuildingModule& getModule(size_t index) const { return modules[index]; }
    const std::vector<BuildingModule>& getAllModules() const { return modules; }
    size_t getModuleCount() const { return modules.size(); }

    // Get indices of modules that can be placed at specific locations
    std::vector<size_t> getGroundModules() const;
    std::vector<size_t> getTopModules() const;
    std::vector<size_t> getModulesByCategory(ModuleCategory cat) const;

private:
    void addModule(ModuleType type, ModuleCategory cat, const std::string& name,
                   const std::array<ConnectionType, 6>& conns, float weight,
                   bool ground, bool top);

    // Add rotated variants of a module
    void addModuleWithRotations(ModuleType baseType, ModuleCategory cat, const std::string& baseName,
                                const std::array<ConnectionType, 6>& conns, float weight,
                                bool ground, bool top);

    std::vector<BuildingModule> modules;
};

// Cell in the WFC grid - tracks possible modules
struct WFCCell {
    std::vector<bool> possible;  // Which modules are still possible
    int possibleCount = 0;       // Cached count of possibilities
    bool collapsed = false;
    size_t chosenModule = 0;

    void init(size_t moduleCount) {
        possible.resize(moduleCount, true);
        possibleCount = static_cast<int>(moduleCount);
        collapsed = false;
    }

    void eliminate(size_t moduleIndex) {
        if (possible[moduleIndex]) {
            possible[moduleIndex] = false;
            possibleCount--;
        }
    }

    bool isPossible(size_t moduleIndex) const {
        return possible[moduleIndex];
    }

    float entropy() const {
        return static_cast<float>(possibleCount);
    }
};

// WFC solver for building generation
class BuildingWFC {
public:
    void init(const ModuleLibrary& library, int width, int height, int depth);

    // Run WFC to generate a building
    bool solve(uint32_t seed);

    // Get the result
    const WFCCell& getCell(int x, int y, int z) const;
    glm::ivec3 getSize() const { return gridSize; }

    // Apply initial constraints based on building footprint
    void setFootprint(const std::vector<glm::ivec2>& footprint);
    void setHeight(int minHeight, int maxHeight);

private:
    // Find cell with minimum entropy
    glm::ivec3 findMinEntropyCell() const;

    // Collapse a cell to a specific module
    void collapse(const glm::ivec3& pos, size_t moduleIndex);

    // Propagate constraints from a cell
    bool propagate(const glm::ivec3& startPos);

    // Check if position is valid
    bool isValid(const glm::ivec3& pos) const;

    // Get cell at position
    WFCCell& cellAt(const glm::ivec3& pos);
    const WFCCell& cellAt(const glm::ivec3& pos) const;

    // Random selection weighted by module weights
    size_t weightedRandomChoice(const WFCCell& cell, float random) const;

    const ModuleLibrary* library = nullptr;
    std::vector<WFCCell> grid;
    glm::ivec3 gridSize{0};

    // Constraints
    std::vector<glm::ivec2> buildingFootprint;
    int minBuildingHeight = 1;
    int maxBuildingHeight = 3;
};
