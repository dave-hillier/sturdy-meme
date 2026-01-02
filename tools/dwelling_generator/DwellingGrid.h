#pragma once

#include <vector>
#include <memory>
#include <map>
#include <cstdint>
#include <cmath>

namespace dwelling {

// Direction enum for navigation
enum class Dir {
    North = 0,  // -row
    East,       // +col
    South,      // +row
    West        // -col
};

// Get clockwise direction
inline Dir clockwise(Dir d) {
    return static_cast<Dir>((static_cast<int>(d) + 1) % 4);
}

// Get counter-clockwise direction
inline Dir counterClockwise(Dir d) {
    return static_cast<Dir>((static_cast<int>(d) + 3) % 4);
}

// Get opposite direction
inline Dir opposite(Dir d) {
    return static_cast<Dir>((static_cast<int>(d) + 2) % 4);
}

// Delta for direction
inline int di(Dir d) {
    static const int deltas[] = {-1, 0, 1, 0};
    return deltas[static_cast<int>(d)];
}

inline int dj(Dir d) {
    static const int deltas[] = {0, 1, 0, -1};
    return deltas[static_cast<int>(d)];
}

// Node (corner point in grid)
struct Node {
    int i, j;  // row, col indices

    bool operator==(const Node& other) const {
        return i == other.i && j == other.j;
    }

    bool operator<(const Node& other) const {
        if (i != other.i) return i < other.i;
        return j < other.j;
    }
};

// Cell (square in grid)
struct Cell {
    int i, j;  // row, col indices

    bool operator==(const Cell& other) const {
        return i == other.i && j == other.j;
    }

    bool operator<(const Cell& other) const {
        if (i != other.i) return i < other.i;
        return j < other.j;
    }
};

// Edge (connection between two nodes)
struct Edge {
    Node a, b;  // Two endpoints
    Dir dir;    // Direction from a to b

    bool operator==(const Edge& other) const {
        return a == other.a && b == other.b;
    }

    // Get the reversed edge
    Edge reversed() const {
        return Edge{b, a, opposite(dir)};
    }

    // Get the cell adjacent to this edge (on the left side walking from a to b)
    Cell adjacentCell() const;

    // Get position of edge center
    float centerX() const { return (a.j + b.j) * 0.5f; }
    float centerY() const { return (a.i + b.i) * 0.5f; }
};

// Grid for managing cells, nodes, and edges
class Grid {
public:
    Grid(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }

    // Access cells and nodes
    Cell* cell(int i, int j);
    const Cell* cell(int i, int j) const;
    Node* node(int i, int j);
    const Node* node(int i, int j) const;

    // Get edge between two nodes
    Edge edgeBetween(const Node& a, const Node& b) const;

    // Get edge from cell in given direction
    Edge cellEdge(const Cell& c, Dir d) const;

    // Get the cell on the other side of an edge
    Cell* edgeToCell(const Edge& e);
    const Cell* edgeToCell(const Edge& e) const;

    // Generate contour (list of edges) around a set of cells
    std::vector<Edge> outline(const std::vector<Cell*>& cells) const;

    // Convert contour to area (cells inside)
    std::vector<Cell*> contourToArea(const std::vector<Edge>& contour);

    // Check if a set of cells is connected
    bool isConnected(const std::vector<Cell*>& cells) const;

private:
    int width_, height_;
    std::vector<std::vector<Cell>> cells_;
    std::vector<std::vector<Node>> nodes_;
};

} // namespace dwelling
