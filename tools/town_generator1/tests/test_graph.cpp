#include <doctest/doctest.h>
#include "town_generator/geom/Graph.h"
#include <cmath>

using namespace town_generator::geom;

TEST_SUITE("Graph construction") {
    TEST_CASE("Empty graph") {
        Graph graph;
        CHECK(graph.nodes.empty());
    }

    TEST_CASE("Add nodes") {
        Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();

        CHECK(graph.nodes.size() == 2);
        CHECK(n1 != n2);
    }

    TEST_CASE("Add existing node") {
        Graph graph;

        auto existing = new Node();
        auto added = graph.add(existing);

        CHECK(added == existing);
        CHECK(graph.nodes.size() == 1);
    }
}

TEST_SUITE("Node links") {
    TEST_CASE("Link nodes bidirectionally") {
        Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();

        n1->link(n2, 5.0);

        CHECK(n1->links.count(n2) == 1);
        CHECK(n1->links[n2] == 5.0);
        CHECK(n2->links.count(n1) == 1);
        CHECK(n2->links[n1] == 5.0);
    }

    TEST_CASE("Link nodes unidirectionally") {
        Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();

        n1->link(n2, 3.0, false);

        CHECK(n1->links.count(n2) == 1);
        CHECK(n2->links.count(n1) == 0);
    }

    TEST_CASE("Unlink nodes") {
        Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();

        n1->link(n2, 1.0);
        n1->unlink(n2);

        CHECK(n1->links.empty());
        CHECK(n2->links.empty());
    }

    TEST_CASE("Unlink all") {
        Graph graph;

        auto center = graph.add();
        auto n1 = graph.add();
        auto n2 = graph.add();
        auto n3 = graph.add();

        center->link(n1, 1.0);
        center->link(n2, 2.0);
        center->link(n3, 3.0);

        center->unlinkAll();

        CHECK(center->links.empty());
        CHECK(n1->links.empty());
        CHECK(n2->links.empty());
        CHECK(n3->links.empty());
    }
}

TEST_SUITE("Graph remove") {
    TEST_CASE("Remove node unlinks") {
        Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();
        auto n3 = graph.add();

        n1->link(n2, 1.0);
        n2->link(n3, 1.0);

        graph.remove(n2);

        CHECK(graph.nodes.size() == 2);
        CHECK(n1->links.empty());
        CHECK(n3->links.empty());
    }
}

TEST_SUITE("Graph A* pathfinding") {
    TEST_CASE("Direct path between two nodes") {
        Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();

        n1->link(n2, 1.0);

        auto path = graph.aStar(n1, n2);

        CHECK(path.size() == 2);
        CHECK(path[0] == n2);
        CHECK(path[1] == n1);
    }

    TEST_CASE("Path through intermediate node") {
        Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();
        auto n3 = graph.add();

        n1->link(n2, 1.0);
        n2->link(n3, 1.0);

        auto path = graph.aStar(n1, n3);

        CHECK(path.size() == 3);
    }

    TEST_CASE("No path exists") {
        Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();
        // No links

        auto path = graph.aStar(n1, n2);

        CHECK(path.empty());
    }

    TEST_CASE("Path to self") {
        Graph graph;

        auto n1 = graph.add();

        auto path = graph.aStar(n1, n1);

        CHECK(path.size() == 1);
        CHECK(path[0] == n1);
    }

    TEST_CASE("Exclude nodes from path") {
        Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();
        auto n3 = graph.add();
        auto n4 = graph.add();

        // Two paths: n1 -> n2 -> n4 (cost 2) and n1 -> n3 -> n4 (cost 10)
        n1->link(n2, 1.0);
        n2->link(n4, 1.0);

        n1->link(n3, 5.0);
        n3->link(n4, 5.0);

        std::vector<Node*> exclude = {n2};
        auto path = graph.aStar(n1, n4, &exclude);

        // Should find path through n3
        CHECK(path.size() == 3);
        CHECK(path[0] == n4);
        CHECK(path[1] == n3);
        CHECK(path[2] == n1);
    }
}

