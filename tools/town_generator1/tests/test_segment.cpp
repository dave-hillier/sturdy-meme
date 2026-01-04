#include <doctest/doctest.h>
#include "town_generator/geom/Segment.h"
#include <cmath>

using namespace town_generator::geom;

TEST_SUITE("Segment construction") {
    TEST_CASE("Default construction") {
        Segment seg;

        CHECK(seg.start.get() != nullptr);
        CHECK(seg.end.get() != nullptr);
        CHECK(seg.start->x == 0);
        CHECK(seg.start->y == 0);
    }

    TEST_CASE("Construct from Point values") {
        Segment seg(Point(1, 2), Point(5, 6));

        CHECK(seg.start->x == 1);
        CHECK(seg.start->y == 2);
        CHECK(seg.end->x == 5);
        CHECK(seg.end->y == 6);
    }

    TEST_CASE("Construct from PointPtr") {
        auto start = std::make_shared<Point>(0, 0);
        auto end = std::make_shared<Point>(3, 4);

        Segment seg(start, end);

        CHECK(seg.start.get() == start.get());
        CHECK(seg.end.get() == end.get());
    }
}

TEST_SUITE("Segment properties") {
    TEST_CASE("dx returns x difference") {
        Segment seg(Point(1, 2), Point(4, 6));
        CHECK(seg.dx() == 3);
    }

    TEST_CASE("dy returns y difference") {
        Segment seg(Point(1, 2), Point(4, 6));
        CHECK(seg.dy() == 4);
    }

    TEST_CASE("vector returns difference as Point") {
        Segment seg(Point(1, 1), Point(4, 5));
        Point v = seg.vector();

        CHECK(v.x == 3);
        CHECK(v.y == 4);
    }

    TEST_CASE("length returns euclidean distance") {
        Segment seg(Point(0, 0), Point(3, 4));
        CHECK(seg.length() == doctest::Approx(5.0));
    }

    TEST_CASE("Zero length segment") {
        Segment seg(Point(5, 5), Point(5, 5));
        CHECK(seg.length() == doctest::Approx(0.0));
    }
}

TEST_SUITE("Segment equality") {
    TEST_CASE("Identity equality (same pointers)") {
        auto start = std::make_shared<Point>(0, 0);
        auto end = std::make_shared<Point>(3, 4);

        Segment seg1(start, end);
        Segment seg2(start, end);

        CHECK(seg1 == seg2);
    }

    TEST_CASE("Different pointers not equal") {
        Segment seg1(Point(0, 0), Point(3, 4));
        Segment seg2(Point(0, 0), Point(3, 4));

        // Even with same values, different pointers means not equal
        CHECK(seg1 != seg2);
    }

    TEST_CASE("Value equality") {
        Segment seg1(Point(0, 0), Point(3, 4));
        Segment seg2(Point(0, 0), Point(3, 4));

        CHECK(seg1.valueEquals(seg2));
    }
}

TEST_SUITE("Segment shared pointer semantics") {
    TEST_CASE("Mutating start affects length") {
        auto start = std::make_shared<Point>(0, 0);
        auto end = std::make_shared<Point>(3, 4);
        Segment seg(start, end);

        CHECK(seg.length() == doctest::Approx(5.0));

        // Move start point
        start->x = 3;
        start->y = 0;

        // Length should now be 4 (from (3,0) to (3,4))
        CHECK(seg.length() == doctest::Approx(4.0));
    }

    TEST_CASE("Mutating end affects vector") {
        auto start = std::make_shared<Point>(0, 0);
        auto end = std::make_shared<Point>(5, 0);
        Segment seg(start, end);

        CHECK(seg.dx() == 5);
        CHECK(seg.dy() == 0);

        // Move end point
        end->y = 5;

        CHECK(seg.dx() == 5);
        CHECK(seg.dy() == 5);
    }

    TEST_CASE("Two segments sharing a point see mutations") {
        auto shared = std::make_shared<Point>(5, 5);
        auto end1 = std::make_shared<Point>(10, 5);
        auto end2 = std::make_shared<Point>(5, 10);

        Segment seg1(shared, end1);
        Segment seg2(shared, end2);

        // Move the shared point
        shared->x = 0;
        shared->y = 0;

        // Both segments now start from origin
        CHECK(seg1.start->x == 0);
        CHECK(seg1.start->y == 0);
        CHECK(seg2.start->x == 0);
        CHECK(seg2.start->y == 0);

        CHECK(seg1.dx() == 10);
        CHECK(seg2.dy() == 10);
    }
}
