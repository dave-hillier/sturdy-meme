#include <doctest/doctest.h>
#include "town_generator/geom/Polygon.h"
#include <cmath>

using namespace town_generator::geom;

TEST_SUITE("Polygon construction") {
    TEST_CASE("Empty polygon") {
        Polygon poly;
        CHECK(poly.size() == 0);
        CHECK(poly.empty());
    }

    TEST_CASE("Construct from Point initializer list") {
        Polygon poly{Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)};

        CHECK(poly.size() == 4);
        CHECK(poly[0].x == 0);
        CHECK(poly[0].y == 0);
    }

    TEST_CASE("Construct from vector of Points") {
        std::vector<Point> pts = {Point(0, 0), Point(1, 0), Point(1, 1)};
        Polygon poly(pts);

        CHECK(poly.size() == 3);
    }

    TEST_CASE("Construct from PointPtr vector") {
        std::vector<PointPtr> pts;
        pts.push_back(makePoint(0, 0));
        pts.push_back(makePoint(1, 0));
        pts.push_back(makePoint(1, 1));

        Polygon poly(pts);
        CHECK(poly.size() == 3);
    }
}

TEST_SUITE("Polygon computed properties") {
    TEST_CASE("square - unit square") {
        Polygon poly{Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)};

        CHECK(std::abs(poly.square()) == doctest::Approx(1.0).epsilon(0.001));
    }

    TEST_CASE("square - triangle") {
        Polygon poly{Point(0, 0), Point(2, 0), Point(1, 2)};

        // Area = 0.5 * base * height = 0.5 * 2 * 2 = 2
        CHECK(std::abs(poly.square()) == doctest::Approx(2.0).epsilon(0.001));
    }

    TEST_CASE("perimeter - unit square") {
        Polygon poly{Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)};

        CHECK(poly.perimeter() == doctest::Approx(4.0).epsilon(0.001));
    }

    TEST_CASE("center - unit square") {
        Polygon poly{Point(0, 0), Point(2, 0), Point(2, 2), Point(0, 2)};

        Point c = poly.center();
        CHECK(c.x == doctest::Approx(1.0));
        CHECK(c.y == doctest::Approx(1.0));
    }

    TEST_CASE("centroid - unit square") {
        Polygon poly{Point(0, 0), Point(2, 0), Point(2, 2), Point(0, 2)};

        Point c = poly.centroid();
        CHECK(c.x == doctest::Approx(1.0).epsilon(0.01));
        CHECK(c.y == doctest::Approx(1.0).epsilon(0.01));
    }

    TEST_CASE("compactness - square vs circle") {
        Polygon square{Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)};

        double c = square.compactness();
        CHECK(c == doctest::Approx(0.785).epsilon(0.01));
    }
}

TEST_SUITE("Polygon vertex operations") {
    TEST_CASE("push Point value") {
        Polygon poly;
        poly.push(Point(1, 2));
        poly.push(Point(3, 4));

        CHECK(poly.size() == 2);
        CHECK(poly[0].x == 1);
        CHECK(poly[1].x == 3);
    }

    TEST_CASE("pushShared shares pointer") {
        Polygon poly;
        auto p = makePoint(5, 5);
        poly.pushShared(p);

        p->x = 100;
        CHECK(poly[0].x == 100);
    }

    TEST_CASE("unshift adds at beginning") {
        Polygon poly{Point(1, 0), Point(2, 0)};
        poly.unshift(Point(0, 0));

        CHECK(poly.size() == 3);
        CHECK(poly[0].x == 0);
        CHECK(poly[1].x == 1);
    }

    TEST_CASE("splice removes elements") {
        Polygon poly{Point(0, 0), Point(1, 0), Point(2, 0), Point(3, 0)};

        poly.splice(1, 2);

        CHECK(poly.size() == 2);
        CHECK(poly[0].x == 0);
        CHECK(poly[1].x == 3);
    }

    TEST_CASE("indexOf finds by value") {
        Polygon poly{Point(0, 0), Point(1, 0), Point(1, 1)};

        CHECK(poly.indexOf(Point(1, 0)) == 1);
        CHECK(poly.indexOf(Point(99, 99)) == -1);
    }

    TEST_CASE("indexOfPtr finds by pointer identity") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);

        Polygon poly({p1, p2, p3});

        CHECK(poly.indexOfPtr(p1) == 0);
        CHECK(poly.indexOfPtr(p2) == 1);
        CHECK(poly.indexOfPtr(p3) == 2);

        auto notInPoly = makePoint(0, 0);
        CHECK(poly.indexOfPtr(notInPoly) == -1);
    }

    TEST_CASE("last returns last vertex") {
        Polygon poly{Point(0, 0), Point(1, 0), Point(2, 0)};

        CHECK(poly.last().x == 2);
    }
}

TEST_SUITE("Polygon navigation") {
    TEST_CASE("next returns following vertex") {
        Polygon poly{Point(0, 0), Point(1, 0), Point(1, 1)};

        CHECK(poly.next(Point(0, 0)).x == 1);
        CHECK(poly.next(Point(1, 0)).x == 1);
        CHECK(poly.next(Point(1, 1)).x == 0); // Wraps around
    }

    TEST_CASE("prev returns preceding vertex") {
        Polygon poly{Point(0, 0), Point(1, 0), Point(1, 1)};

        CHECK(poly.prev(Point(1, 0)).x == 0);
        CHECK(poly.prev(Point(1, 1)).x == 1);
        CHECK(poly.prev(Point(0, 0)).y == 1); // Wraps around
    }

    TEST_CASE("vector returns edge vector") {
        Polygon poly{Point(0, 0), Point(3, 4)};

        Point v = poly.vector(Point(0, 0));
        CHECK(v.x == 3);
        CHECK(v.y == 4);
    }
}

