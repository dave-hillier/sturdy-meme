// Dwelling generator implementation
// Ported from watabou's Dwellings (https://watabou.itch.io/dwellings)

#include "DwellingGenerator.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <numeric>
#include <nlohmann/json.hpp>

namespace dwelling {

// Direction helpers
Dir clockwise(Dir d) {
    switch (d) {
        case Dir::North: return Dir::East;
        case Dir::East: return Dir::South;
        case Dir::South: return Dir::West;
        case Dir::West: return Dir::North;
    }
    return Dir::North;
}

Dir counterClockwise(Dir d) {
    switch (d) {
        case Dir::North: return Dir::West;
        case Dir::West: return Dir::South;
        case Dir::South: return Dir::East;
        case Dir::East: return Dir::North;
    }
    return Dir::North;
}

Dir opposite(Dir d) {
    switch (d) {
        case Dir::North: return Dir::South;
        case Dir::South: return Dir::North;
        case Dir::East: return Dir::West;
        case Dir::West: return Dir::East;
    }
    return Dir::South;
}

int deltaI(Dir d) {
    switch (d) {
        case Dir::North: return -1;
        case Dir::South: return 1;
        case Dir::East: return 0;
        case Dir::West: return 0;
    }
    return 0;
}

int deltaJ(Dir d) {
    switch (d) {
        case Dir::North: return 0;
        case Dir::South: return 0;
        case Dir::East: return 1;
        case Dir::West: return -1;
    }
    return 0;
}

static const Dir CARDINAL[] = {Dir::North, Dir::East, Dir::South, Dir::West};

std::string roomTypeName(RoomType type) {
    switch (type) {
        case RoomType::Generic: return "room";
        case RoomType::Corridor: return "corridor";
        case RoomType::Hall: return "hall";
        case RoomType::Kitchen: return "kitchen";
        case RoomType::DiningRoom: return "dining room";
        case RoomType::LivingRoom: return "living room";
        case RoomType::Bedroom: return "bedroom";
        case RoomType::GuestRoom: return "guest room";
        case RoomType::Bathroom: return "bathroom";
        case RoomType::Study: return "study";
        case RoomType::Library: return "library";
        case RoomType::Office: return "office";
        case RoomType::Storage: return "storage";
        case RoomType::Cellar: return "cellar";
        case RoomType::Attic: return "attic";
        case RoomType::Stairwell: return "stairwell";
        case RoomType::SecretPassage: return "secret passage";
        case RoomType::Armory: return "armory";
        case RoomType::Greenhouse: return "greenhouse";
        case RoomType::Observatory: return "observatory";
        case RoomType::Laboratory: return "laboratory";
        case RoomType::Gallery: return "gallery";
        case RoomType::Chapel: return "chapel";
        case RoomType::Servant: return "servant quarters";
        case RoomType::Nursery: return "nursery";
        case RoomType::Pantry: return "pantry";
        case RoomType::Lookout: return "lookout";
    }
    return "room";
}

// Helper to check if pointer is in vector (reference/identity comparison)
template<typename T>
bool contains(const std::vector<T*>& vec, T* item) {
    return std::find(vec.begin(), vec.end(), item) != vec.end();
}

template<typename T>
void removeItem(std::vector<T*>& vec, T* item) {
    vec.erase(std::remove(vec.begin(), vec.end(), item), vec.end());
}

template<typename T>
int indexOf(const std::vector<T*>& vec, T* item) {
    auto it = std::find(vec.begin(), vec.end(), item);
    if (it == vec.end()) return -1;
    return static_cast<int>(std::distance(vec.begin(), it));
}

// Grid implementation
Grid::Grid(int width, int height) : w(width), h(height) {
    // Create nodes (one more than cells in each dimension)
    nodes.resize(h + 1);
    int nodeId = 0;
    for (int i = 0; i <= h; ++i) {
        nodes[i].resize(w + 1);
        for (int j = 0; j <= w; ++j) {
            auto n = std::make_unique<Node>();
            n->i = i;
            n->j = j;
            n->id = nodeId++;
            nodes[i][j] = std::move(n);
        }
    }

    // Create cells
    cells.resize(h);
    for (int i = 0; i < h; ++i) {
        cells[i].resize(w);
        for (int j = 0; j < w; ++j) {
            auto c = std::make_unique<Cell>();
            c->i = i;
            c->j = j;
            cells[i][j] = std::move(c);
        }
    }

    createEdges();
}

void Grid::createEdges() {
    // Create edges between adjacent nodes
    for (int i = 0; i <= h; ++i) {
        for (int j = 0; j <= w; ++j) {
            Node* n = nodes[i][j].get();

            // Horizontal edge (to the right)
            if (j < w) {
                Node* right = nodes[i][j + 1].get();
                auto key = edgeKey(n, right);
                auto e = std::make_unique<Edge>();
                e->a = n;
                e->b = right;
                e->dir = Dir::East;
                edges[key] = std::move(e);

                // Reverse edge
                auto keyRev = edgeKey(right, n);
                auto eRev = std::make_unique<Edge>();
                eRev->a = right;
                eRev->b = n;
                eRev->dir = Dir::West;
                edges[keyRev] = std::move(eRev);
            }

            // Vertical edge (downward)
            if (i < h) {
                Node* down = nodes[i + 1][j].get();
                auto key = edgeKey(n, down);
                auto e = std::make_unique<Edge>();
                e->a = n;
                e->b = down;
                e->dir = Dir::South;
                edges[key] = std::move(e);

                // Reverse edge
                auto keyRev = edgeKey(down, n);
                auto eRev = std::make_unique<Edge>();
                eRev->a = down;
                eRev->b = n;
                eRev->dir = Dir::North;
                edges[keyRev] = std::move(eRev);
            }
        }
    }
}

std::pair<int, int> Grid::edgeKey(Node* a, Node* b) {
    return {a->id, b->id};
}

Node* Grid::node(int i, int j) {
    if (i < 0 || i > h || j < 0 || j > w) return nullptr;
    return nodes[i][j].get();
}

Cell* Grid::cell(int i, int j) {
    if (i < 0 || i >= h || j < 0 || j >= w) return nullptr;
    return cells[i][j].get();
}

Edge* Grid::nodeToEdge(Node* n, Dir dir) {
    if (!n) return nullptr;
    Node* other = node(n->i + deltaI(dir), n->j + deltaJ(dir));
    if (!other) return nullptr;
    return edgeBetween(n, other);
}

Edge* Grid::cellToEdge(Cell* c, Dir dir) {
    if (!c) return nullptr;
    int i = c->i;
    int j = c->j;

    switch (dir) {
        case Dir::North:
            return edgeBetween(nodes[i][j].get(), nodes[i][j + 1].get());
        case Dir::East:
            return edgeBetween(nodes[i][j + 1].get(), nodes[i + 1][j + 1].get());
        case Dir::South:
            return edgeBetween(nodes[i + 1][j + 1].get(), nodes[i + 1][j].get());
        case Dir::West:
            return edgeBetween(nodes[i + 1][j].get(), nodes[i][j].get());
    }
    return nullptr;
}

Edge* Grid::edgeBetween(Node* a, Node* b) {
    if (!a || !b) return nullptr;
    auto key = edgeKey(a, b);
    auto it = edges.find(key);
    if (it == edges.end()) return nullptr;
    return it->second.get();
}

Cell* Grid::edgeToCell(Edge* e) {
    if (!e) return nullptr;
    Node* a = e->a;

    switch (e->dir) {
        case Dir::East:
            return cell(a->i, a->j);
        case Dir::South:
            return cell(a->i, a->j - 1);
        case Dir::West:
            return cell(a->i - 1, a->j - 1);
        case Dir::North:
            return cell(a->i - 1, a->j);
    }
    return nullptr;
}

std::vector<Edge*> Grid::outline(const std::vector<Cell*>& area) {
    std::vector<Edge*> boundary;

    for (Cell* c : area) {
        Node* tl = nodes[c->i][c->j].get();
        Node* tr = nodes[c->i][c->j + 1].get();
        Node* br = nodes[c->i + 1][c->j + 1].get();
        Node* bl = nodes[c->i + 1][c->j].get();

        // North edge
        if (!contains(area, cell(c->i - 1, c->j))) {
            boundary.push_back(edgeBetween(tl, tr));
        }
        // East edge
        if (!contains(area, cell(c->i, c->j + 1))) {
            boundary.push_back(edgeBetween(tr, br));
        }
        // South edge
        if (!contains(area, cell(c->i + 1, c->j))) {
            boundary.push_back(edgeBetween(br, bl));
        }
        // West edge
        if (!contains(area, cell(c->i, c->j - 1))) {
            boundary.push_back(edgeBetween(bl, tl));
        }
    }

    // Sort edges into a continuous contour
    if (boundary.empty()) return {};

    std::vector<Edge*> result;
    result.push_back(boundary[0]);
    boundary.erase(boundary.begin());

    while (!boundary.empty()) {
        Edge* last = result.back();
        bool found = false;

        // Try to continue in same direction first, then clockwise, then counter-clockwise
        for (Dir tryDir : {last->dir, clockwise(last->dir), counterClockwise(last->dir)}) {
            Edge* next = nodeToEdge(last->b, tryDir);
            auto it = std::find(boundary.begin(), boundary.end(), next);
            if (it != boundary.end()) {
                result.push_back(next);
                boundary.erase(it);
                found = true;
                break;
            }
        }

        if (!found) break;
    }

    return result;
}

std::vector<Cell*> Grid::contourToArea(const std::vector<Edge*>& contour) {
    if (contour.empty()) return {};

    Cell* start = edgeToCell(contour[0]);
    if (!start) return {};

    std::vector<Cell*> area;
    std::vector<Cell*> queue;
    area.push_back(start);
    queue.push_back(start);

    while (!queue.empty()) {
        Cell* c = queue.back();
        queue.pop_back();

        for (Dir dir : CARDINAL) {
            Cell* neighbor = cell(c->i + deltaI(dir), c->j + deltaJ(dir));
            if (!neighbor) continue;
            if (contains(area, neighbor)) continue;

            Edge* e = cellToEdge(c, dir);
            if (contains(contour, e)) continue;

            area.push_back(neighbor);
            queue.push_back(neighbor);
        }
    }

    return area;
}

bool Grid::isConnected(const std::vector<Cell*>& area) {
    if (area.empty()) return true;

    std::vector<Cell*> remaining = area;
    std::vector<Cell*> connected;
    connected.push_back(remaining.back());
    remaining.pop_back();

    while (!remaining.empty()) {
        bool found = false;
        for (size_t i = 0; i < remaining.size(); ++i) {
            Cell* c = remaining[i];
            for (Dir dir : CARDINAL) {
                Cell* neighbor = cell(c->i + deltaI(dir), c->j + deltaJ(dir));
                if (contains(connected, neighbor)) {
                    connected.push_back(c);
                    remaining.erase(remaining.begin() + i);
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) return false;
    }
    return true;
}

std::vector<Edge*> Grid::revertChain(const std::vector<Edge*>& chain) {
    std::vector<Edge*> result;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        Edge* e = *it;
        Edge* rev = edgeBetween(e->b, e->a);
        result.push_back(rev);
    }
    return result;
}

// Door implementation
int Door::getPrice() const {
    int price = 0;
    if (room1) price += room1->size();
    if (room2) price += room2->size();
    return price;
}

// Room implementation
int Room::countDoors() const {
    return static_cast<int>(doors.size());
}

std::vector<Door*> Room::getDoors() {
    std::vector<Door*> result;
    for (auto& [_, door] : doors) {
        result.push_back(door);
    }
    return result;
}

bool Room::hasExit() const {
    if (!floor || !floor->entrance.has_value()) return false;
    return contains(contour, floor->entrance->door);
}

bool Room::hasSpiral() const {
    if (!floor || !floor->spiral.has_value()) return false;
    return contains(area, floor->spiral->landing);
}

void Room::link(Room* other, Edge* edge, Door* door) {
    door->room1 = this;
    door->room2 = other;
    door->edge1 = edge;
    door->edge2 = floor->grid->edgeBetween(edge->b, edge->a);
    doors[other] = door;
    other->doors[this] = door;
}

void Room::unlink(Room* other) {
    doors.erase(other);
    other->doors.erase(this);
}

// Floor implementation
int Floor::getFloorIndex() const {
    if (!dwelling) return 0;
    for (size_t i = 0; i < dwelling->floors.size(); ++i) {
        if (dwelling->floors[i].get() == this) {
            return static_cast<int>(i);
        }
    }
    if (dwelling->basement.get() == this) return -1;
    return 0;
}

bool Floor::isGroundFloor() const {
    return getFloorIndex() == 0;
}

bool Floor::isTopFloor() const {
    if (!dwelling) return true;
    return getFloorIndex() == static_cast<int>(dwelling->floors.size()) - 1;
}

Room* Floor::getRoom(Cell* cell) {
    if (!cell) return nullptr;
    for (auto& room : rooms) {
        if (contains(room->area, cell)) {
            return room.get();
        }
    }
    return nullptr;
}

Room* Floor::edgeToRoom(Edge* e) {
    return getRoom(grid->edgeToCell(e));
}

Room* Floor::addRoom(const std::vector<Edge*>& roomContour) {
    auto room = std::make_unique<Room>();
    room->floor = this;
    room->contour = roomContour;
    room->area = grid->contourToArea(roomContour);

    // Identify narrow cells
    for (Cell* c : room->area) {
        if (isNarrow(room->area, c)) {
            room->narrow.push_back(c);
        }
    }

    Room* ptr = room.get();
    rooms.push_back(std::move(room));
    return ptr;
}

std::vector<Door*> Floor::getDoors() {
    std::vector<Door*> result;
    for (auto& room : rooms) {
        for (auto& [_, door] : room->doors) {
            if (std::find(result.begin(), result.end(), door) == result.end()) {
                result.push_back(door);
            }
        }
    }
    return result;
}

Room* Floor::findStart() {
    int floorIdx = getFloorIndex();
    if (floorIdx == 0 && entrance.has_value()) {
        return getRoom(entrance->landing);
    }
    if (spiral.has_value()) {
        return getRoom(spiral->landing);
    }
    // Find room with stairs going down (for upper floors) or up (for basement)
    for (auto& stair : stairs) {
        if (floorIdx < 0) {
            // Basement - find stairs going up
            if (stair.to && stair.to->getFloorIndex() > floorIdx) {
                return getRoom(stair.cell);
            }
        } else {
            // Upper floor - find stairs going down
            if (stair.to && stair.to->getFloorIndex() < floorIdx) {
                return getRoom(stair.cell);
            }
        }
    }
    return rooms.empty() ? nullptr : rooms[0].get();
}

bool Floor::isNarrow(const std::vector<Cell*>& areaVec, Cell* c) {
    bool north = contains(areaVec, grid->cell(c->i + deltaI(Dir::North), c->j + deltaJ(Dir::North)));
    bool south = contains(areaVec, grid->cell(c->i + deltaI(Dir::South), c->j + deltaJ(Dir::South)));
    if (!north && !south) return true;

    bool east = contains(areaVec, grid->cell(c->i + deltaI(Dir::East), c->j + deltaJ(Dir::East)));
    bool west = contains(areaVec, grid->cell(c->i + deltaI(Dir::West), c->j + deltaJ(Dir::West)));
    if (!east && !west) return true;

    // Check diagonal neighbors
    auto checkDiag = [&](int di, int dj, bool adj1, bool adj2) {
        Cell* diag = grid->cell(c->i + di, c->j + dj);
        if (contains(areaVec, diag) && adj1 && adj2) return false;
        return true;
    };

    if (!checkDiag(-1, 1, north, east)) return false;  // NE
    if (!checkDiag(-1, -1, north, west)) return false; // NW
    if (!checkDiag(1, 1, south, east)) return false;   // SE
    if (!checkDiag(1, -1, south, west)) return false;  // SW

    return true;
}

// Blueprint implementation
bool Blueprint::hasTag(const std::string& tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

// DwellingGenerator implementation
DwellingGenerator::DwellingGenerator() : rng(std::random_device{}()) {}

float DwellingGenerator::random() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng);
}

int DwellingGenerator::randomInt(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

template<typename T>
T& DwellingGenerator::randomChoice(std::vector<T>& vec) {
    return vec[randomInt(0, static_cast<int>(vec.size()) - 1)];
}

template<typename T>
const T& DwellingGenerator::randomChoice(const std::vector<T>& vec) {
    return vec[randomInt(0, static_cast<int>(vec.size()) - 1)];
}

template<typename T>
T& DwellingGenerator::weightedChoice(std::vector<T>& items, const std::vector<float>& weights) {
    float total = std::accumulate(weights.begin(), weights.end(), 0.0f);
    float r = random() * total;
    float sum = 0.0f;
    for (size_t i = 0; i < items.size(); ++i) {
        sum += weights[i];
        if (r <= sum) return items[i];
    }
    return items.back();
}

bool DwellingGenerator::generate(const DwellingConfig& config,
                                  std::function<void(float, const std::string&)> progress) {
    dwellings.clear();

    if (config.seed != 0) {
        rng.seed(config.seed);
    }

    for (int i = 0; i < config.count; ++i) {
        if (progress) {
            progress(static_cast<float>(i) / config.count, "Generating dwelling " + std::to_string(i + 1));
        }

        Blueprint bp;
        bp.seed = config.seed != 0 ? config.seed + i : randomInt(1, 2147483647);
        bp.numFloors = config.numFloors;
        bp.size = config.size;
        bp.square = config.square;
        bp.hasBasement = config.basement;
        bp.tags = config.tags;

        if (config.spiral) bp.tags.push_back("spiral");
        if (config.stairwell) bp.tags.push_back("stairwell");

        auto dwelling = generateDwelling(bp);
        if (dwelling) {
            dwellings.push_back(std::move(dwelling));
        }
    }

    if (progress) {
        progress(1.0f, "Generation complete");
    }

    return !dwellings.empty();
}

DwellingGenerator::ShapeResult DwellingGenerator::getShape(int minSize, int maxSize, bool isSquare) {
    std::vector<Point> points;
    if (isSquare) {
        points = getBox(minSize, maxSize);
    } else {
        points = getPolyomino(minSize, maxSize);
    }

    if (points.empty()) {
        return {nullptr, {}};
    }

    // Find bounding box
    int minI = points[0].i, maxI = points[0].i;
    int minJ = points[0].j, maxJ = points[0].j;
    for (const auto& p : points) {
        minI = std::min(minI, p.i);
        maxI = std::max(maxI, p.i);
        minJ = std::min(minJ, p.j);
        maxJ = std::max(maxJ, p.j);
    }

    int gridW = maxJ - minJ + 1;
    int gridH = maxI - minI + 1;

    auto grid = std::make_unique<Grid>(gridW, gridH);
    std::vector<Cell*> area;

    for (const auto& p : points) {
        Cell* c = grid->cell(p.i - minI, p.j - minJ);
        if (c) area.push_back(c);
    }

    return {std::move(grid), area};
}

std::vector<Point> DwellingGenerator::getBox(int minSize, int maxSize) {
    while (true) {
        int w = randomInt(2, 8);
        int h = randomInt(2, 8);
        if (w * h >= minSize && w * h <= maxSize) {
            std::vector<Point> result;
            for (int i = 0; i < h; ++i) {
                for (int j = 0; j < w; ++j) {
                    result.push_back({i, j});
                }
            }
            return result;
        }
    }
}

std::vector<Point> DwellingGenerator::getPolyomino(int minSize, int maxSize) {
    // Tetromino and pentomino patterns (3x3 grid representation)
    static const std::vector<std::string> tetros = {
        " x xxx",    // T
        "xx  xx",    // L
        "xx xx ",    // S
        "xx  xx"     // Z
    };

    static const std::vector<std::string> pentos = {
        " xxxx  x ",  // F
        "xx xx x  ",  // I variant
        "xxx x  x ",  // L variant
        "x xxxx",     // N
        "x  x  xxx",  // P
        "x  xx  xx",  // T variant
        " x xxx x ",  // U
        "xx  x  xx"   // W
    };

    int targetMin = static_cast<int>(std::round(minSize / 10.0f));
    int targetMax = static_cast<int>(std::round(maxSize / 10.0f));

    while (true) {
        // Random transforms
        bool mirrorX = random() < 0.5f;
        bool mirrorY = random() < 0.5f;
        bool rotate = random() < 0.5f;

        // Random column/row sizes
        std::vector<int> cols(3), rows(3);
        for (int i = 0; i < 3; ++i) {
            cols[i] = randomInt(targetMin, targetMax);
            rows[i] = randomInt(targetMin, targetMax);
        }

        // Pick a pattern
        std::vector<std::string> allPatterns = tetros;
        allPatterns.insert(allPatterns.end(), pentos.begin(), pentos.end());
        const std::string& pattern = randomChoice(allPatterns);

        // Generate cells based on pattern
        int gridSize = 3 * targetMax + 2;
        std::vector<std::vector<bool>> bitmap(gridSize, std::vector<bool>(gridSize, false));

        auto setCell = [&](int px, int py) {
            if (mirrorX) px = 2 - px;
            if (mirrorY) py = 2 - py;
            if (rotate) std::swap(px, py);

            int startX = 1, startY = 1;
            for (int i = 0; i < px; ++i) startX += cols[i];
            for (int i = 0; i < py; ++i) startY += rows[i];

            auto randOffset = [this]() { return randomInt(0, 1) == 0 ? 0 : 1; };

            int x1 = startX - randOffset();
            int x2 = startX + cols[px] + randOffset();
            int y1 = startY - randOffset();
            int y2 = startY + rows[py] + randOffset();

            for (int y = y1; y < y2 && y < gridSize; ++y) {
                for (int x = x1; x < x2 && x < gridSize; ++x) {
                    if (x >= 0 && y >= 0) {
                        bitmap[y][x] = true;
                    }
                }
            }
        };

        for (size_t idx = 0; idx < pattern.size(); ++idx) {
            if (pattern[idx] != ' ') {
                int px = idx % 3;
                int py = static_cast<int>(idx / 3);
                setCell(px, py);
            }
        }

        // Convert bitmap to points
        std::vector<Point> result;
        for (int i = 0; i < gridSize; ++i) {
            for (int j = 0; j < gridSize; ++j) {
                if (bitmap[i][j]) {
                    result.push_back({i, j});
                }
            }
        }

        if (static_cast<int>(result.size()) >= minSize &&
            static_cast<int>(result.size()) <= maxSize) {
            return result;
        }
    }
}

std::unique_ptr<Dwelling> DwellingGenerator::generateDwelling(const Blueprint& bp) {
    rng.seed(bp.seed);

    auto dwelling = std::make_unique<Dwelling>();
    dwelling->seed = bp.seed;
    dwelling->name = "Dwelling " + std::to_string(bp.seed);

    // Determine size range
    int minSize = 10, maxSize = 16;
    if (bp.size == "small") {
        minSize = 10; maxSize = 16;
    } else if (bp.size == "medium") {
        minSize = 16; maxSize = 24;
    } else if (bp.size == "large") {
        minSize = 24; maxSize = 34;
    }

    // Generate shape
    auto [grid, area] = getShape(minSize, maxSize, bp.square);
    if (!grid || area.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to generate shape");
        return nullptr;
    }

    // Determine number of floors
    int numFloors = bp.numFloors;
    if (numFloors <= 0) {
        float baseFloors = std::sqrt(static_cast<float>(area.size())) - 1.0f;
        numFloors = std::max(1, std::min(8, static_cast<int>(std::round(
            baseFloors * (random() + random() + random()) / 3.0f
        ))));
    }

    // Create ground floor
    auto floor0 = std::make_unique<Floor>();
    floor0->dwelling = dwelling.get();
    floor0->grid = std::move(grid);
    floor0->area = area;
    floor0->contour = floor0->grid->outline(area);

    // Pick entrance
    if (!floor0->contour.empty()) {
        std::vector<float> weights;
        for (size_t i = 0; i < floor0->contour.size(); ++i) {
            Edge* e = floor0->contour[i];
            Edge* prev = floor0->contour[(i + floor0->contour.size() - 1) % floor0->contour.size()];
            Edge* next = floor0->contour[(i + 1) % floor0->contour.size()];

            // Prefer straight segments for entrance
            if (prev->dir == e->dir && e->dir == next->dir) {
                weights.push_back(5.0f);
            } else if (clockwise(prev->dir) == e->dir && clockwise(e->dir) == next->dir) {
                weights.push_back(3.0f);
            } else {
                weights.push_back(1.0f);
            }
        }

        Edge* entranceEdge = weightedChoice(floor0->contour, weights);
        floor0->entrance = Entrance{entranceEdge, floor0->grid->edgeToCell(entranceEdge)};
    }

    // Roll for stairs type
    bool hasSpiral = false;
    bool hasStairwell = false;
    if (numFloors > 1) {
        if (bp.hasTag("spiral")) {
            hasSpiral = true;
        } else if (bp.hasTag("stairwell")) {
            hasStairwell = true;
        } else {
            float spiralChance = numFloors == 2 ? 0.1f : 0.3f;
            hasSpiral = random() < spiralChance;
            if (!hasSpiral) {
                float stairwellChance = static_cast<float>(numFloors - 1) / (numFloors + 1);
                hasStairwell = random() < stairwellChance;
            }
        }
    }

    // Setup stairwell if needed
    if (hasStairwell && !floor0->area.empty()) {
        // Find a cell that can be removed while keeping area connected
        std::vector<Cell*> candidates;
        for (Cell* c : floor0->area) {
            if (floor0->entrance.has_value() && c == floor0->entrance->landing) continue;

            auto remaining = floor0->area;
            removeItem(remaining, c);
            if (floor0->grid->isConnected(remaining)) {
                candidates.push_back(c);
            }
        }

        if (!candidates.empty()) {
            // Weight by number of exterior walls (prefer corners)
            std::vector<float> weights;
            for (Cell* c : candidates) {
                int exteriorWalls = 0;
                for (Dir dir : CARDINAL) {
                    if (!contains(floor0->area, floor0->grid->cell(c->i + deltaI(dir), c->j + deltaJ(dir)))) {
                        exteriorWalls++;
                    }
                }
                weights.push_back(static_cast<float>(4 - exteriorWalls + 1));
            }

            Cell* stairCell = weightedChoice(candidates, weights);

            // Find exit direction
            std::vector<Dir> exitDirs;
            for (Dir dir : CARDINAL) {
                Cell* neighbor = floor0->grid->cell(stairCell->i + deltaI(dir), stairCell->j + deltaJ(dir));
                if (contains(floor0->area, neighbor)) {
                    exitDirs.push_back(dir);
                }
            }

            if (!exitDirs.empty()) {
                Dir exitDir = randomChoice(exitDirs);
                Cell* landing = floor0->grid->cell(stairCell->i + deltaI(exitDir), stairCell->j + deltaJ(exitDir));

                floor0->stairwell = Stairwell{stairCell, landing, exitDir, nullptr};
            }
        }
    }

    // Setup spiral stairs if needed
    if (hasSpiral && floor0->contour.size() >= 4) {
        std::vector<Edge*> spiralCandidates;
        for (size_t i = 0; i < floor0->contour.size(); ++i) {
            Edge* e = floor0->contour[i];
            Edge* prev1 = floor0->contour[(i + floor0->contour.size() - 1) % floor0->contour.size()];
            Edge* prev2 = floor0->contour[(i + floor0->contour.size() - 2) % floor0->contour.size()];
            Edge* next = floor0->contour[(i + 1) % floor0->contour.size()];

            if (clockwise(e->dir) == prev1->dir &&
                clockwise(e->dir) == prev2->dir &&
                e->dir == next->dir &&
                e != floor0->entrance->door &&
                prev1 != floor0->entrance->door) {
                spiralCandidates.push_back(e);
            }
        }

        if (!spiralCandidates.empty()) {
            Edge* spiralEntrance = randomChoice(spiralCandidates);
            size_t idx = indexOf(floor0->contour, spiralEntrance);
            Edge* spiralExit = floor0->contour[(idx + floor0->contour.size() - 1) % floor0->contour.size()];

            floor0->spiral = Spiral{spiralEntrance, spiralExit, floor0->grid->edgeToCell(spiralEntrance)};
        }
    }

    // Divide the floor into rooms
    if (floor0->stairwell.has_value()) {
        // Create stairwell room first
        auto stairwellArea = std::vector<Cell*>{floor0->stairwell->stair};
        auto stairwellContour = floor0->grid->outline(stairwellArea);
        Room* stairRoom = floor0->addRoom(stairwellContour);
        floor0->stairwell->room = stairRoom;

        // Divide remaining area
        auto remainingArea = floor0->area;
        removeItem(remainingArea, floor0->stairwell->stair);
        auto remainingContour = floor0->grid->outline(remainingArea);
        floor0->divideArea(remainingContour);
    } else {
        floor0->divideArea(floor0->contour);
    }

    floor0->mergeCorridors();
    floor0->connectRooms();
    floor0->spawnWindows();

    dwelling->floors.push_back(std::move(floor0));

    // Generate upper floors
    for (int f = 1; f < numFloors; ++f) {
        Floor* prevFloor = dwelling->floors.back().get();

        auto upperFloor = std::make_unique<Floor>();
        upperFloor->dwelling = dwelling.get();

        // Upper floor may be smaller - find a subset of rooms from floor below
        if (prevFloor->rooms.size() > 1 && random() < 0.3f) {
            // Pick a random subset of area
            std::vector<Cell*> upperArea;
            Room* startRoom = nullptr;
            float bestCompact = -1.0f;

            for (auto& room : prevFloor->rooms) {
                if (prevFloor->stairwell.has_value() && room.get() == prevFloor->stairwell->room) {
                    continue;
                }

                // Calculate compactness
                float compact = static_cast<float>(room->area.size()) /
                               static_cast<float>(room->contour.size());
                if (compact > bestCompact) {
                    bestCompact = compact;
                    startRoom = room.get();
                }
            }

            if (startRoom) {
                upperArea = startRoom->area;
            }

            if (upperArea.empty()) {
                upperArea = prevFloor->area;
            }

            // Create new grid for upper floor
            int minI = upperArea[0]->i, maxI = upperArea[0]->i;
            int minJ = upperArea[0]->j, maxJ = upperArea[0]->j;
            for (Cell* c : upperArea) {
                minI = std::min(minI, c->i);
                maxI = std::max(maxI, c->i);
                minJ = std::min(minJ, c->j);
                maxJ = std::max(maxJ, c->j);
            }

            upperFloor->grid = std::make_unique<Grid>(maxJ - minJ + 1, maxI - minI + 1);
            for (Cell* c : upperArea) {
                Cell* newCell = upperFloor->grid->cell(c->i - minI, c->j - minJ);
                if (newCell) upperFloor->area.push_back(newCell);
            }
        } else {
            // Same shape as floor below
            upperFloor->grid = std::make_unique<Grid>(prevFloor->grid->width(), prevFloor->grid->height());
            for (Cell* c : prevFloor->area) {
                Cell* newCell = upperFloor->grid->cell(c->i, c->j);
                if (newCell) upperFloor->area.push_back(newCell);
            }
        }

        upperFloor->contour = upperFloor->grid->outline(upperFloor->area);

        // Copy spiral if present
        if (prevFloor->spiral.has_value() && hasSpiral) {
            // Find corresponding edge
            for (Edge* e : upperFloor->contour) {
                Cell* prevCell = prevFloor->spiral->landing;
                Cell* currCell = upperFloor->grid->edgeToCell(e);
                if (currCell && prevCell &&
                    currCell->i == prevCell->i && currCell->j == prevCell->j &&
                    e->dir == prevFloor->spiral->entrance->dir) {
                    size_t idx = indexOf(upperFloor->contour, e);
                    Edge* exitEdge = upperFloor->contour[(idx + upperFloor->contour.size() - 1) % upperFloor->contour.size()];
                    upperFloor->spiral = Spiral{e, exitEdge, currCell};
                    break;
                }
            }
        }

        // Copy stairwell if present
        if (prevFloor->stairwell.has_value() && hasStairwell) {
            Cell* prevStair = prevFloor->stairwell->stair;
            Cell* newStair = upperFloor->grid->cell(prevStair->i, prevStair->j);
            Cell* newLanding = upperFloor->grid->cell(
                prevFloor->stairwell->landing->i,
                prevFloor->stairwell->landing->j
            );

            if (newStair && contains(upperFloor->area, newStair)) {
                upperFloor->stairwell = Stairwell{newStair, newLanding, prevFloor->stairwell->exit, nullptr};

                auto stairwellArea = std::vector<Cell*>{newStair};
                auto stairwellContour = upperFloor->grid->outline(stairwellArea);
                Room* stairRoom = upperFloor->addRoom(stairwellContour);
                upperFloor->stairwell->room = stairRoom;

                auto remainingArea = upperFloor->area;
                removeItem(remainingArea, newStair);
                auto remainingContour = upperFloor->grid->outline(remainingArea);
                upperFloor->divideArea(remainingContour);
            } else {
                upperFloor->divideArea(upperFloor->contour);
            }
        } else {
            upperFloor->divideArea(upperFloor->contour);
        }

        upperFloor->mergeCorridors();
        upperFloor->connectRooms();
        upperFloor->spawnWindows();

        dwelling->floors.push_back(std::move(upperFloor));
    }

    // Generate basement if requested
    if (bp.hasBasement || (random() < static_cast<float>(numFloors) / (numFloors + 1) && !bp.hasTag("basement"))) {
        Floor* groundFloor = dwelling->floors[0].get();

        auto basement = std::make_unique<Floor>();
        basement->dwelling = dwelling.get();
        basement->grid = std::make_unique<Grid>(groundFloor->grid->width(), groundFloor->grid->height());

        for (Cell* c : groundFloor->area) {
            Cell* newCell = basement->grid->cell(c->i, c->j);
            if (newCell) basement->area.push_back(newCell);
        }

        basement->contour = basement->grid->outline(basement->area);
        basement->divideArea(basement->contour);
        basement->mergeCorridors();
        basement->connectRooms();

        dwelling->basement = std::move(basement);
    }

    // Assign room types
    for (auto& floor : dwelling->floors) {
        floor->assignRooms();
    }
    if (dwelling->basement) {
        dwelling->basement->assignRooms();
    }

    // Connect floors with stairs
    if (!hasSpiral) {
        for (size_t i = 1; i < dwelling->floors.size(); ++i) {
            Floor* lower = dwelling->floors[i - 1].get();
            Floor* upper = dwelling->floors[i].get();

            // Find stairs locations
            if (lower->stairwell.has_value() && upper->stairwell.has_value()) {
                Staircase stairUp;
                stairUp.cell = lower->stairwell->landing;
                stairUp.dir = opposite(lower->stairwell->exit);
                stairUp.from = lower;
                stairUp.to = upper;
                lower->stairs.push_back(stairUp);

                Staircase stairDown;
                stairDown.cell = upper->stairwell->landing;
                stairDown.dir = upper->stairwell->exit;
                stairDown.from = upper;
                stairDown.to = lower;
                upper->stairs.push_back(stairDown);
            }
        }
    }

    return dwelling;
}

void Floor::divideArea(const std::vector<Edge*>& areaContour) {
    auto area = grid->contourToArea(areaContour);

    // Use a unique seed combining area size, position of first cell, and contour length
    unsigned int seed = static_cast<unsigned>(area.size() * 48271);
    if (!area.empty()) {
        seed ^= static_cast<unsigned>(area[0]->i * 7919 + area[0]->j * 6997);
    }
    seed ^= static_cast<unsigned>(areaContour.size() * 3571);

    std::mt19937 localRng(seed);
    auto localRandom = [&]() {
        return static_cast<float>(localRng()) / static_cast<float>(localRng.max());
    };

    // Minimum room size - rooms should be 3-8 cells ideally
    int minRoomSize = 3;
    int maxRoomSize = static_cast<int>(avgRoomSize * 1.5f);  // Around 9 cells max

    // Always subdivide if area is larger than max room size
    if (static_cast<int>(area.size()) > maxRoomSize) {
        // Continue to subdivision logic
    } else if (static_cast<int>(area.size()) <= minRoomSize) {
        // Too small to divide further
        addRoom(areaContour);
        return;
    } else {
        // Medium-sized area - randomly decide whether to subdivide
        float subdivideChance = static_cast<float>(area.size() - minRoomSize) /
                                static_cast<float>(maxRoomSize - minRoomSize);
        if (localRandom() > subdivideChance) {
            addRoom(areaContour);
            return;
        }
    }

    Edge* notch = getNotch(areaContour);
    if (!notch) {
        addRoom(areaContour);
        return;
    }

    // Extend the wall from the notch
    std::vector<Edge*> wallChain;
    wallChain.push_back(notch);

    Dir wallDir = notch->dir;

    // Find where the wall meets the opposite side
    auto findEdgeInContour = [](const std::vector<Edge*>& contour, Node* node) -> Edge* {
        for (Edge* e : contour) {
            if (e->a == node) return e;
        }
        return nullptr;
    };

    while (!findEdgeInContour(areaContour, wallChain.back()->b)) {
        Edge* next = grid->nodeToEdge(wallChain.back()->b, wallDir);
        if (!next) break;
        wallChain.push_back(next);
    }

    if (wallChain.size() <= 1) {
        addRoom(areaContour);
        return;
    }

    // Possibly make an L-shaped wall
    float lShapeChance = static_cast<float>(wallChain.size()) / avgRoomSize;
    if (localRandom() < lShapeChance) {
        Dir turnDir = localRandom() < 0.5f ? clockwise(wallDir) : counterClockwise(wallDir);
        int halfLen = static_cast<int>(wallChain.size()) / 2;
        int cutPoint = halfLen + (localRandom() < 0.5f ? 0 : 1);

        wallChain.resize(cutPoint);

        while (!findEdgeInContour(areaContour, wallChain.back()->b)) {
            Edge* next = grid->nodeToEdge(wallChain.back()->b, turnDir);
            if (!next) break;
            wallChain.push_back(next);
        }
    }

    innerWalls.push_back(wallChain.back());

    // Split the contour into two parts
    Node* splitStart = wallChain.front()->a;
    Node* splitEnd = wallChain.back()->b;

    // Find split points in original contour
    int startIdx = -1, endIdx = -1;
    for (size_t i = 0; i < areaContour.size(); ++i) {
        if (areaContour[i]->a == splitStart) startIdx = static_cast<int>(i);
        if (areaContour[i]->a == splitEnd) endIdx = static_cast<int>(i);
    }

    if (startIdx < 0 || endIdx < 0) {
        addRoom(areaContour);
        return;
    }

    // Build two new contours
    std::vector<Edge*> contour1, contour2;

    // Contour 1: from splitStart to splitEnd via original contour, plus wall
    for (int i = startIdx; i != endIdx; i = (i + 1) % areaContour.size()) {
        contour1.push_back(areaContour[i]);
    }
    for (auto& e : grid->revertChain(wallChain)) {
        contour1.push_back(e);
    }

    // Contour 2: from splitEnd to splitStart via original contour, plus wall
    for (int i = endIdx; i != startIdx; i = (i + 1) % areaContour.size()) {
        contour2.push_back(areaContour[i]);
    }
    for (auto& e : wallChain) {
        contour2.push_back(e);
    }

    // Validate that both resulting areas are non-empty and connected
    auto area1 = grid->contourToArea(contour1);
    auto area2 = grid->contourToArea(contour2);

    if (area1.empty() || area2.empty()) {
        // Invalid split, keep original area
        addRoom(areaContour);
        return;
    }

    // Recursively divide both resulting areas
    divideArea(contour1);
    divideArea(contour2);
}

Edge* Floor::getNotch(const std::vector<Edge*>& contour) {
    auto area = grid->contourToArea(contour);

    std::vector<Edge*> wallCandidates;
    std::vector<Edge*> cornerCandidates;

    int n = static_cast<int>(contour.size());
    for (int i = 0; i < n; ++i) {
        Edge* curr = contour[i];
        Edge* prev = contour[(i + n - 1) % n];

        if (curr->dir == prev->dir) {
            // Straight wall - potential notch
            Cell* c1 = grid->edgeToCell(prev);
            Cell* c2 = grid->edgeToCell(curr);
            if (!(isNarrow(area, c1) && isNarrow(area, c2))) {
                wallCandidates.push_back(curr);
            }
        } else if (curr->dir == counterClockwise(prev->dir)) {
            // Convex corner
            Cell* c = grid->edgeToCell(curr);
            Cell* cadj = grid->edgeToCell(grid->nodeToEdge(curr->a, clockwise(curr->dir)));
            if (!(isNarrow(area, c) && isNarrow(area, cadj))) {
                cornerCandidates.push_back(curr);
            }

            Cell* cprev = grid->edgeToCell(prev);
            if (!(isNarrow(area, cadj) && isNarrow(area, cprev))) {
                cornerCandidates.push_back(grid->nodeToEdge(curr->a, prev->dir));
            }
        }
    }

    std::vector<Edge*> candidates;
    if (preferCorners) {
        candidates = cornerCandidates.empty() ? wallCandidates : cornerCandidates;
    } else if (preferWalls) {
        candidates = wallCandidates.empty() ? cornerCandidates : wallCandidates;
    } else {
        candidates = wallCandidates;
        candidates.insert(candidates.end(), cornerCandidates.begin(), cornerCandidates.end());
    }

    if (candidates.empty()) return nullptr;

    // Random selection
    std::mt19937 localRng(static_cast<unsigned>(contour.size() * 12345));
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    Edge* selected = candidates[dist(localRng)];

    return grid->nodeToEdge(selected->a, clockwise(selected->dir));
}

void Floor::mergeCorridors() {
    // Find corridor-like rooms (all narrow cells)
    std::vector<Room*> corridors;
    for (auto& room : rooms) {
        if (room->narrow.size() == room->area.size()) {
            if (stairwell.has_value() && room.get() == stairwell->room) continue;
            corridors.push_back(room.get());
        }
    }

    // Merge adjacent corridors
    bool merged = true;
    while (merged) {
        merged = false;

        for (size_t i = 0; i < corridors.size() && !merged; ++i) {
            Room* r1 = corridors[i];

            for (size_t j = i + 1; j < corridors.size() && !merged; ++j) {
                Room* r2 = corridors[j];

                // Check adjacency
                int sharedEdges = 0;
                for (Edge* e1 : r1->contour) {
                    Edge* rev = grid->edgeBetween(e1->b, e1->a);
                    if (contains(r2->contour, rev)) {
                        sharedEdges++;
                        if (sharedEdges > 1) break;
                    }
                }

                if (sharedEdges == 1) {
                    // Merge r2 into r1
                    std::vector<Cell*> combinedArea = r1->area;
                    combinedArea.insert(combinedArea.end(), r2->area.begin(), r2->area.end());

                    auto combinedContour = grid->outline(combinedArea);
                    Room* mergedRoom = addRoom(combinedContour);

                    // Remove old rooms
                    corridors.erase(std::remove(corridors.begin(), corridors.end(), r1), corridors.end());
                    corridors.erase(std::remove(corridors.begin(), corridors.end(), r2), corridors.end());

                    rooms.erase(std::remove_if(rooms.begin(), rooms.end(),
                        [r1, r2](const auto& r) { return r.get() == r1 || r.get() == r2; }),
                        rooms.end());

                    corridors.push_back(mergedRoom);
                    merged = true;
                }
            }
        }
    }
}

void Floor::connectRooms() {
    // Build adjacency map
    std::map<Room*, std::map<Room*, std::vector<Edge*>>> adjacency;

    for (auto& room : rooms) {
        adjacency[room.get()] = {};
        for (auto& other : rooms) {
            adjacency[room.get()][other.get()] = {};
        }
    }

    // Find shared edges between rooms
    for (auto& room : rooms) {
        for (Edge* e : room->contour) {
            Edge* rev = grid->edgeBetween(e->b, e->a);
            if (!rev) continue;

            Cell* otherCell = grid->edgeToCell(rev);
            Room* otherRoom = getRoom(otherCell);

            if (otherRoom && otherRoom != room.get()) {
                adjacency[room.get()][otherRoom].push_back(e);
                adjacency[otherRoom][room.get()].push_back(rev);
            }
        }
    }

    // Connect stairwell to its landing room
    if (stairwell.has_value() && stairwell->room) {
        Room* stairRoom = stairwell->room;
        Room* landingRoom = getRoom(stairwell->landing);
        if (landingRoom) {
            Edge* doorEdge = grid->cellToEdge(stairwell->stair, stairwell->exit);

            auto door = std::make_unique<Door>();
            stairRoom->link(landingRoom, doorEdge, door.get());
            door->type = DoorType::Doorway;
            doorList.push_back(std::move(door));
        }
    }

    // Place doors between adjacent rooms
    std::vector<Cell*> doorCells;

    auto countNeighbors = [this, &doorCells](Cell* c, Room* room) {
        int count = 1;
        for (Dir dir : CARDINAL) {
            Cell* neighbor = grid->cell(c->i + deltaI(dir), c->j + deltaJ(dir));
            if (contains(room->area, neighbor)) count++;
        }
        if (contains(doorCells, c)) count *= 2;
        return count;
    };

    std::vector<Room*> connected;
    connected.push_back(rooms[0].get());

    for (auto& room : rooms) {
        if (room.get() == rooms[0].get()) continue;
        if (stairwell.has_value() && room.get() == stairwell->room) continue;

        // Find best door position to an already-connected room
        struct DoorCandidate {
            Room* connected;
            Edge* edge;
            int score;
        };

        std::vector<DoorCandidate> candidates;

        for (Room* conn : connected) {
            auto& sharedEdges = adjacency[room.get()][conn];
            for (Edge* e : sharedEdges) {
                Edge* rev = grid->edgeBetween(e->b, e->a);
                Cell* c1 = grid->edgeToCell(e);
                Cell* c2 = grid->edgeToCell(rev);

                int score = countNeighbors(c1, room.get()) + countNeighbors(c2, conn);
                candidates.push_back({conn, e, score});
            }
        }

        if (candidates.empty()) continue;

        // Pick lowest score (fewest neighbors = better door placement)
        auto best = std::min_element(candidates.begin(), candidates.end(),
            [](const DoorCandidate& a, const DoorCandidate& b) {
                return a.score < b.score;
            });

        auto door = std::make_unique<Door>();
        room->link(best->connected, best->edge, door.get());
        doorList.push_back(std::move(door));

        doorCells.push_back(grid->edgeToCell(best->edge));
        doorCells.push_back(grid->edgeToCell(grid->edgeBetween(best->edge->b, best->edge->a)));

        connected.push_back(room.get());
    }
}

void Floor::spawnWindows() {
    // Collect exterior edges not used by entrance or stairs
    std::vector<Edge*> candidates;

    for (auto& room : rooms) {
        for (Edge* e : room->contour) {
            // Skip if not on building exterior
            if (!contains(contour, e)) continue;

            // Skip entrance
            if (entrance.has_value() && e == entrance->door) continue;

            // Skip spiral stairs
            if (spiral.has_value()) {
                if (e == spiral->entrance || e == spiral->exit) continue;
            }

            candidates.push_back(e);
        }
    }

    // Select a portion for windows
    int numWindows = static_cast<int>(candidates.size() * windowDensity);

    std::mt19937 localRng(static_cast<unsigned>(contour.size() * 98765));
    std::shuffle(candidates.begin(), candidates.end(), localRng);

    for (int i = 0; i < numWindows && i < static_cast<int>(candidates.size()); ++i) {
        windows.push_back({candidates[i]});
    }
}

void Floor::assignRooms() {
    int floorIdx = getFloorIndex();

    for (auto& room : rooms) {
        // Default type
        room->type = RoomType::Generic;

        int size = room->size();
        int doorCount = room->countDoors();
        float narrowRatio = static_cast<float>(room->narrow.size()) / size;

        // Stairwell room
        if (stairwell.has_value() && room.get() == stairwell->room) {
            room->type = RoomType::Stairwell;
            continue;
        }

        // Corridor detection
        if (narrowRatio >= 0.8f && doorCount >= 2) {
            room->type = RoomType::Corridor;
            continue;
        }

        // Hall (ground floor, has exit, multiple doors)
        if (floorIdx == 0 && room->hasExit() && doorCount >= 2 && size >= 4) {
            room->type = RoomType::Hall;
            continue;
        }

        // Kitchen (ground floor, 4-6 cells)
        if (floorIdx == 0 && size >= 4 && size <= 6 && narrowRatio < 0.5f && doorCount <= 2) {
            room->type = RoomType::Kitchen;
            continue;
        }

        // Library (not basement, 4-9 cells, low narrow ratio)
        if (floorIdx >= 0 && size >= 4 && size <= 9 && narrowRatio < 0.8f &&
            doorCount <= 3 && !room->hasExit()) {
            room->type = RoomType::Library;
            continue;
        }

        // Bedroom (upper floors preferred)
        if (floorIdx > 0 && size >= 4 && size <= 8 && narrowRatio < 0.5f && doorCount <= 2) {
            room->type = RoomType::Bedroom;
            continue;
        }

        // Storage (basement)
        if (floorIdx < 0 && size >= 3 && size <= 6) {
            room->type = RoomType::Storage;
            continue;
        }

        // Cellar (basement, larger)
        if (floorIdx < 0 && size > 6) {
            room->type = RoomType::Cellar;
            continue;
        }

        // Lookout (top floor, small, external walls)
        if (isTopFloor() && size >= 3 && size <= 6) {
            int exteriorEdges = 0;
            for (Edge* e : room->contour) {
                if (contains(contour, e)) exteriorEdges++;
            }
            if (exteriorEdges > room->contour.size() / 2) {
                room->type = RoomType::Lookout;
                continue;
            }
        }

        // Bathroom (small, single door, any floor)
        if (size >= 2 && size <= 4 && doorCount == 1) {
            room->type = RoomType::Bathroom;
            continue;
        }

        // Study (medium size, few doors)
        if (size >= 4 && size <= 7 && doorCount <= 2 && narrowRatio < 0.3f) {
            room->type = RoomType::Study;
            continue;
        }

        // Living room (ground floor, larger)
        if (floorIdx == 0 && size >= 6) {
            room->type = RoomType::LivingRoom;
            continue;
        }

        // Dining room
        if (floorIdx == 0 && size >= 5 && size <= 8) {
            room->type = RoomType::DiningRoom;
            continue;
        }
    }
}

// JSON export
bool DwellingGenerator::saveDwellings(const std::string& path) {
    nlohmann::json j;
    j["dwellings"] = nlohmann::json::array();

    for (const auto& dwelling : dwellings) {
        nlohmann::json dj;
        dj["name"] = dwelling->name;
        dj["seed"] = dwelling->seed;
        dj["floors"] = nlohmann::json::array();

        auto floorToJson = [](const Floor& floor) {
            nlohmann::json fj;
            fj["index"] = floor.getFloorIndex();
            fj["width"] = floor.grid->width();
            fj["height"] = floor.grid->height();

            fj["rooms"] = nlohmann::json::array();
            for (const auto& room : floor.rooms) {
                nlohmann::json rj;
                rj["type"] = roomTypeName(room->type);
                rj["size"] = room->size();

                rj["cells"] = nlohmann::json::array();
                for (Cell* c : room->area) {
                    rj["cells"].push_back({{"i", c->i}, {"j", c->j}});
                }

                rj["contour"] = nlohmann::json::array();
                for (Edge* e : room->contour) {
                    rj["contour"].push_back({
                        {"a", {{"i", e->a->i}, {"j", e->a->j}}},
                        {"b", {{"i", e->b->i}, {"j", e->b->j}}}
                    });
                }

                fj["rooms"].push_back(rj);
            }

            fj["doors"] = nlohmann::json::array();
            for (const auto& door : floor.doorList) {
                if (door->edge1) {
                    fj["doors"].push_back({
                        {"a", {{"i", door->edge1->a->i}, {"j", door->edge1->a->j}}},
                        {"b", {{"i", door->edge1->b->i}, {"j", door->edge1->b->j}}}
                    });
                }
            }

            fj["windows"] = nlohmann::json::array();
            for (const auto& window : floor.windows) {
                if (window.edge) {
                    fj["windows"].push_back({
                        {"a", {{"i", window.edge->a->i}, {"j", window.edge->a->j}}},
                        {"b", {{"i", window.edge->b->i}, {"j", window.edge->b->j}}}
                    });
                }
            }

            if (floor.entrance.has_value()) {
                fj["entrance"] = {
                    {"a", {{"i", floor.entrance->door->a->i}, {"j", floor.entrance->door->a->j}}},
                    {"b", {{"i", floor.entrance->door->b->i}, {"j", floor.entrance->door->b->j}}}
                };
            }

            return fj;
        };

        for (const auto& floor : dwelling->floors) {
            dj["floors"].push_back(floorToJson(*floor));
        }

        if (dwelling->basement) {
            dj["basement"] = floorToJson(*dwelling->basement);
        }

        j["dwellings"].push_back(dj);
    }

    std::ofstream file(path);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open %s for writing", path.c_str());
        return false;
    }

    file << j.dump(2);
    return true;
}

bool DwellingGenerator::saveDwellingsSVG(const std::string& path) {
    if (dwellings.empty()) return false;

    std::string svg = DwellingSVG::generateMultiFloor(*dwellings[0], 30.0f);

    std::ofstream file(path);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open %s for writing", path.c_str());
        return false;
    }

