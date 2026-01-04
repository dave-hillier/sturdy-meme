#include <doctest/doctest.h>
#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Polygon.hpp"

using namespace town_generator2::geom;

TEST_SUITE("Point") {
    TEST_CASE("Point construction and basic operations") {
        Point p1(3.0, 4.0);
        CHECK(p1.x == 3.0);
        CHECK(p1.y == 4.0);
        CHECK(p1.length() == doctest::Approx(5.0));
    }

    TEST_CASE("Point arithmetic") {
        Point p1(1.0, 2.0);
        Point p2(3.0, 4.0);

        Point sum = p1.add(p2);
        CHECK(sum.x == 4.0);
        CHECK(sum.y == 6.0);

        Point diff = p2.subtract(p1);
        CHECK(diff.x == 2.0);
        CHECK(diff.y == 2.0);

        Point scaled = p1.scale(2.0);
        CHECK(scaled.x == 2.0);
        CHECK(scaled.y == 4.0);
    }

    TEST_CASE("Point mutation with shared_ptr") {
        auto ptr1 = makePoint(1.0, 2.0);
        auto ptr2 = ptr1; // Same shared_ptr

        ptr1->addEq(Point(1.0, 1.0));

        // Both should see the change
        CHECK(ptr1->x == 2.0);
        CHECK(ptr2->x == 2.0);
        CHECK(ptr1.get() == ptr2.get());
    }

    TEST_CASE("Point distance") {
        Point p1(0.0, 0.0);
        Point p2(3.0, 4.0);
        CHECK(Point::distance(p1, p2) == doctest::Approx(5.0));
    }

    TEST_CASE("Point rotate90") {
        Point p(1.0, 0.0);
        Point rotated = p.rotate90();
        CHECK(rotated.x == doctest::Approx(0.0).epsilon(0.0001));
        CHECK(rotated.y == doctest::Approx(1.0).epsilon(0.0001));
    }

    TEST_CASE("Point norm") {
        Point p(3.0, 4.0);
        Point normed = p.norm(1.0);
        CHECK(normed.length() == doctest::Approx(1.0));
    }
}

TEST_SUITE("Polygon basics") {
    TEST_CASE("Polygon construction") {
        Polygon poly({Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)});
        CHECK(poly.length() == 4);
    }

    TEST_CASE("Polygon area (square)") {
        Polygon square({Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)});
        CHECK(square.square() == doctest::Approx(1.0));
    }

    TEST_CASE("Polygon area (triangle)") {
        Polygon tri({Point(0, 0), Point(2, 0), Point(1, 2)});
        CHECK(tri.square() == doctest::Approx(2.0));
    }

    TEST_CASE("Polygon center") {
        Polygon square({Point(0, 0), Point(2, 0), Point(2, 2), Point(0, 2)});
        Point c = square.center();
        CHECK(c.x == doctest::Approx(1.0));
        CHECK(c.y == doctest::Approx(1.0));
    }

    TEST_CASE("Polygon perimeter") {
        Polygon square({Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)});
        CHECK(square.perimeter() == doctest::Approx(4.0));
    }

    TEST_CASE("Polygon compactness (circle approximation)") {
        Polygon circle = Polygon::circle(1.0);
        // Circle should have compactness close to 1.0
        CHECK(circle.compactness() > 0.9);
    }

    TEST_CASE("Polygon isConvex") {
        Polygon square({Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)});
        CHECK(square.isConvex());

        // L-shape is not convex
        Polygon lShape({
            Point(0, 0), Point(2, 0), Point(2, 1),
            Point(1, 1), Point(1, 2), Point(0, 2)
        });
        CHECK_FALSE(lShape.isConvex());
    }
}

