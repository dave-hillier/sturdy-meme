#include "DwellingPlan.h"
#include <algorithm>
#include <queue>
#include <set>
#include <map>

namespace dwelling {

std::string roomTypeName(RoomType type) {
    switch (type) {
        case RoomType::Hall: return "Hall";
        case RoomType::Kitchen: return "Kitchen";
        case RoomType::DiningRoom: return "Dining Room";
        case RoomType::LivingRoom: return "Living Room";
        case RoomType::Bedroom: return "Bedroom";
        case RoomType::Bathroom: return "Bathroom";
        case RoomType::Study: return "Study";
        case RoomType::Storage: return "Storage";
        case RoomType::Attic: return "Attic";
        case RoomType::Cellar: return "Cellar";
        default: return "Room";
    }
}

Room::Room(Plan* plan, const std::vector<Edge>& contour)
    : plan_(plan), contour_(contour)
{
    if (!contour.empty()) {
        area_ = plan_->grid()->contourToArea(contour);
    }
}

std::vector<Door*> Room::getDoors() const {
    std::vector<Door*> result;
    for (auto& door : const_cast<std::vector<Door>&>(plan_->doors())) {
        if (door.room1 == this || door.room2 == this) {
            result.push_back(&door);
        }
    }
    return result;
}

std::vector<Window*> Room::getWindows() const {
    std::vector<Window*> result;
    for (auto& window : const_cast<std::vector<Window>&>(plan_->windows())) {
        if (window.room == this) {
            result.push_back(&window);
        }
    }
    return result;
}

bool Room::contains(const Cell& c) const {
    for (const Cell* cell : area_) {
        if (*cell == c) return true;
    }
    return false;
}

bool Room::hasEdge(const Edge& e) const {
    for (const Edge& ce : contour_) {
        if ((ce.a == e.a && ce.b == e.b) || (ce.a == e.b && ce.b == e.a)) {
            return true;
        }
    }
    return false;
}

Plan::Plan(Grid* grid, const std::vector<Cell*>& area, uint32_t seed)
    : grid_(grid), area_(area), rng_(seed)
{
    contour_ = grid_->outline(area);
}

Room* Plan::getRoom(const Cell& c) {
    for (Room* room : rooms_) {
        if (room->contains(c)) {
            return room;
        }
    }
    return nullptr;
}

Room* Plan::getRoom(const Edge& e) {
    Cell c = e.adjacentCell();
    return getRoom(c);
}

void Plan::generate() {
    innerWalls_.clear();
    rooms_.clear();
    ownedRooms_.clear();
    doors_.clear();
    windows_.clear();
    entrance_ = nullptr;

    // Divide the area into rooms
    divideArea(contour_);

    // Merge narrow corridor-like rooms
    mergeCorridors();

    // Connect rooms with doors
    connectRooms();

    // Place entrance
    if (!rooms_.empty() && !contour_.empty()) {
        // Find a good exterior edge for entrance
        std::uniform_int_distribution<size_t> dist(0, contour_.size() - 1);
        size_t idx = dist(rng_);

        Room* entranceRoom = getRoom(contour_[idx]);
        if (entranceRoom) {
            Door entrance;
            entrance.room1 = nullptr;
            entrance.room2 = entranceRoom;
            entrance.edge = contour_[idx];
            entrance.type = DoorType::Regular;
            doors_.push_back(entrance);
            entrance_ = &doors_.back();
        }
    }
}

bool Plan::isNarrow(const std::vector<Cell*>& area, const Cell& c) const {
    bool hasNorth = false, hasSouth = false, hasEast = false, hasWest = false;

    for (const Cell* cell : area) {
        if (cell->i == c.i - 1 && cell->j == c.j) hasNorth = true;
        if (cell->i == c.i + 1 && cell->j == c.j) hasSouth = true;
        if (cell->i == c.i && cell->j == c.j + 1) hasEast = true;
        if (cell->i == c.i && cell->j == c.j - 1) hasWest = true;
    }

    // Narrow if missing neighbors on opposite sides
    if (!hasNorth && !hasSouth) return true;
    if (!hasEast && !hasWest) return true;

    return false;
}

Edge* Plan::getNotch(const std::vector<Edge>& contour) {
    std::vector<Cell*> area = grid_->contourToArea(contour);
    std::vector<Edge*> candidates;

    for (size_t i = 0; i < contour.size(); ++i) {
        const Edge& current = contour[i];
        const Edge& prev = contour[(i + contour.size() - 1) % contour.size()];

        if (current.dir == prev.dir) {
            // Straight edge - good for wall placement
            Cell* c1 = grid_->edgeToCell(prev);
            Cell* c2 = grid_->edgeToCell(current);
            bool narrow1 = c1 && isNarrow(area, *c1);
            bool narrow2 = c2 && isNarrow(area, *c2);
            if (!narrow1 || !narrow2) {
                candidates.push_back(const_cast<Edge*>(&contour[i]));
            }
        } else if (current.dir == counterClockwise(prev.dir)) {
            // Convex corner - good for notch
            candidates.push_back(const_cast<Edge*>(&contour[i]));
        }
    }

    if (candidates.empty()) return nullptr;

    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng_)];
}