    file << svg;
    return true;
}

bool DwellingGenerator::saveDwellingsGeoJSON(const std::string& path) {
    nlohmann::json geojson;
    geojson["type"] = "FeatureCollection";
    geojson["features"] = nlohmann::json::array();

    for (const auto& dwelling : dwellings) {
        for (const auto& floor : dwelling->floors) {
            for (const auto& room : floor->rooms) {
                nlohmann::json feature;
                feature["type"] = "Feature";
                feature["properties"] = {
                    {"dwelling", dwelling->name},
                    {"floor", floor->getFloorIndex()},
                    {"room_type", roomTypeName(room->type)},
                    {"size", room->size()}
                };

                // Create polygon from contour
                nlohmann::json coords = nlohmann::json::array();
                nlohmann::json ring = nlohmann::json::array();

                for (Edge* e : room->contour) {
                    ring.push_back({e->a->j, e->a->i});
                }
                if (!room->contour.empty()) {
                    ring.push_back({room->contour[0]->a->j, room->contour[0]->a->i});
                }

                coords.push_back(ring);
                feature["geometry"] = {
                    {"type", "Polygon"},
                    {"coordinates", coords}
                };

                geojson["features"].push_back(feature);
            }
        }
    }

    std::ofstream file(path);
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open %s for writing", path.c_str());
        return false;
    }

    file << geojson.dump(2);
    return true;
}