TEST_SUITE("Polygon pointer semantics") {
    TEST_CASE("indexOf by pointer identity") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);

        Polygon poly({p1, p2, p3});

        CHECK(poly.indexOf(p1) == 0);
        CHECK(poly.indexOf(p2) == 1);
        CHECK(poly.indexOf(p3) == 2);

        // Different pointer, same coordinates
        auto p1copy = makePoint(0, 0);
        CHECK(poly.indexOf(p1copy) == -1);
    }

    TEST_CASE("indexOfByValue by coordinates") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);

        Polygon poly({p1, p2, p3});

        CHECK(poly.indexOfByValue(Point(0, 0)) == 0);
        CHECK(poly.indexOfByValue(Point(1, 0)) == 1);
        CHECK(poly.indexOfByValue(Point(1, 1)) == 2);
        CHECK(poly.indexOfByValue(Point(9, 9)) == -1);
    }

    TEST_CASE("Copy shares pointers (reference semantics)") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);

        Polygon poly1({p1, p2, p3});
        Polygon poly2 = poly1; // Copy

        // Mutate original point
        p1->x = 5.0;

        // Both polygons see the change
        CHECK(poly1[0].x == 5.0);
        CHECK(poly2[0].x == 5.0);
    }

    TEST_CASE("deepCopy creates independent points") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);

        Polygon poly1({p1, p2, p3});
        Polygon poly2 = poly1.deepCopy();

        // Mutate original point
        p1->x = 5.0;

        // Only poly1 sees the change
        CHECK(poly1[0].x == 5.0);
        CHECK(poly2[0].x == 0.0);
    }

    TEST_CASE("findEdge by pointer identity") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);
        auto p4 = makePoint(0, 1);

        Polygon poly({p1, p2, p3, p4});

        CHECK(poly.findEdge(p1, p2) == 0);
        CHECK(poly.findEdge(p2, p3) == 1);
        CHECK(poly.findEdge(p3, p4) == 2);
        CHECK(poly.findEdge(p4, p1) == 3);

        // Wrong direction
        CHECK(poly.findEdge(p2, p1) == -1);
    }
}

TEST_SUITE("Polygon cut operation") {
    TEST_CASE("Cut square horizontally") {
        Polygon square({Point(0, 0), Point(2, 0), Point(2, 2), Point(0, 2)});

        // Cut with horizontal line at y=1
        auto halves = square.cut(Point(-1, 1), Point(3, 1));

        CHECK(halves.size() == 2);

        // Each half should have area of 2
        double area1 = std::abs(halves[0].square());
        double area2 = std::abs(halves[1].square());
        CHECK(area1 == doctest::Approx(2.0).epsilon(0.01));
        CHECK(area2 == doctest::Approx(2.0).epsilon(0.01));
    }

    TEST_CASE("Cut square vertically") {
        Polygon square({Point(0, 0), Point(2, 0), Point(2, 2), Point(0, 2)});

        // Cut with vertical line at x=1
        auto halves = square.cut(Point(1, -1), Point(1, 3));

        CHECK(halves.size() == 2);

        double area1 = std::abs(halves[0].square());
        double area2 = std::abs(halves[1].square());
        CHECK(area1 == doctest::Approx(2.0).epsilon(0.01));
        CHECK(area2 == doctest::Approx(2.0).epsilon(0.01));
    }

    TEST_CASE("Cut misses polygon") {
        Polygon square({Point(0, 0), Point(2, 0), Point(2, 2), Point(0, 2)});

        // Line outside polygon
        auto result = square.cut(Point(-5, -5), Point(-3, -5));

        // Should return original polygon (as deep copy)
        CHECK(result.size() == 1);
        CHECK(std::abs(result[0].square()) == doctest::Approx(4.0));
    }
}

TEST_SUITE("Polygon shrink operation") {
    TEST_CASE("Shrink square uniformly") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});

        Polygon shrunk = square.shrinkEq(1.0);

        // Should be 8x8 = 64
        CHECK(std::abs(shrunk.square()) == doctest::Approx(64.0).epsilon(1.0));
    }

    TEST_CASE("Shrink with varying distances") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});

        // Shrink only top edge
        std::vector<double> distances = {0, 0, 0, 2};
        Polygon shrunk = square.shrink(distances);

        // Area should be less than 100 but more than 64
        double area = std::abs(shrunk.square());
        CHECK(area < 100.0);
        CHECK(area > 60.0);
    }
}

