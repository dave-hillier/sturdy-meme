#pragma once

#include "DwellingGrid.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <random>

namespace dwelling {

// Forward declarations
class Room;
class Plan;

// Door types
enum class DoorType {
    Doorway,    // Opening without door
    Regular,    // Standard door
    Double      // Double doors
};

// Door between rooms or to exterior
struct Door {
    Room* room1 = nullptr;  // First room (nullptr if exterior)
    Room* room2 = nullptr;  // Second room (nullptr if exterior)
    Edge edge;              // Position of door
    DoorType type = DoorType::Regular;

    bool isExterior() const { return room1 == nullptr || room2 == nullptr; }
};

// Window on exterior wall
struct Window {
    Room* room = nullptr;
    Edge edge;              // Position on wall
};

// Staircase types
enum class StairType {
    Regular,    // Standard straight stairs
    Spiral      // Spiral/circular staircase
};

// Staircase in a room
struct Stair {
    Cell cell;              // Position in grid
    Dir direction;          // Direction stairs face
    bool goingUp;           // Going up or down
    StairType type = StairType::Regular;
    Room* room = nullptr;   // Room containing the stair
};

// Room types
enum class RoomType {
    Unassigned,
    Hall,
    Kitchen,
    DiningRoom,
    LivingRoom,
    Bedroom,
    Bathroom,
    Study,
    Storage,
    Attic,
    Cellar,
    // Additional types from original
    Library,
    Chapel,
    Gallery,
    Workshop,
    Corridor,
    Stairhall,
    Armoury,
    Salon,
    Nursery,
    Pantry
};

std::string roomTypeName(RoomType type);

// A room in the dwelling
class Room {
public:
    Room(Plan* plan, const std::vector<Edge>& contour);

    Plan* plan() const { return plan_; }
    const std::vector<Edge>& contour() const { return contour_; }
    const std::vector<Cell*>& area() const { return area_; }
    int size() const { return static_cast<int>(area_.size()); }

    RoomType type() const { return type_; }
    void setType(RoomType t) { type_ = t; }

    const std::string& name() const { return name_; }
    void setName(const std::string& n) { name_ = n; }

    // Get all doors for this room
    std::vector<Door*> getDoors() const;

    // Get all windows for this room
    std::vector<Window*> getWindows() const;

    // Check if cell is in this room
    bool contains(const Cell& c) const;

    // Check if edge is on the contour
    bool hasEdge(const Edge& e) const;

private:
    Plan* plan_;
    std::vector<Edge> contour_;
    std::vector<Cell*> area_;
    RoomType type_ = RoomType::Unassigned;
    std::string name_;
};

// Style tags for generation
enum class DwellingStyle {
    Natural,     // Default organic shapes
    Mechanical,  // Prefer corners, more regular
    Organic,     // Prefer walls, irregular
    Gothic       // Use gothic room set
};

// Parameters for dwelling generation
struct DwellingParams {
    int minSectionSize = 3;   // Minimum polyomino section size (building footprint)
    int maxSectionSize = 7;   // Maximum polyomino section size (building footprint)
    float avgRoomSize = 6.0f; // Average room size in cells
    float roomSizeChaos = 1.0f; // Variation in room sizes
    bool preferCorners = false;  // Mechanical style
    bool preferWalls = false;    // Organic style
    bool regularRooms = false;   // Prefer rectangular rooms
    bool noNooks = false;        // Avoid hallway nooks
    float windowDensity = 0.7f;
    int numFloors = 1;
    bool hasBasement = false;
    DwellingStyle style = DwellingStyle::Natural;
    uint32_t seed = 12345;
};

// A floor plan of the dwelling
class Plan {
public:
    Plan(Grid* grid, const std::vector<Cell*>& area, uint32_t seed);

    Grid* grid() const { return grid_; }
    const std::vector<Edge>& contour() const { return contour_; }
    const std::vector<Room*>& rooms() const { return rooms_; }
    const std::vector<Door>& doors() const { return doors_; }
    const std::vector<Window>& windows() const { return windows_; }
    const std::vector<Stair>& stairs() const { return stairs_; }

    // Get the entrance door
    const Door* entrance() const { return entrance_; }

    // Room finding
    Room* getRoom(const Cell& c);
    Room* getRoom(const Edge& e);

    // Division parameters
    void setParams(const DwellingParams& params) { params_ = params; }

    // Generate the floor plan
    void generate();

    // Assign room types
    void assignRooms();

    // Place doors between rooms
    void assignDoors();

    // Place windows on exterior walls
    void spawnWindows();

    // Place stairs (for multi-floor buildings)
    void spawnStairs(bool hasFloorAbove, bool hasFloorBelow);

    // Set stair position (for alignment between floors)
    void setStairPosition(const Cell& cell, Dir direction, StairType type, bool goingUp);

private:
    // Recursive area division
    void divideArea(const std::vector<Edge>& contour);
    Room* addRoom(const std::vector<Edge>& contour);
    void connectRooms();
    void mergeCorridors();

    // Helper functions
    bool isNarrow(const std::vector<Cell*>& area, const Cell& c) const;
    Edge* getNotch(const std::vector<Edge>& contour);
    std::vector<Edge> findWall(const std::vector<Edge>& contour, const Edge& start, Dir direction);

    Grid* grid_;
    std::vector<Cell*> area_;
    std::vector<Edge> contour_;
    std::vector<Room*> rooms_;
    std::vector<std::unique_ptr<Room>> ownedRooms_;
    std::vector<Door> doors_;
    std::vector<Window> windows_;
    std::vector<Stair> stairs_;
    Door* entrance_ = nullptr;
    std::vector<std::vector<Edge>> innerWalls_;

    DwellingParams params_;
    std::mt19937 rng_;
};

} // namespace dwelling