// SVG generation
std::string DwellingSVG::roomColor(RoomType type) {
    switch (type) {
        case RoomType::Kitchen: return "#FFE4B5";
        case RoomType::Bedroom: return "#E6E6FA";
        case RoomType::Bathroom: return "#ADD8E6";
        case RoomType::Library: return "#DEB887";
        case RoomType::Study: return "#F5DEB3";
        case RoomType::LivingRoom: return "#FAFAD2";
        case RoomType::DiningRoom: return "#FFF8DC";
        case RoomType::Hall: return "#F0F0F0";
        case RoomType::Corridor: return "#E8E8E8";
        case RoomType::Storage: return "#D3D3D3";
        case RoomType::Cellar: return "#A9A9A9";
        case RoomType::Stairwell: return "#C0C0C0";
        case RoomType::Lookout: return "#87CEEB";
        default: return "#FFFFFF";
    }
}

std::string DwellingSVG::generate(const Dwelling& dwelling, int floorIdx, float scale) {
    const Floor* floor = nullptr;
    if (floorIdx < 0 && dwelling.basement) {
        floor = dwelling.basement.get();
    } else if (floorIdx >= 0 && floorIdx < static_cast<int>(dwelling.floors.size())) {
        floor = dwelling.floors[floorIdx].get();
    }

    if (!floor) return "";

    int w = floor->grid->width();
    int h = floor->grid->height();

    std::stringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
    svg << "width=\"" << (w + 2) * scale << "\" height=\"" << (h + 2) * scale << "\">\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"#F5F5F5\"/>\n";

    float offsetX = scale;
    float offsetY = scale;

    // Draw rooms inline
    for (const auto& room : floor->rooms) {
        svg << "<path d=\"M";
        bool first = true;
        for (Edge* e : room->contour) {
            if (!first) svg << " L";
            svg << (offsetX + e->a->j * scale) << " " << (offsetY + e->a->i * scale);
            first = false;
        }
        svg << " Z\" fill=\"" << roomColor(room->type) << "\" stroke=\"#666\" stroke-width=\"0.5\"/>\n";
    }

    // Draw outer walls
    svg << "<path d=\"M";
    bool first = true;
    for (Edge* e : floor->contour) {
        if (!first) svg << " L";
        svg << (offsetX + e->a->j * scale) << " " << (offsetY + e->a->i * scale);
        first = false;
    }
    svg << " Z\" fill=\"none\" stroke=\"#000\" stroke-width=\"2\"/>\n";

    // Draw doors
    for (const auto& door : floor->doorList) {
        if (!door->edge1) continue;
        float x1 = offsetX + door->edge1->a->j * scale;
        float y1 = offsetY + door->edge1->a->i * scale;
        float x2 = offsetX + door->edge1->b->j * scale;
        float y2 = offsetY + door->edge1->b->i * scale;
        float mx = (x1 + x2) / 2;
        float my = (y1 + y2) / 2;
        svg << "<circle cx=\"" << mx << "\" cy=\"" << my << "\" r=\"3\" ";
        svg << "fill=\"#FFF\" stroke=\"#000\" stroke-width=\"1\"/>\n";
    }

    // Draw windows
    for (const auto& window : floor->windows) {
        if (!window.edge) continue;
        float x1 = offsetX + window.edge->a->j * scale;
        float y1 = offsetY + window.edge->a->i * scale;
        float x2 = offsetX + window.edge->b->j * scale;
        float y2 = offsetY + window.edge->b->i * scale;
        float mx = (x1 + x2) / 2;
        float my = (y1 + y2) / 2;
        float len = scale * 0.3f;

        if (window.edge->dir == Dir::North || window.edge->dir == Dir::South) {
            svg << "<line x1=\"" << (mx - len) << "\" y1=\"" << my << "\" ";
            svg << "x2=\"" << (mx + len) << "\" y2=\"" << my << "\" ";
        } else {
            svg << "<line x1=\"" << mx << "\" y1=\"" << (my - len) << "\" ";
            svg << "x2=\"" << mx << "\" y2=\"" << (my + len) << "\" ";
        }
        svg << "stroke=\"#4169E1\" stroke-width=\"2\"/>\n";
    }

    svg << "</svg>\n";
    return svg.str();
}