TEST_SUITE("Polygon buffer operation") {
    TEST_CASE("Buffer square uniformly (shrink)") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});

        // Negative buffer = shrink
        Polygon buffered = square.bufferEq(-1.0);

        double area = std::abs(buffered.square());
        CHECK(area == doctest::Approx(64.0).epsilon(2.0));
    }

    TEST_CASE("Buffer square uniformly (expand)") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});

        // Positive buffer = expand
        Polygon buffered = square.bufferEq(1.0);

        double area = std::abs(buffered.square());
        CHECK(area == doctest::Approx(144.0).epsilon(2.0));
    }

    TEST_CASE("Buffer with self-intersection handling") {
        // A thin rectangle that would self-intersect if buffered too much
        Polygon thin({Point(0, 0), Point(10, 0), Point(10, 2), Point(0, 2)});

        // Buffer by 0.5 should work fine
        Polygon buffered = thin.bufferEq(-0.5);
        CHECK(buffered.length() >= 4);
        CHECK(std::abs(buffered.square()) > 0);
    }
}

TEST_SUITE("Polygon split operation") {
    TEST_CASE("Split square at two vertices") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(2, 0);
        auto p3 = makePoint(2, 2);
        auto p4 = makePoint(0, 2);

        Polygon square({p1, p2, p3, p4});

        auto halves = square.split(p1, p3);

        CHECK(halves.size() == 2);

        // Each triangle should have area of 2
        double area1 = std::abs(halves[0].square());
        double area2 = std::abs(halves[1].square());
        CHECK(area1 == doctest::Approx(2.0).epsilon(0.01));
        CHECK(area2 == doctest::Approx(2.0).epsilon(0.01));
    }
}

TEST_SUITE("Polygon borders check") {
    TEST_CASE("Adjacent squares share edge") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);
        auto p4 = makePoint(0, 1);
        auto p5 = makePoint(2, 0);
        auto p6 = makePoint(2, 1);

        Polygon sq1({p1, p2, p3, p4});
        Polygon sq2({p2, p5, p6, p3}); // Shares edge p2-p3

        CHECK(sq1.borders(sq2));
        CHECK(sq2.borders(sq1));
    }

    TEST_CASE("Non-adjacent squares don't share edge") {
        Polygon sq1({Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)});
        Polygon sq2({Point(5, 5), Point(6, 5), Point(6, 6), Point(5, 6)});

        CHECK_FALSE(sq1.borders(sq2));
    }
}

TEST_SUITE("Polygon factory methods") {
    TEST_CASE("rect creates rectangle centered at origin") {
        Polygon r = Polygon::rect(4.0, 2.0);
        CHECK(r.length() == 4);
        CHECK(std::abs(r.square()) == doctest::Approx(8.0));

        Point c = r.center();
        CHECK(c.x == doctest::Approx(0.0).epsilon(0.01));
        CHECK(c.y == doctest::Approx(0.0).epsilon(0.01));
    }

    TEST_CASE("regular creates regular polygon") {
        Polygon hex = Polygon::regular(6, 1.0);
        CHECK(hex.length() == 6);

        // Regular hexagon area = 3*sqrt(3)/2 * r^2 ~= 2.598
        CHECK(std::abs(hex.square()) == doctest::Approx(2.598).epsilon(0.1));
    }

    TEST_CASE("circle creates 16-gon approximation") {
        Polygon c = Polygon::circle(1.0);
        CHECK(c.length() == 16);

        // Should approximate pi
        CHECK(std::abs(c.square()) == doctest::Approx(M_PI).epsilon(0.1));
    }
}
