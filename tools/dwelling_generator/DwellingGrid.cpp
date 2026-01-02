#include "DwellingGrid.h"
#include <algorithm>
#include <queue>
#include <set>

namespace dwelling {

Cell Edge::adjacentCell() const {
    // Cell is on the left side of edge going from a to b
    switch (dir) {
        case Dir::East:  return Cell{a.i, a.j};      // Cell below edge
        case Dir::South: return Cell{a.i, a.j - 1};  // Cell left of edge
        case Dir::West:  return Cell{a.i - 1, a.j - 1}; // Cell above edge
        case Dir::North: return Cell{a.i - 1, a.j};  // Cell right of edge
    }
    return Cell{-1, -1};
}

Grid::Grid(int width, int height)
    : width_(width), height_(height)
{
    // Create nodes (corners) - one more than cells in each dimension
    nodes_.resize(height + 1);
    for (int i = 0; i <= height; ++i) {
        nodes_[i].resize(width + 1);
        for (int j = 0; j <= width; ++j) {
            nodes_[i][j] = Node{i, j};
        }
    }

    // Create cells
    cells_.resize(height);
    for (int i = 0; i < height; ++i) {
        cells_[i].resize(width);
        for (int j = 0; j < width; ++j) {
            cells_[i][j] = Cell{i, j};
        }
    }
}

Cell* Grid::cell(int i, int j) {
    if (i < 0 || i >= height_ || j < 0 || j >= width_) return nullptr;
    return &cells_[i][j];
}

const Cell* Grid::cell(int i, int j) const {
    if (i < 0 || i >= height_ || j < 0 || j >= width_) return nullptr;
    return &cells_[i][j];
}

Node* Grid::node(int i, int j) {
    if (i < 0 || i > height_ || j < 0 || j > width_) return nullptr;
    return &nodes_[i][j];
}

const Node* Grid::node(int i, int j) const {
    if (i < 0 || i > height_ || j < 0 || j > width_) return nullptr;
    return &nodes_[i][j];
}

Edge Grid::edgeBetween(const Node& a, const Node& b) const {
    Dir d;
    if (b.i < a.i) d = Dir::North;
    else if (b.i > a.i) d = Dir::South;
    else if (b.j > a.j) d = Dir::East;
    else d = Dir::West;
    return Edge{a, b, d};
}

Edge Grid::cellEdge(const Cell& c, Dir d) const {
    switch (d) {
        case Dir::North:
            return Edge{nodes_[c.i][c.j], nodes_[c.i][c.j + 1], Dir::East};
        case Dir::East:
            return Edge{nodes_[c.i][c.j + 1], nodes_[c.i + 1][c.j + 1], Dir::South};
        case Dir::South:
            return Edge{nodes_[c.i + 1][c.j + 1], nodes_[c.i + 1][c.j], Dir::West};
        case Dir::West:
            return Edge{nodes_[c.i + 1][c.j], nodes_[c.i][c.j], Dir::North};
    }
    return Edge{};
}

Cell* Grid::edgeToCell(const Edge& e) {
    Cell c = e.adjacentCell();
    return cell(c.i, c.j);
}

const Cell* Grid::edgeToCell(const Edge& e) const {
    Cell c = e.adjacentCell();
    return cell(c.i, c.j);
}

std::vector<Edge> Grid::outline(const std::vector<Cell*>& cells) const {
    if (cells.empty()) return {};

    // Find all boundary edges
    std::vector<Edge> boundaryEdges;

    for (const Cell* c : cells) {
        // Check each direction
        for (int d = 0; d < 4; ++d) {
            Dir dir = static_cast<Dir>(d);
            int ni = c->i + di(dir);
            int nj = c->j + dj(dir);

            // Check if neighbor is not in cells
            const Cell* neighbor = cell(ni, nj);
            bool neighborInCells = false;
            if (neighbor) {
                for (const Cell* other : cells) {
                    if (*other == *neighbor) {
                        neighborInCells = true;
                        break;
                    }
                }
            }

            if (!neighborInCells) {
                // This is a boundary edge
                boundaryEdges.push_back(cellEdge(*c, dir));
            }
        }
    }

    if (boundaryEdges.empty()) return {};

    // Order edges into a continuous contour
    std::vector<Edge> contour;
    std::set<std::pair<std::pair<int,int>, std::pair<int,int>>> used;

    Edge current = boundaryEdges[0];
    contour.push_back(current);
    used.insert({{current.a.i, current.a.j}, {current.b.i, current.b.j}});

    while (contour.size() < boundaryEdges.size()) {
        Node target = current.b;
        bool found = false;

        // Find edge starting at target, preferring to turn right (clockwise)
        for (int turn = 0; turn < 3 && !found; ++turn) {
            Dir searchDir;
            if (turn == 0) searchDir = clockwise(current.dir);       // Right turn
            else if (turn == 1) searchDir = current.dir;             // Straight
            else searchDir = counterClockwise(current.dir);          // Left turn

            for (const Edge& e : boundaryEdges) {
                if (e.a == target && e.dir == searchDir) {
                    auto key = std::make_pair(
                        std::make_pair(e.a.i, e.a.j),
                        std::make_pair(e.b.i, e.b.j)
                    );
                    if (used.find(key) == used.end()) {
                        contour.push_back(e);
                        used.insert(key);
                        current = e;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (!found) {
            // Just find any unused edge starting at target
            for (const Edge& e : boundaryEdges) {
                if (e.a == target) {
                    auto key = std::make_pair(
                        std::make_pair(e.a.i, e.a.j),
                        std::make_pair(e.b.i, e.b.j)
                    );
                    if (used.find(key) == used.end()) {
                        contour.push_back(e);
                        used.insert(key);
                        current = e;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (!found) break;
    }

    return contour;
}

std::vector<Cell*> Grid::contourToArea(const std::vector<Edge>& contour) {
    if (contour.empty()) return {};

    // Start with the cell adjacent to the first edge
    Cell* start = edgeToCell(contour[0]);
    if (!start) return {};

    std::vector<Cell*> area;
    std::queue<Cell*> queue;
    std::set<std::pair<int,int>> visited;

    queue.push(start);
    visited.insert({start->i, start->j});

    while (!queue.empty()) {
        Cell* c = queue.front();
        queue.pop();
        area.push_back(c);

        // Check neighbors
        for (int d = 0; d < 4; ++d) {
            Dir dir = static_cast<Dir>(d);
            Edge edge = cellEdge(*c, dir);

            // Check if this edge is in the contour (boundary)
            bool isBoundary = false;
            for (const Edge& ce : contour) {
                if ((ce.a == edge.a && ce.b == edge.b) ||
                    (ce.a == edge.b && ce.b == edge.a)) {
                    isBoundary = true;
                    break;
                }
            }

            if (!isBoundary) {
                int ni = c->i + di(dir);
                int nj = c->j + dj(dir);
                Cell* neighbor = cell(ni, nj);
                if (neighbor && visited.find({ni, nj}) == visited.end()) {
                    queue.push(neighbor);
                    visited.insert({ni, nj});
                }
            }
        }
    }

    return area;
}

bool Grid::isConnected(const std::vector<Cell*>& cells) const {
    if (cells.size() <= 1) return true;

    std::set<std::pair<int,int>> cellSet;
    for (const Cell* c : cells) {
        cellSet.insert({c->i, c->j});
    }

    std::set<std::pair<int,int>> visited;
    std::queue<std::pair<int,int>> queue;

    queue.push({cells[0]->i, cells[0]->j});
    visited.insert({cells[0]->i, cells[0]->j});

    while (!queue.empty()) {
        auto [i, j] = queue.front();
        queue.pop();

        for (int d = 0; d < 4; ++d) {
            Dir dir = static_cast<Dir>(d);
            int ni = i + di(dir);
            int nj = j + dj(dir);
            auto pos = std::make_pair(ni, nj);

            if (cellSet.count(pos) && !visited.count(pos)) {
                visited.insert(pos);
                queue.push(pos);
            }
        }
    }

    return visited.size() == cells.size();
}

} // namespace dwelling