TEST_SUITE("Polygon transformations") {
    TEST_CASE("offset moves all vertices") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(1, 1);

        Polygon poly({p1, p2, p3});
        poly.offset(10, 20);

        CHECK(p1->x == 10);
        CHECK(p1->y == 20);
        CHECK(p2->x == 11);
        CHECK(p2->y == 20);
    }

    TEST_CASE("rotate rotates all vertices") {
        auto p1 = makePoint(1, 0);
        auto p2 = makePoint(0, 0);
        auto p3 = makePoint(0, 1);

        Polygon poly({p1, p2, p3});
        poly.rotate(M_PI / 2); // 90 degrees

        // (1, 0) -> (0, 1)
        CHECK(p1->x == doctest::Approx(0.0).epsilon(0.001));
        CHECK(p1->y == doctest::Approx(1.0).epsilon(0.001));
    }
}

TEST_SUITE("Polygon convexity") {
    TEST_CASE("isConvex - convex square") {
        Polygon poly{Point(0, 0), Point(1, 0), Point(1, 1), Point(0, 1)};

        CHECK(poly.isConvex());
    }

    TEST_CASE("isConvex - concave L-shape") {
        Polygon poly{
            Point(0, 0), Point(2, 0), Point(2, 1),
            Point(1, 1), Point(1, 2), Point(0, 2)
        };

        CHECK_FALSE(poly.isConvex());
    }
}

TEST_SUITE("Polygon bounds") {
    TEST_CASE("getBounds returns bounding rectangle") {
        Polygon poly{Point(1, 2), Point(5, 3), Point(3, 8)};

        Rectangle bounds = poly.getBounds();

        CHECK(bounds.left == 1);
        CHECK(bounds.top == 2);
        CHECK(bounds.right == 5);
        CHECK(bounds.bottom == 8);
    }
}

TEST_SUITE("Polygon split") {
    TEST_CASE("split divides polygon") {
        Polygon square{Point(0, 0), Point(2, 0), Point(2, 2), Point(0, 2)};
        auto halves = square.split(Point(0, 0), Point(2, 2));

        CHECK(halves.size() == 2);
        CHECK(halves[0].size() == 3);
        CHECK(halves[1].size() == 3);
    }
}

TEST_SUITE("Polygon factory methods") {
    TEST_CASE("rect creates rectangle") {
        Polygon poly = Polygon::rect(4, 2);

        CHECK(poly.size() == 4);
        CHECK(std::abs(poly.square()) == doctest::Approx(8.0).epsilon(0.01));
    }

    TEST_CASE("regular creates regular polygon") {
        Polygon hex = Polygon::regular(6, 1.0);

        CHECK(hex.size() == 6);
    }

    TEST_CASE("circle creates 16-gon") {
        Polygon circ = Polygon::circle(1.0);

        CHECK(circ.size() == 16);
        CHECK(circ.compactness() > 0.95);
    }
}

TEST_SUITE("Polygon copy semantics") {
    TEST_CASE("Copy constructor shares points") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(0, 1);

        Polygon original({p1, p2, p3});
        Polygon copy(original);

        // Mutate through shared pointer
        p1->x = 100;

        // Copy sees the change
        CHECK(copy[0].x == 100);
    }

    TEST_CASE("deepCopy isolates points") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(1, 0);
        auto p3 = makePoint(0, 1);

        Polygon original({p1, p2, p3});
        Polygon deep = original.deepCopy();

        // Mutate original
        p1->x = 100;

        // Deep copy should NOT see the change
        CHECK(deep[0].x == 0);
    }

    TEST_CASE("Two polygons sharing vertex see mutations") {
        auto shared1 = makePoint(1, 0);
        auto shared2 = makePoint(1, 1);

        Polygon poly1({makePoint(0, 0), shared1, shared2, makePoint(0, 1)});
        Polygon poly2({shared1, makePoint(2, 0), makePoint(2, 1), shared2});

        // Get areas before mutation
        double area1Before = poly1.square();
        double area2Before = poly2.square();

        // Move the shared vertices
        shared1->x = 0.5;
        shared2->x = 0.5;

        // Areas should have changed
        double area1After = poly1.square();
        double area2After = poly2.square();

        CHECK(area1After != area1Before);
        CHECK(area2After != area2Before);
    }
}

TEST_SUITE("Polygon filter and min/max") {
    TEST_CASE("filter vertices") {
        Polygon poly{Point(0, 0), Point(5, 0), Point(10, 0)};
        Polygon filtered = poly.filter([](const Point& p) {
            return p.x >= 5;
        });

        CHECK(filtered.length() == 2);
    }

    TEST_CASE("min by function") {
        Polygon poly{Point(5, 0), Point(2, 0), Point(8, 0)};
        const Point& minX = poly.min([](const Point& p) { return p.x; });

        CHECK(minX.x == 2);
    }

    TEST_CASE("max by function") {
        Polygon poly{Point(5, 0), Point(2, 0), Point(8, 0)};
        const Point& maxX = poly.max([](const Point& p) { return p.x; });

        CHECK(maxX.x == 8);
    }
}
