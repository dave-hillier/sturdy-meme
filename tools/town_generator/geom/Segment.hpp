/**
 * Ported from: Source/com/watabou/geom/Segment.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */
#pragma once

#include "../geom/Point.hpp"

namespace town {

class Segment {
public:
    Point start;
    Point end;

    Segment(const Point& start, const Point& end)
        : start(start), end(end) {}

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