std::string DwellingSVG::generateMultiFloor(const Dwelling& dwelling, float scale) {
    if (dwelling.floors.empty()) return "";

    int maxW = 0, maxH = 0;
    for (const auto& floor : dwelling.floors) {
        maxW = std::max(maxW, floor->grid->width());
        maxH = std::max(maxH, floor->grid->height());
    }

    int numFloors = static_cast<int>(dwelling.floors.size());
    if (dwelling.basement) numFloors++;

    float floorWidth = (maxW + 3) * scale;
    float floorHeight = (maxH + 3) * scale;

    int cols = std::min(4, numFloors);
    int rows = (numFloors + cols - 1) / cols;

    std::stringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
    svg << "width=\"" << cols * floorWidth << "\" height=\"" << rows * floorHeight << "\">\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"#F5F5F5\"/>\n";

    auto drawFloor = [&](const Floor& floor, int col, int row, const std::string& label) {
        float baseX = col * floorWidth + scale;
        float baseY = row * floorHeight + scale;

        // Label
        svg << "<text x=\"" << baseX << "\" y=\"" << baseY - 5 << "\" ";
        svg << "font-family=\"sans-serif\" font-size=\"14\" fill=\"#333\">";
        svg << label << "</text>\n";

        // Draw rooms
        for (const auto& room : floor.rooms) {
            svg << "<path d=\"M";
            bool first = true;
            for (Edge* e : room->contour) {
                if (!first) svg << " L";
                svg << (baseX + e->a->j * scale) << " " << (baseY + e->a->i * scale);
                first = false;
            }
            svg << " Z\" fill=\"" << roomColor(room->type) << "\" stroke=\"#666\" stroke-width=\"0.5\"/>\n";

            // Room label
            if (!room->area.empty()) {
                float cx = 0, cy = 0;
                for (Cell* c : room->area) {
                    cx += c->j + 0.5f;
                    cy += c->i + 0.5f;
                }
                cx = baseX + (cx / room->area.size()) * scale;
                cy = baseY + (cy / room->area.size()) * scale;

                svg << "<text x=\"" << cx << "\" y=\"" << cy << "\" ";
                svg << "font-family=\"sans-serif\" font-size=\"8\" fill=\"#666\" ";
                svg << "text-anchor=\"middle\" dominant-baseline=\"middle\">";
                svg << roomTypeName(room->type) << "</text>\n";
            }
        }

        // Draw inner walls (room boundaries that aren't on building exterior)
        for (const auto& room : floor.rooms) {
            for (Edge* e : room->contour) {
                // Skip if this edge is on the building exterior
                if (contains(floor.contour, e)) continue;

                float x1 = baseX + e->a->j * scale;
                float y1 = baseY + e->a->i * scale;
                float x2 = baseX + e->b->j * scale;
                float y2 = baseY + e->b->i * scale;

                svg << "<line x1=\"" << x1 << "\" y1=\"" << y1 << "\" ";
                svg << "x2=\"" << x2 << "\" y2=\"" << y2 << "\" ";
                svg << "stroke=\"#333\" stroke-width=\"1\"/>\n";
            }
        }

        // Draw outer walls
        svg << "<path d=\"M";
        bool first = true;
        for (Edge* e : floor.contour) {
            if (!first) svg << " L";
            svg << (baseX + e->a->j * scale) << " " << (baseY + e->a->i * scale);
            first = false;
        }
        svg << " Z\" fill=\"none\" stroke=\"#000\" stroke-width=\"2\"/>\n";

        // Draw doors as gaps with door swing arcs
        for (const auto& door : floor.doorList) {
            if (!door->edge1) continue;
            float x1 = baseX + door->edge1->a->j * scale;
            float y1 = baseY + door->edge1->a->i * scale;
            float x2 = baseX + door->edge1->b->j * scale;
            float y2 = baseY + door->edge1->b->i * scale;

            float mx = (x1 + x2) / 2;
            float my = (y1 + y2) / 2;
            float doorLen = scale * 0.35f;

            // Edge direction tells us how the edge runs:
            // Edge direction: East/West edges are HORIZONTAL (run left-right), North/South are VERTICAL (run up-down)
            if (door->edge1->dir == Dir::East || door->edge1->dir == Dir::West) {
                // Horizontal edge - door leaf is horizontal, swings down into room
                // Door leaf (horizontal line)
                svg << "<line x1=\"" << mx << "\" y1=\"" << my << "\" ";
                svg << "x2=\"" << (mx + doorLen) << "\" y2=\"" << my << "\" ";
                svg << "stroke=\"#000\" stroke-width=\"1\"/>\n";
                // Door swing arc
                svg << "<path d=\"M " << (mx + doorLen) << " " << my;
                svg << " A " << doorLen << " " << doorLen << " 0 0 1 ";
                svg << mx << " " << (my + doorLen) << "\" ";
                svg << "fill=\"none\" stroke=\"#000\" stroke-width=\"0.5\"/>\n";
            } else {
                // Vertical edge - door leaf is vertical, swings right into room
                // Door leaf (vertical line)
                svg << "<line x1=\"" << mx << "\" y1=\"" << my << "\" ";
                svg << "x2=\"" << mx << "\" y2=\"" << (my + doorLen) << "\" ";
                svg << "stroke=\"#000\" stroke-width=\"1\"/>\n";
                // Door swing arc
                svg << "<path d=\"M " << mx << " " << (my + doorLen);
                svg << " A " << doorLen << " " << doorLen << " 0 0 0 ";
                svg << (mx + doorLen) << " " << my << "\" ";
                svg << "fill=\"none\" stroke=\"#000\" stroke-width=\"0.5\"/>\n";
            }
        }

        // Draw entrance
        if (floor.entrance.has_value()) {
            float x1 = baseX + floor.entrance->door->a->j * scale;
            float y1 = baseY + floor.entrance->door->a->i * scale;
            float x2 = baseX + floor.entrance->door->b->j * scale;
            float y2 = baseY + floor.entrance->door->b->i * scale;

            svg << "<line x1=\"" << x1 << "\" y1=\"" << y1 << "\" ";
            svg << "x2=\"" << x2 << "\" y2=\"" << y2 << "\" ";
            svg << "stroke=\"#8B4513\" stroke-width=\"4\"/>\n";
        }

        // Draw windows as gaps in walls with perpendicular end marks
        for (const auto& window : floor.windows) {
            if (!window.edge) continue;
            float x1 = baseX + window.edge->a->j * scale;
            float y1 = baseY + window.edge->a->i * scale;
            float x2 = baseX + window.edge->b->j * scale;
            float y2 = baseY + window.edge->b->i * scale;

            float mx = (x1 + x2) / 2;
            float my = (y1 + y2) / 2;
            float windowLen = scale * 0.3f;   // Half-width of window opening
            float tickLen = scale * 0.15f;    // Length of tick marks extending inward from wall

            // Window ticks should intersect the wall at the start and end of the window opening
            // Edge direction: East/West edges are HORIZONTAL (run left-right), North/South are VERTICAL (run up-down)
            if (window.edge->dir == Dir::East || window.edge->dir == Dir::West) {
                // Horizontal wall (runs left-right at constant y)
                // Left tick: at (mx - windowLen, my), extends vertically through wall
                svg << "<line x1=\"" << (mx - windowLen) << "\" y1=\"" << (my - tickLen) << "\" ";
                svg << "x2=\"" << (mx - windowLen) << "\" y2=\"" << (my + tickLen) << "\" ";
                svg << "stroke=\"#4169E1\" stroke-width=\"1.5\"/>\n";
                // Right tick: at (mx + windowLen, my), extends vertically through wall
                svg << "<line x1=\"" << (mx + windowLen) << "\" y1=\"" << (my - tickLen) << "\" ";
                svg << "x2=\"" << (mx + windowLen) << "\" y2=\"" << (my + tickLen) << "\" ";
                svg << "stroke=\"#4169E1\" stroke-width=\"1.5\"/>\n";
            } else {
                // Vertical wall (runs up-down at constant x)
                // Top tick: at (mx, my - windowLen), extends horizontally through wall
                svg << "<line x1=\"" << (mx - tickLen) << "\" y1=\"" << (my - windowLen) << "\" ";
                svg << "x2=\"" << (mx + tickLen) << "\" y2=\"" << (my - windowLen) << "\" ";
                svg << "stroke=\"#4169E1\" stroke-width=\"1.5\"/>\n";
                // Bottom tick: at (mx, my + windowLen), extends horizontally through wall
                svg << "<line x1=\"" << (mx - tickLen) << "\" y1=\"" << (my + windowLen) << "\" ";
                svg << "x2=\"" << (mx + tickLen) << "\" y2=\"" << (my + windowLen) << "\" ";
                svg << "stroke=\"#4169E1\" stroke-width=\"1.5\"/>\n";
            }
        }
    };

    int idx = 0;

    // Draw basement first if present
    if (dwelling.basement) {
        int col = idx % cols;
        int row = idx / cols;
        drawFloor(*dwelling.basement, col, row, "Basement");
        idx++;
    }

    // Draw floors
    for (size_t i = 0; i < dwelling.floors.size(); ++i) {
        int col = idx % cols;
        int row = idx / cols;
        std::string label = i == 0 ? "Ground Floor" : "Floor " + std::to_string(i);
        drawFloor(*dwelling.floors[i], col, row, label);
        idx++;
    }

    svg << "</svg>\n";
    return svg.str();
}

void DwellingSVG::drawRoom(std::string& svg, const Room& room, float scale, float offsetX, float offsetY) {
    // Implementation handled in generateMultiFloor
}

void DwellingSVG::drawDoor(std::string& svg, const Door& door, float scale, float offsetX, float offsetY) {
    // Implementation handled in generateMultiFloor
}

void DwellingSVG::drawWalls(std::string& svg, const Floor& floor, float scale, float offsetX, float offsetY) {
    // Implementation handled in generateMultiFloor
}

void DwellingSVG::drawWindows(std::string& svg, const Floor& floor, float scale, float offsetX, float offsetY) {
    // Implementation handled in generateMultiFloor
}

void DwellingSVG::drawStairs(std::string& svg, const Floor& floor, float scale, float offsetX, float offsetY) {
    // Implementation handled in generateMultiFloor
}

}  // namespace dwelling