TEST_SUITE("Graph calculatePrice") {
    TEST_CASE("Price of empty path") {
        Graph graph;
        std::vector<Node*> path;

        double price = graph.calculatePrice(path);

        CHECK(price == 0.0);
    }

    TEST_CASE("Price of single node path") {
        Graph graph;
        auto n1 = graph.add();
        std::vector<Node*> path = {n1};

        double price = graph.calculatePrice(path);

        CHECK(price == 0.0);
    }

    TEST_CASE("Price of two node path") {
        Graph graph;
        auto n1 = graph.add();
        auto n2 = graph.add();

        n1->link(n2, 5.0);

        std::vector<Node*> path = {n1, n2};

        double price = graph.calculatePrice(path);

        CHECK(price == 5.0);
    }

    TEST_CASE("Price of multi-node path") {
        Graph graph;
        auto n1 = graph.add();
        auto n2 = graph.add();
        auto n3 = graph.add();

        n1->link(n2, 3.0);
        n2->link(n3, 7.0);

        std::vector<Node*> path = {n1, n2, n3};

        double price = graph.calculatePrice(path);

        CHECK(price == 10.0); // 3 + 7
    }

    TEST_CASE("Price of invalid path (no link)") {
        Graph graph;
        auto n1 = graph.add();
        auto n2 = graph.add();
        // No links

        std::vector<Node*> path = {n1, n2};

        double price = graph.calculatePrice(path);

        CHECK(std::isnan(price));
    }
}

TEST_SUITE("Graph complex scenarios") {
    TEST_CASE("Diamond pattern") {
        //     n1
        //    /  \
        //   n2  n3
        //    \  /
        //     n4
        Graph graph;

        auto n1 = graph.add();
        auto n2 = graph.add();
        auto n3 = graph.add();
        auto n4 = graph.add();

        n1->link(n2, 1.0);
        n1->link(n3, 2.0);
        n2->link(n4, 3.0);
        n3->link(n4, 1.0);

        // Path from n1 to n4
        auto path = graph.aStar(n1, n4);

        CHECK(!path.empty());
        CHECK(path.front() == n4);
        CHECK(path.back() == n1);

        // Should take n1 -> n3 -> n4 (cost 3) over n1 -> n2 -> n4 (cost 4)
        double cost = graph.calculatePrice(path);
        CHECK(cost == 3.0);
    }

    TEST_CASE("Disconnected subgraphs") {
        Graph graph;

        // Subgraph 1
        auto a1 = graph.add();
        auto a2 = graph.add();
        a1->link(a2, 1.0);

        // Subgraph 2
        auto b1 = graph.add();
        auto b2 = graph.add();
        b1->link(b2, 1.0);

        // No path between subgraphs
        auto path = graph.aStar(a1, b1);
        CHECK(path.empty());
    }

    TEST_CASE("Linear chain") {
        Graph graph;

        std::vector<Node*> chain;
        for (int i = 0; i < 5; ++i) {
            chain.push_back(graph.add());
        }

        for (size_t i = 0; i + 1 < chain.size(); ++i) {
            chain[i]->link(chain[i + 1], 1.0);
        }

        auto path = graph.aStar(chain.front(), chain.back());

        CHECK(path.size() == 5);
        CHECK(graph.calculatePrice(path) == 4.0);
    }
}

TEST_SUITE("Node operations") {
    TEST_CASE("Node default construction") {
        Node node;
        CHECK(node.links.empty());
    }

    TEST_CASE("Multiple links from one node") {
        Graph graph;
        auto center = graph.add();
        auto n1 = graph.add();
        auto n2 = graph.add();
        auto n3 = graph.add();

        center->link(n1, 1.0);
        center->link(n2, 2.0);
        center->link(n3, 3.0);

        CHECK(center->links.size() == 3);
        CHECK(center->links[n1] == 1.0);
        CHECK(center->links[n2] == 2.0);
        CHECK(center->links[n3] == 3.0);
    }

    TEST_CASE("Update link cost") {
        Graph graph;
        auto n1 = graph.add();
        auto n2 = graph.add();

        n1->link(n2, 5.0);
        CHECK(n1->links[n2] == 5.0);

        n1->links[n2] = 10.0;
        CHECK(n1->links[n2] == 10.0);
    }
}
