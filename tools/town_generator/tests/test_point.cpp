#include <doctest/doctest.h>
#include "town_generator/geom/Point.h"
#include <cmath>

using namespace town_generator::geom;

TEST_SUITE("Point basic operations") {
    TEST_CASE("Default construction") {
        Point p;
        CHECK(p.x == 0.0);
        CHECK(p.y == 0.0);
    }

    TEST_CASE("Parameterized construction") {
        Point p(3.0, 4.0);
        CHECK(p.x == 3.0);
        CHECK(p.y == 4.0);
    }

    TEST_CASE("Equality operators") {
        Point p1(1.0, 2.0);
        Point p2(1.0, 2.0);
        Point p3(3.0, 4.0);

        CHECK(p1 == p2);
        CHECK(p1 != p3);
    }

    TEST_CASE("Approximate equality") {
        Point p1(1.0, 2.0);
        Point p2(1.0000000001, 2.0000000001);

        CHECK(p1.equals(p2));
        CHECK_FALSE(p1.equals(Point(1.1, 2.0)));
    }
}

TEST_SUITE("Point arithmetic") {
    TEST_CASE("Addition operator") {
        Point p1(1.0, 2.0);
        Point p2(3.0, 4.0);
        Point result = p1 + p2;

        CHECK(result.x == 4.0);
        CHECK(result.y == 6.0);
    }

    TEST_CASE("Subtraction operator") {
        Point p1(5.0, 7.0);
        Point p2(2.0, 3.0);
        Point result = p1 - p2;

        CHECK(result.x == 3.0);
        CHECK(result.y == 4.0);
    }

    TEST_CASE("Scalar multiplication") {
        Point p(2.0, 3.0);
        Point result = p * 2.0;

        CHECK(result.x == 4.0);
        CHECK(result.y == 6.0);
    }

    TEST_CASE("add method") {
        Point p1(1.0, 2.0);
        Point p2(3.0, 4.0);
        Point result = p1.add(p2);

        CHECK(result.x == 4.0);
        CHECK(result.y == 6.0);
    }

    TEST_CASE("subtract method") {
        Point p1(5.0, 7.0);
        Point p2(2.0, 3.0);
        Point result = p1.subtract(p2);

        CHECK(result.x == 3.0);
        CHECK(result.y == 4.0);
    }

    TEST_CASE("scale method") {
        Point p(2.0, 3.0);
        Point result = p.scale(2.0);

        CHECK(result.x == 4.0);
        CHECK(result.y == 6.0);
    }
}

TEST_SUITE("Point mutation methods") {
    TEST_CASE("addEq mutates in place") {
        Point p(1.0, 2.0);
        p.addEq(Point(3.0, 4.0));

        CHECK(p.x == 4.0);
        CHECK(p.y == 6.0);
    }

    TEST_CASE("subEq mutates in place") {
        Point p(5.0, 7.0);
        p.subEq(Point(2.0, 3.0));

        CHECK(p.x == 3.0);
        CHECK(p.y == 4.0);
    }

    TEST_CASE("scaleEq mutates in place") {
        Point p(2.0, 3.0);
        p.scaleEq(2.0);

        CHECK(p.x == 4.0);
        CHECK(p.y == 6.0);
    }

    TEST_CASE("setTo mutates in place") {
        Point p(1.0, 1.0);
        p.setTo(5.0, 6.0);

        CHECK(p.x == 5.0);
        CHECK(p.y == 6.0);
    }

    TEST_CASE("set from Point mutates in place") {
        Point p(1.0, 1.0);
        p.set(Point(9.0, 10.0));

        CHECK(p.x == 9.0);
        CHECK(p.y == 10.0);
    }

    TEST_CASE("offset mutates in place") {
        Point p(3.0, 4.0);
        p.offset(1.0, 2.0);

        CHECK(p.x == 4.0);
        CHECK(p.y == 6.0);
    }

    TEST_CASE("+= operator") {
        Point p(1.0, 2.0);
        p += Point(3.0, 4.0);

        CHECK(p.x == 4.0);
        CHECK(p.y == 6.0);
    }

    TEST_CASE("-= operator") {
        Point p(5.0, 7.0);
        p -= Point(2.0, 3.0);

        CHECK(p.x == 3.0);
        CHECK(p.y == 4.0);
    }

    TEST_CASE("*= operator") {
        Point p(2.0, 3.0);
        p *= 2.0;

        CHECK(p.x == 4.0);
        CHECK(p.y == 6.0);
    }
}