std::vector<Edge> Plan::findWall(const std::vector<Edge>& contour, const Edge& start, Dir direction) {
    std::vector<Edge> wall;
    wall.push_back(start);

    // Extend wall in the given direction
    Node current = start.b;
    while (true) {
        // Find next edge
        bool found = false;
        for (const Edge& e : contour) {
            if (e.a == current && e.dir == direction) {
                wall.push_back(e);
                current = e.b;
                found = true;
                break;
            }
        }
        if (!found) break;
    }

    return wall;
}

void Plan::divideArea(const std::vector<Edge>& contour) {
    if (contour.empty()) return;

    std::vector<Cell*> area = grid_->contourToArea(contour);

    if (area.empty()) return;

    // Check if area is small enough for a room
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float sizeVariation = (dist(rng_) + dist(rng_) + dist(rng_)) / 3.0f;
    float threshold = params_.avgRoomSize * (0.5f + sizeVariation);

    if (static_cast<float>(area.size()) <= threshold || area.size() <= 3) {
        addRoom(contour);
        return;
    }

    // Simple division: split roughly in half
    // Find bounds
    int minI = area[0]->i, maxI = area[0]->i;
    int minJ = area[0]->j, maxJ = area[0]->j;
    for (const Cell* c : area) {
        minI = std::min(minI, c->i);
        maxI = std::max(maxI, c->i);
        minJ = std::min(minJ, c->j);
        maxJ = std::max(maxJ, c->j);
    }

    int rangeI = maxI - minI + 1;
    int rangeJ = maxJ - minJ + 1;

    std::vector<Cell*> side1, side2;

    // Split along the longer dimension
    if (rangeI >= rangeJ) {
        // Split horizontally
        int midI = minI + rangeI / 2;
        for (Cell* c : area) {
            if (c->i < midI) {
                side1.push_back(c);
            } else {
                side2.push_back(c);
            }
        }
    } else {
        // Split vertically
        int midJ = minJ + rangeJ / 2;
        for (Cell* c : area) {
            if (c->j < midJ) {
                side1.push_back(c);
            } else {
                side2.push_back(c);
            }
        }
    }

    // Handle edge cases
    if (side1.empty() || side2.empty()) {
        addRoom(contour);
        return;
    }

    // Recursively divide each side if connected
    if (grid_->isConnected(side1)) {
        std::vector<Edge> contour1 = grid_->outline(side1);
        if (!contour1.empty()) {
            divideArea(contour1);
        }
    } else {
        // Add disconnected parts as separate rooms
        addRoom(grid_->outline(side1));
    }

    if (grid_->isConnected(side2)) {
        std::vector<Edge> contour2 = grid_->outline(side2);
        if (!contour2.empty()) {
            divideArea(contour2);
        }
    } else {
        addRoom(grid_->outline(side2));
    }
}

Room* Plan::addRoom(const std::vector<Edge>& contour) {
    if (contour.empty()) return nullptr;

    auto room = std::make_unique<Room>(this, contour);
    if (room->area().empty()) return nullptr;  // Invalid room

    Room* ptr = room.get();
    rooms_.push_back(ptr);
    ownedRooms_.push_back(std::move(room));
    return ptr;
}

void Plan::mergeCorridors() {
    // Find corridor-like rooms (all cells are narrow)
    std::vector<Room*> corridors;
    for (Room* room : rooms_) {
        bool allNarrow = true;
        for (const Cell* c : room->area()) {
            if (!isNarrow(room->area(), *c)) {
                allNarrow = false;
                break;
            }
        }
        if (allNarrow) {
            corridors.push_back(room);
        }
    }

    // Try to merge corridors with adjacent rooms
    // (Simplified - just keep them as separate rooms for now)
}

