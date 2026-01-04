#pragma once

#include "town_generator/geom/Point.h"
#include <memory>

namespace town_generator {
namespace geom {

// Forward declare PointPtr (also defined in Polygon.h)
using PointPtr = std::shared_ptr<Point>;

/**
 * Segment - Line segment, faithful port from Haxe TownGeneratorOS
 *
 * Uses PointPtr (shared_ptr<Point>) to match Haxe reference semantics.
 * In Haxe, Segment stores Point references and compares by identity.
 */
class Segment {
public:
    PointPtr start;
    PointPtr end;

    Segment() : start(std::make_shared<Point>()), end(std::make_shared<Point>()) {}

    // Construct from PointPtr (preserves identity)
    Segment(const PointPtr& start, const PointPtr& end) : start(start), end(end) {}

    // Construct from Point values (creates new shared points - no identity sharing)
    Segment(const Point& startVal, const Point& endVal)
        : start(std::make_shared<Point>(startVal))
        , end(std::make_shared<Point>(endVal)) {}

    // Computed properties
    double dx() const { return end->x - start->x; }
    double dy() const { return end->y - start->y; }

    Point vector() const { return end->subtract(*start); }
    double length() const { return Point::distance(*start, *end); }

    // Identity-based equality (pointer comparison, like Haxe ==)
    bool operator==(const Segment& other) const {
        return start == other.start && end == other.end;
    }

    bool operator!=(const Segment& other) const {
        return !(*this == other);
    }

    // Value-based equality (compares coordinates)
    bool valueEquals(const Segment& other) const {
        return *start == *other.start && *end == *other.end;
    }
};

} // namespace geom
} // namespace town_generator
