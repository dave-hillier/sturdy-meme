#pragma once

#include "town_generator2/geom/Point.hpp"

namespace town_generator2 {
namespace geom {

/**
 * Segment - Line segment between two points
 */
struct Segment {
    PointPtr start;
    PointPtr end;

    Segment() : start(makePoint()), end(makePoint()) {}
    Segment(PointPtr s, PointPtr e) : start(s), end(e) {}
    Segment(const Point& s, const Point& e) : start(makePoint(s)), end(makePoint(e)) {}

    double dx() const { return end->x - start->x; }
    double dy() const { return end->y - start->y; }

    Point vector() const { return end->subtract(*start); }
    double length() const { return Point::distance(*start, *end); }
};

} // namespace geom
} // namespace town_generator2