void Plan::connectRooms() {
    if (rooms_.size() <= 1) return;

    // Find shared edges between rooms and place doors
    std::set<std::pair<Room*, Room*>> connected;

    for (size_t i = 0; i < rooms_.size(); ++i) {
        for (size_t j = i + 1; j < rooms_.size(); ++j) {
            Room* r1 = rooms_[i];
            Room* r2 = rooms_[j];

            // Find shared edges
            std::vector<Edge> sharedEdges;
            for (const Edge& e1 : r1->contour()) {
                for (const Edge& e2 : r2->contour()) {
                    if ((e1.a == e2.b && e1.b == e2.a)) {
                        sharedEdges.push_back(e1);
                    }
                }
            }

            if (!sharedEdges.empty() && connected.find({r1, r2}) == connected.end()) {
                // Place a door at a random shared edge
                std::uniform_int_distribution<size_t> dist(0, sharedEdges.size() - 1);
                Edge doorEdge = sharedEdges[dist(rng_)];

                Door door;
                door.room1 = r1;
                door.room2 = r2;
                door.edge = doorEdge;
                door.type = DoorType::Regular;
                doors_.push_back(door);

                connected.insert({r1, r2});
                connected.insert({r2, r1});
            }
        }
    }
}

void Plan::assignRooms() {
    if (rooms_.empty()) return;

    // Sort rooms by size
    std::vector<Room*> sortedRooms = rooms_;
    std::sort(sortedRooms.begin(), sortedRooms.end(),
        [](Room* a, Room* b) { return a->size() > b->size(); });

    // Find entrance room
    Room* entranceRoom = nullptr;
    if (entrance_) {
        entranceRoom = getRoom(entrance_->edge);
    }

    // Assign types based on size and position
    std::vector<RoomType> availableTypes = {
        RoomType::LivingRoom,
        RoomType::Kitchen,
        RoomType::Bedroom,
        RoomType::Bathroom,
        RoomType::Study,
        RoomType::Storage
    };

    int typeIndex = 0;
    for (Room* room : sortedRooms) {
        if (room == entranceRoom) {
            room->setType(RoomType::Hall);
        } else if (typeIndex < static_cast<int>(availableTypes.size())) {
            room->setType(availableTypes[typeIndex++]);
        } else {
            room->setType(RoomType::Bedroom);
        }
    }
}

void Plan::assignDoors() {
    // Doors are already assigned in connectRooms()
    // This could be enhanced to set door types based on room types
    for (Door& door : doors_) {
        if (door.isExterior()) {
            door.type = DoorType::Regular;
        } else {
            // Bathrooms get regular doors, other rooms may have doorways
            if ((door.room1 && door.room1->type() == RoomType::Bathroom) ||
                (door.room2 && door.room2->type() == RoomType::Bathroom)) {
                door.type = DoorType::Regular;
            } else {
                std::uniform_real_distribution<float> dist(0.0f, 1.0f);
                door.type = dist(rng_) < 0.7f ? DoorType::Regular : DoorType::Doorway;
            }
        }
    }
}

void Plan::spawnWindows() {
    windows_.clear();

    for (Room* room : rooms_) {
        // Find exterior edges
        std::vector<Edge> exteriorEdges;
        for (const Edge& e : room->contour()) {
            bool isExterior = false;
            for (const Edge& ce : contour_) {
                if (e.a == ce.a && e.b == ce.b) {
                    isExterior = true;
                    break;
                }
            }

            // Check it's not a door
            bool isDoor = false;
            for (const Door& d : doors_) {
                if ((d.edge.a == e.a && d.edge.b == e.b) ||
                    (d.edge.a == e.b && d.edge.b == e.a)) {
                    isDoor = true;
                    break;
                }
            }

            if (isExterior && !isDoor) {
                exteriorEdges.push_back(e);
            }
        }

        // Place windows on some exterior edges
        int numWindows = static_cast<int>(exteriorEdges.size() * params_.windowDensity);
        std::shuffle(exteriorEdges.begin(), exteriorEdges.end(), rng_);

        for (int i = 0; i < numWindows && i < static_cast<int>(exteriorEdges.size()); ++i) {
            Window window;
            window.room = room;
            window.edge = exteriorEdges[i];
            windows_.push_back(window);
        }
    }
}

} // namespace dwelling
