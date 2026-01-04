/**
 * Ported from: Source/com/watabou/geom/Segment.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 *
 * Segment stores Point values (not shared_ptr) as it represents a geometric
 * line segment for temporary calculations. This matches the Haxe usage where
 * Segment is used for intermediate computations, not for sharing Point references.
 */
#pragma once

#include "../geom/Point.hpp"
#include <memory>

namespace town {

class Segment {
public:
    Point start;
    Point end;

    Segment(const Point& start, const Point& end)
        : start(start), end(end) {}

    // Constructor from shared_ptr<Point> - dereferences for value storage
    Segment(const std::shared_ptr<Point>& start, const std::shared_ptr<Point>& end)
        : start(*start), end(*end) {}

    float getDx() const {
        return end.x - start.x;
    }

    float getDy() const {
        return end.y - start.y;
    }

    Point getVector() const {
        return end.subtract(start);
    }

    float getLength() const {
        return Point::distance(start, end);
    }
};

} // namespace town