TEST_SUITE("Point properties") {
    TEST_CASE("length (3-4-5 triangle)") {
        Point p(3.0, 4.0);
        CHECK(p.length() == doctest::Approx(5.0));
    }

    TEST_CASE("length of zero vector") {
        Point p(0.0, 0.0);
        CHECK(p.length() == doctest::Approx(0.0));
    }

    TEST_CASE("distance between points") {
        Point p1(0.0, 0.0);
        Point p2(3.0, 4.0);
        CHECK(Point::distance(p1, p2) == doctest::Approx(5.0));
    }

    TEST_CASE("atan returns correct angle") {
        Point p1(1.0, 0.0);
        CHECK(p1.atan() == doctest::Approx(0.0));

        Point p2(0.0, 1.0);
        CHECK(p2.atan() == doctest::Approx(M_PI / 2).epsilon(0.001));

        Point p3(-1.0, 0.0);
        CHECK(p3.atan() == doctest::Approx(M_PI).epsilon(0.001));
    }

    TEST_CASE("dot product") {
        Point p1(1.0, 0.0);
        Point p2(0.0, 1.0);
        CHECK(p1.dot(p2) == doctest::Approx(0.0));

        Point p3(2.0, 3.0);
        Point p4(4.0, 5.0);
        CHECK(p3.dot(p4) == doctest::Approx(23.0)); // 2*4 + 3*5 = 23
    }
}

TEST_SUITE("Point normalization") {
    TEST_CASE("normalize in place") {
        Point p(3.0, 4.0);
        p.normalize();

        CHECK(p.length() == doctest::Approx(1.0));
        CHECK(p.x == doctest::Approx(0.6));
        CHECK(p.y == doctest::Approx(0.8));
    }

    TEST_CASE("normalize with custom length") {
        Point p(3.0, 4.0);
        p.normalize(10.0);

        CHECK(p.length() == doctest::Approx(10.0));
        CHECK(p.x == doctest::Approx(6.0));
        CHECK(p.y == doctest::Approx(8.0));
    }

    TEST_CASE("norm returns normalized copy") {
        Point p(3.0, 4.0);
        Point normalized = p.norm();

        // Original unchanged
        CHECK(p.x == 3.0);
        CHECK(p.y == 4.0);

        // Normalized is unit length
        CHECK(normalized.length() == doctest::Approx(1.0));
    }

    TEST_CASE("normalize zero vector") {
        Point p(0.0, 0.0);
        p.normalize();

        // Should remain zero (avoid division by zero)
        CHECK(p.x == 0.0);
        CHECK(p.y == 0.0);
    }
}

TEST_SUITE("Point transformations") {
    TEST_CASE("rotate90") {
        Point p(1.0, 0.0);
        Point rotated = p.rotate90();

        CHECK(rotated.x == doctest::Approx(0.0).epsilon(0.001));
        CHECK(rotated.y == doctest::Approx(1.0).epsilon(0.001));
    }

    TEST_CASE("rotate90 twice gives negative") {
        Point p(1.0, 0.0);
        Point rotated = p.rotate90().rotate90();

        CHECK(rotated.x == doctest::Approx(-1.0).epsilon(0.001));
        CHECK(rotated.y == doctest::Approx(0.0).epsilon(0.001));
    }

    TEST_CASE("clone creates copy") {
        Point p(5.0, 6.0);
        Point cloned = p.clone();

        CHECK(cloned.x == 5.0);
        CHECK(cloned.y == 6.0);

        // Modifying original doesn't affect clone
        p.x = 100.0;
        CHECK(cloned.x == 5.0);
    }
}
