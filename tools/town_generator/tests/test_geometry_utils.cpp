#include <doctest/doctest.h>
#include "town_generator/geom/Point.h"
#include "town_generator/geom/Segment.h"
#include "town_generator/geom/Spline.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/geom/Circle.h"
#include "town_generator/utils/MathUtils.h"
#include <cmath>

using namespace town_generator::geom;
using namespace town_generator::utils;

TEST_SUITE("GeomUtils intersectLines") {
    TEST_CASE("Perpendicular lines") {
        // Horizontal line at y=5 from x=0
        // Vertical line at x=3 from y=0
        auto result = GeomUtils::intersectLines(
            0, 5, 10, 0,   // horizontal line: (0,5) + t*(10,0)
            3, 0, 0, 10    // vertical line: (3,0) + t*(0,10)
        );

        CHECK(result.has_value());
        CHECK(result->x == doctest::Approx(0.3).epsilon(0.01));
        CHECK(result->y == doctest::Approx(0.5).epsilon(0.01));
    }

    TEST_CASE("Parallel lines (no intersection)") {
        auto result = GeomUtils::intersectLines(
            0, 0, 10, 0,   // line at y=0
            0, 5, 10, 0    // line at y=5
        );

        CHECK_FALSE(result.has_value());
    }

    TEST_CASE("Diagonal lines") {
        // Line from origin with slope 1
        // Line from (0,2) with slope -1
        auto result = GeomUtils::intersectLines(
            0, 0, 1, 1,    // y = x
            0, 2, 1, -1    // y = -x + 2
        );

        CHECK(result.has_value());
        CHECK(result->x == doctest::Approx(1.0).epsilon(0.01));
        CHECK(result->y == doctest::Approx(1.0).epsilon(0.01));
    }
}

TEST_SUITE("GeomUtils interpolate") {
    TEST_CASE("Midpoint") {
        Point p1(0, 0);
        Point p2(10, 10);

        Point mid = GeomUtils::interpolate(p1, p2, 0.5);

        CHECK(mid.x == doctest::Approx(5.0));
        CHECK(mid.y == doctest::Approx(5.0));
    }

    TEST_CASE("At start") {
        Point p1(0, 0);
        Point p2(10, 10);

        Point start = GeomUtils::interpolate(p1, p2, 0.0);

        CHECK(start.x == doctest::Approx(0.0));
        CHECK(start.y == doctest::Approx(0.0));
    }

    TEST_CASE("At end") {
        Point p1(0, 0);
        Point p2(10, 10);

        Point end = GeomUtils::interpolate(p1, p2, 1.0);

        CHECK(end.x == doctest::Approx(10.0));
        CHECK(end.y == doctest::Approx(10.0));
    }

    TEST_CASE("Quarter") {
        Point p1(0, 0);
        Point p2(8, 4);

        Point quarter = GeomUtils::interpolate(p1, p2, 0.25);

        CHECK(quarter.x == doctest::Approx(2.0));
        CHECK(quarter.y == doctest::Approx(1.0));
    }
}

TEST_SUITE("GeomUtils scalar and cross") {
    TEST_CASE("Scalar (dot product) - perpendicular") {
        CHECK(GeomUtils::scalar(1, 0, 0, 1) == doctest::Approx(0.0));
    }

    TEST_CASE("Scalar (dot product) - parallel") {
        CHECK(GeomUtils::scalar(2, 0, 3, 0) == doctest::Approx(6.0));
    }

    TEST_CASE("Scalar (dot product) - general") {
        CHECK(GeomUtils::scalar(1, 2, 3, 4) == doctest::Approx(11.0)); // 1*3 + 2*4
    }

    TEST_CASE("Cross product - i x j") {
        CHECK(GeomUtils::cross(1, 0, 0, 1) == doctest::Approx(1.0));
    }

    TEST_CASE("Cross product - j x i") {
        CHECK(GeomUtils::cross(0, 1, 1, 0) == doctest::Approx(-1.0));
    }

    TEST_CASE("Cross product - parallel vectors") {
        CHECK(GeomUtils::cross(2, 0, 4, 0) == doctest::Approx(0.0));
    }

    TEST_CASE("Cross product - general") {
        // (1,2) x (3,4) = 1*4 - 2*3 = -2
        CHECK(GeomUtils::cross(1, 2, 3, 4) == doctest::Approx(-2.0));
    }
}

