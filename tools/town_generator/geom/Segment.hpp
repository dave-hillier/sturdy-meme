/**
 * Ported from: Source/com/watabou/geom/Segment.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 *
 * IMPORTANT: In Haxe, Point is a reference type (class). Segment stores
 * references to Point objects. This C++ version uses PointPtr (shared_ptr)
 * to match those semantics.
 */
#pragma once

#include "../geom/Point.hpp"

namespace town {

class Segment {
public:
    PointPtr start;
    PointPtr end;

    Segment(PointPtr start, PointPtr end)
        : start(start), end(end) {}

    float getDx() const {
        return end->x - start->x;
    }

    float getDy() const {
        return end->y - start->y;
    }

    Point getVector() const {
        return end->subtract(*start);
    }

    float getLength() const {
        return Point::distance(*start, *end);
    }
};

} // namespace town