TEST_SUITE("GeomUtils distance2line") {
    TEST_CASE("Point on line") {
        double d = GeomUtils::distance2line(0, 0, 10, 0, 5, 0);
        CHECK(d == doctest::Approx(0.0).epsilon(0.001));
    }

    TEST_CASE("Point above line") {
        double d = GeomUtils::distance2line(0, 0, 1, 0, 5, 3);
        CHECK(std::abs(d) == doctest::Approx(3.0).epsilon(0.001));
    }
}

TEST_SUITE("Circle") {
    TEST_CASE("Default construction") {
        Circle c;

        CHECK(c.x == 0);
        CHECK(c.y == 0);
        CHECK(c.r == 0);
    }

    TEST_CASE("Parameterized construction") {
        Circle c(3.0, 4.0, 5.0);

        CHECK(c.x == 3.0);
        CHECK(c.y == 4.0);
        CHECK(c.r == 5.0);
    }
}

TEST_SUITE("Spline") {
    TEST_CASE("startCurve produces control points") {
        Point p0(0, 0);
        Point p1(5, 0);
        Point p2(10, 0);

        auto result = Spline::startCurve(p0, p1, p2);

        CHECK(result.size() == 2);
        CHECK(result[1].x == doctest::Approx(5.0));
        CHECK(result[1].y == doctest::Approx(0.0));
    }

    TEST_CASE("endCurve produces control points") {
        Point p0(0, 0);
        Point p1(5, 0);
        Point p2(10, 0);

        auto result = Spline::endCurve(p0, p1, p2);

        CHECK(result.size() == 2);
        CHECK(result[1].x == doctest::Approx(10.0));
        CHECK(result[1].y == doctest::Approx(0.0));
    }

    TEST_CASE("midCurve produces 4 points") {
        Point p0(0, 0);
        Point p1(5, 0);
        Point p2(10, 0);
        Point p3(15, 0);

        auto result = Spline::midCurve(p0, p1, p2, p3);

        CHECK(result.size() == 4);
        CHECK(result[3].x == doctest::Approx(10.0));
        CHECK(result[3].y == doctest::Approx(0.0));
    }
}

TEST_SUITE("MathUtils") {
    TEST_CASE("gate - clamp double") {
        CHECK(MathUtils::gate(5.0, 0.0, 10.0) == 5.0);
        CHECK(MathUtils::gate(-5.0, 0.0, 10.0) == 0.0);
        CHECK(MathUtils::gate(15.0, 0.0, 10.0) == 10.0);
        CHECK(MathUtils::gate(0.0, 0.0, 10.0) == 0.0);
        CHECK(MathUtils::gate(10.0, 0.0, 10.0) == 10.0);
    }

    TEST_CASE("gatei - clamp int") {
        CHECK(MathUtils::gatei(5, 0, 10) == 5);
        CHECK(MathUtils::gatei(-5, 0, 10) == 0);
        CHECK(MathUtils::gatei(15, 0, 10) == 10);
        CHECK(MathUtils::gatei(0, 0, 10) == 0);
        CHECK(MathUtils::gatei(10, 0, 10) == 10);
    }

    TEST_CASE("sign") {
        CHECK(MathUtils::sign(5.0) == 1);
        CHECK(MathUtils::sign(-5.0) == -1);
        CHECK(MathUtils::sign(0.0) == 0);
        CHECK(MathUtils::sign(0.001) == 1);
        CHECK(MathUtils::sign(-0.001) == -1);
    }
}
