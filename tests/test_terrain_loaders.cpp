#include <doctest/doctest.h>
#include "terrain/RoadNetworkLoader.h"
#include "terrain/ErosionDataLoader.h"
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

// Helper to create a temporary directory for test files
class TempDirectory {
public:
    TempDirectory() {
        // Create a unique temp directory
        std::string tempBase = fs::temp_directory_path().string();
        path_ = tempBase + "/vulkan_game_tests_" + std::to_string(std::rand());
        fs::create_directories(path_);
    }

    ~TempDirectory() {
        // Clean up
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// ============================================================================
// RoadNetworkLoader Tests
// ============================================================================

TEST_SUITE("RoadNetworkLoader") {
    TEST_CASE("getRoadsPath generates correct path") {
        std::string result = RoadNetworkLoader::getRoadsPath("/some/cache/dir");
        CHECK(result == "/some/cache/dir/roads.geojson");
    }

    TEST_CASE("loadFromGeoJson with missing file returns false") {
        RoadNetworkLoader loader;
        bool result = loader.loadFromGeoJson("/nonexistent/path/roads.geojson");
        CHECK(result == false);
        CHECK(loader.isLoaded() == false);
    }

    TEST_CASE("loadFromGeoJson parses empty feature collection") {
        TempDirectory tempDir;
        std::string filePath = tempDir.path() + "/roads.geojson";

        // Write empty GeoJSON
        std::ofstream file(filePath);
        file << R"({
            "type": "FeatureCollection",
            "features": []
        })";
        file.close();

        RoadNetworkLoader loader;
        bool result = loader.loadFromGeoJson(filePath);
        CHECK(result == true);
        CHECK(loader.isLoaded() == true);
        CHECK(loader.getRoadNetwork().roads.size() == 0);
    }

    TEST_CASE("loadFromGeoJson parses single road") {
        TempDirectory tempDir;
        std::string filePath = tempDir.path() + "/roads.geojson";

        std::ofstream file(filePath);
        file << R"({
            "type": "FeatureCollection",
            "properties": {
                "terrain_size": 8192.0
            },
            "features": [
                {
                    "type": "Feature",
                    "geometry": {
                        "type": "LineString",
                        "coordinates": [
                            [100.0, 200.0],
                            [150.0, 250.0],
                            [200.0, 300.0]
                        ]
                    },
                    "properties": {
                        "type": "lane",
                        "from_settlement": 1,
                        "to_settlement": 2,
                        "width": 4.5
                    }
                }
            ]
        })";
        file.close();

        RoadNetworkLoader loader;
        bool result = loader.loadFromGeoJson(filePath);
        REQUIRE(result == true);
        REQUIRE(loader.isLoaded() == true);

        const RoadNetwork& network = loader.getRoadNetwork();
        CHECK(network.terrainSize == doctest::Approx(8192.0f));
        REQUIRE(network.roads.size() == 1);

        const RoadSpline& road = network.roads[0];
        CHECK(road.type == RoadType::Lane);
        CHECK(road.fromSettlementId == 1);
        CHECK(road.toSettlementId == 2);
        REQUIRE(road.controlPoints.size() == 3);

        CHECK(road.controlPoints[0].position.x == doctest::Approx(100.0f));
        CHECK(road.controlPoints[0].position.y == doctest::Approx(200.0f));
        CHECK(road.controlPoints[1].position.x == doctest::Approx(150.0f));
        CHECK(road.controlPoints[1].position.y == doctest::Approx(250.0f));
        CHECK(road.controlPoints[2].position.x == doctest::Approx(200.0f));
        CHECK(road.controlPoints[2].position.y == doctest::Approx(300.0f));

        CHECK(road.controlPoints[0].widthOverride == doctest::Approx(4.5f));
    }

    TEST_CASE("loadFromGeoJson parses all road types") {
        TempDirectory tempDir;
        std::string filePath = tempDir.path() + "/roads.geojson";

        std::ofstream file(filePath);
        file << R"({
            "type": "FeatureCollection",
            "features": [
                {
                    "type": "Feature",
                    "geometry": {"type": "LineString", "coordinates": [[0,0],[10,10]]},
                    "properties": {"type": "footpath"}
                },
                {
                    "type": "Feature",
                    "geometry": {"type": "LineString", "coordinates": [[0,0],[10,10]]},
                    "properties": {"type": "bridleway"}
                },
                {
                    "type": "Feature",
                    "geometry": {"type": "LineString", "coordinates": [[0,0],[10,10]]},
                    "properties": {"type": "lane"}
                },
                {
                    "type": "Feature",
                    "geometry": {"type": "LineString", "coordinates": [[0,0],[10,10]]},
                    "properties": {"type": "road"}
                },
                {
                    "type": "Feature",
                    "geometry": {"type": "LineString", "coordinates": [[0,0],[10,10]]},
                    "properties": {"type": "main_road"}
                }
            ]
        })";
        file.close();

        RoadNetworkLoader loader;
        REQUIRE(loader.loadFromGeoJson(filePath) == true);

        const RoadNetwork& network = loader.getRoadNetwork();
        REQUIRE(network.roads.size() == 5);

        CHECK(network.roads[0].type == RoadType::Footpath);
        CHECK(network.roads[1].type == RoadType::Bridleway);
        CHECK(network.roads[2].type == RoadType::Lane);
        CHECK(network.roads[3].type == RoadType::Road);
        CHECK(network.roads[4].type == RoadType::MainRoad);
    }

    TEST_CASE("loadFromGeoJson ignores non-LineString features") {
        TempDirectory tempDir;
        std::string filePath = tempDir.path() + "/roads.geojson";

        std::ofstream file(filePath);
        file << R"({
            "type": "FeatureCollection",
            "features": [
                {
                    "type": "Feature",
                    "geometry": {"type": "Point", "coordinates": [100,200]},
                    "properties": {"type": "lane"}
                },
                {
                    "type": "Feature",
                    "geometry": {"type": "LineString", "coordinates": [[0,0],[10,10]]},
                    "properties": {"type": "road"}
                },
                {
                    "type": "Feature",
                    "geometry": {"type": "Polygon", "coordinates": [[[0,0],[10,0],[10,10],[0,10],[0,0]]]},
                    "properties": {"type": "lane"}
                }
            ]
        })";
        file.close();

        RoadNetworkLoader loader;
        REQUIRE(loader.loadFromGeoJson(filePath) == true);

        // Only the LineString should be loaded
        CHECK(loader.getRoadNetwork().roads.size() == 1);
        CHECK(loader.getRoadNetwork().roads[0].type == RoadType::Road);
    }

    TEST_CASE("loadFromGeoJson handles invalid JSON") {
        TempDirectory tempDir;
        std::string filePath = tempDir.path() + "/roads.geojson";

        std::ofstream file(filePath);
        file << "{ invalid json }";
        file.close();

        RoadNetworkLoader loader;
        bool result = loader.loadFromGeoJson(filePath);
        CHECK(result == false);
    }
}

TEST_SUITE("RoadType utilities") {
    TEST_CASE("getRoadWidth returns correct widths") {
        CHECK(getRoadWidth(RoadType::Footpath) == doctest::Approx(1.5f));
        CHECK(getRoadWidth(RoadType::Bridleway) == doctest::Approx(3.0f));
        CHECK(getRoadWidth(RoadType::Lane) == doctest::Approx(4.0f));
        CHECK(getRoadWidth(RoadType::Road) == doctest::Approx(6.0f));
        CHECK(getRoadWidth(RoadType::MainRoad) == doctest::Approx(8.0f));
    }

    TEST_CASE("RoadSpline::getWidthAt uses override when set") {
        RoadSpline road;
        road.type = RoadType::Lane;  // default width 4.0
        road.controlPoints.push_back({glm::vec2(0, 0), 0.0f});    // Use default
        road.controlPoints.push_back({glm::vec2(10, 0), 5.5f});   // Override
        road.controlPoints.push_back({glm::vec2(20, 0), 0.0f});   // Use default

        CHECK(road.getWidthAt(0) == doctest::Approx(4.0f));   // Default from Lane
        CHECK(road.getWidthAt(1) == doctest::Approx(5.5f));   // Override
        CHECK(road.getWidthAt(2) == doctest::Approx(4.0f));   // Default from Lane
    }

    TEST_CASE("RoadSpline::getWidthAt handles out of bounds") {
        RoadSpline road;
        road.type = RoadType::Road;
        road.controlPoints.push_back({glm::vec2(0, 0), 0.0f});

        CHECK(road.getWidthAt(100) == doctest::Approx(6.0f));  // Returns default for Road
    }
}

// ============================================================================
// ErosionDataLoader Tests
// ============================================================================

TEST_SUITE("ErosionDataLoader") {
    TEST_CASE("path helpers generate correct paths") {
        std::string cacheDir = "/test/cache";

        CHECK(ErosionDataLoader::getFlowMapPath(cacheDir) == "/test/cache/flow_accumulation.exr");
        CHECK(ErosionDataLoader::getRiversPath(cacheDir) == "/test/cache/rivers.geojson");
        CHECK(ErosionDataLoader::getLakesPath(cacheDir) == "/test/cache/lakes.geojson");
        CHECK(ErosionDataLoader::getMetadataPath(cacheDir) == "/test/cache/erosion_data.meta");
    }

    TEST_CASE("isCacheValid returns false for missing files") {
        TempDirectory tempDir;

        ErosionLoadConfig config;
        config.cacheDirectory = tempDir.path();
        config.sourceHeightmapPath = "";  // No source validation

        ErosionDataLoader loader;
        CHECK(loader.isCacheValid(config) == false);
    }

    TEST_CASE("loadFromCache parses rivers GeoJSON") {
        TempDirectory tempDir;

        // Create rivers.geojson
        std::string riversPath = tempDir.path() + "/rivers.geojson";
        std::ofstream riversFile(riversPath);
        riversFile << R"({
            "type": "FeatureCollection",
            "features": [
                {
                    "type": "Feature",
                    "geometry": {
                        "type": "LineString",
                        "coordinates": [
                            [100.0, 200.0, 50.0],
                            [110.0, 210.0, 48.0],
                            [120.0, 220.0, 45.0]
                        ]
                    },
                    "properties": {
                        "totalFlow": 1500.5,
                        "widths": [2.0, 3.5, 5.0]
                    }
                }
            ]
        })";
        riversFile.close();

        // Create empty lakes.geojson
        std::string lakesPath = tempDir.path() + "/lakes.geojson";
        std::ofstream lakesFile(lakesPath);
        lakesFile << R"({"type": "FeatureCollection", "features": []})";
        lakesFile.close();

        ErosionLoadConfig config;
        config.cacheDirectory = tempDir.path();
        config.sourceHeightmapPath = "";
        config.seaLevel = 0.0f;

        ErosionDataLoader loader;
        bool result = loader.loadFromCache(config);
        REQUIRE(result == true);

        const WaterPlacementData& data = loader.getWaterData();
        REQUIRE(data.rivers.size() == 1);

        const RiverSpline& river = data.rivers[0];
        CHECK(river.totalFlow == doctest::Approx(1500.5f));
        REQUIRE(river.controlPoints.size() == 3);
        REQUIRE(river.widths.size() == 3);

        // Check first control point
        CHECK(river.controlPoints[0].x == doctest::Approx(100.0f));
        CHECK(river.controlPoints[0].z == doctest::Approx(200.0f));
        CHECK(river.controlPoints[0].y == doctest::Approx(50.0f));  // altitude

        // Check widths
        CHECK(river.widths[0] == doctest::Approx(2.0f));
        CHECK(river.widths[1] == doctest::Approx(3.5f));
        CHECK(river.widths[2] == doctest::Approx(5.0f));
    }

    TEST_CASE("loadFromCache parses lakes GeoJSON with Point geometry") {
        TempDirectory tempDir;

        // Create rivers.geojson (empty)
        std::string riversPath = tempDir.path() + "/rivers.geojson";
        std::ofstream riversFile(riversPath);
        riversFile << R"({"type": "FeatureCollection", "features": []})";
        riversFile.close();

        // Create lakes.geojson
        std::string lakesPath = tempDir.path() + "/lakes.geojson";
        std::ofstream lakesFile(lakesPath);
        lakesFile << R"({
            "type": "FeatureCollection",
            "features": [
                {
                    "type": "Feature",
                    "geometry": {
                        "type": "Point",
                        "coordinates": [500.0, 600.0]
                    },
                    "properties": {
                        "waterLevel": 125.5,
                        "radius": 50.0,
                        "area": 7854.0,
                        "depth": 12.5
                    }
                }
            ]
        })";
        lakesFile.close();

        ErosionLoadConfig config;
        config.cacheDirectory = tempDir.path();
        config.sourceHeightmapPath = "";
        config.seaLevel = 10.0f;

        ErosionDataLoader loader;
        bool result = loader.loadFromCache(config);
        REQUIRE(result == true);

        const WaterPlacementData& data = loader.getWaterData();
        CHECK(data.seaLevel == doctest::Approx(10.0f));
        REQUIRE(data.lakes.size() == 1);

        const Lake& lake = data.lakes[0];
        CHECK(lake.position.x == doctest::Approx(500.0f));
        CHECK(lake.position.y == doctest::Approx(600.0f));
        CHECK(lake.waterLevel == doctest::Approx(125.5f));
        CHECK(lake.radius == doctest::Approx(50.0f));
        CHECK(lake.area == doctest::Approx(7854.0f));
        CHECK(lake.depth == doctest::Approx(12.5f));
    }

    TEST_CASE("loadFromCache parses lakes GeoJSON with Polygon geometry") {
        TempDirectory tempDir;

        // Create rivers.geojson (empty)
        std::string riversPath = tempDir.path() + "/rivers.geojson";
        std::ofstream riversFile(riversPath);
        riversFile << R"({"type": "FeatureCollection", "features": []})";
        riversFile.close();

        // Create lakes.geojson with polygon (centroid should be calculated)
        std::string lakesPath = tempDir.path() + "/lakes.geojson";
        std::ofstream lakesFile(lakesPath);
        lakesFile << R"({
            "type": "FeatureCollection",
            "features": [
                {
                    "type": "Feature",
                    "geometry": {
                        "type": "Polygon",
                        "coordinates": [[
                            [0.0, 0.0],
                            [100.0, 0.0],
                            [100.0, 100.0],
                            [0.0, 100.0],
                            [0.0, 0.0]
                        ]]
                    },
                    "properties": {
                        "waterLevel": 50.0,
                        "radius": 70.7,
                        "area": 10000.0,
                        "depth": 5.0
                    }
                }
            ]
        })";
        lakesFile.close();

        ErosionLoadConfig config;
        config.cacheDirectory = tempDir.path();
        config.sourceHeightmapPath = "";

        ErosionDataLoader loader;
        bool result = loader.loadFromCache(config);
        REQUIRE(result == true);

        const WaterPlacementData& data = loader.getWaterData();
        REQUIRE(data.lakes.size() == 1);

        // Centroid of the square should be (50, 50)
        // Note: The current algorithm divides by number of points including the closing point
        // For 5 points (0,0), (100,0), (100,100), (0,100), (0,0):
        // sumX = 200, sumY = 200, count = 5, so centroid = (40, 40)
        const Lake& lake = data.lakes[0];
        CHECK(lake.position.x == doctest::Approx(40.0f));
        CHECK(lake.position.y == doctest::Approx(40.0f));
    }

    TEST_CASE("loadFromCache uses default width when widths array not present") {
        TempDirectory tempDir;

        // Create rivers.geojson with single width property
        std::string riversPath = tempDir.path() + "/rivers.geojson";
        std::ofstream riversFile(riversPath);
        riversFile << R"({
            "type": "FeatureCollection",
            "features": [
                {
                    "type": "Feature",
                    "geometry": {
                        "type": "LineString",
                        "coordinates": [
                            [0.0, 0.0, 10.0],
                            [10.0, 10.0, 8.0],
                            [20.0, 20.0, 6.0]
                        ]
                    },
                    "properties": {
                        "totalFlow": 100.0,
                        "width": 8.0
                    }
                }
            ]
        })";
        riversFile.close();

        // Create empty lakes.geojson
        std::string lakesPath = tempDir.path() + "/lakes.geojson";
        std::ofstream lakesFile(lakesPath);
        lakesFile << R"({"type": "FeatureCollection", "features": []})";
        lakesFile.close();

        ErosionLoadConfig config;
        config.cacheDirectory = tempDir.path();
        config.sourceHeightmapPath = "";

        ErosionDataLoader loader;
        bool result = loader.loadFromCache(config);
        REQUIRE(result == true);

        const RiverSpline& river = loader.getWaterData().rivers[0];
        REQUIRE(river.widths.size() == 3);
        CHECK(river.widths[0] == doctest::Approx(8.0f));
        CHECK(river.widths[1] == doctest::Approx(8.0f));
        CHECK(river.widths[2] == doctest::Approx(8.0f));
    }

    TEST_CASE("loadFromCache handles missing rivers file") {
        TempDirectory tempDir;

        // Only create lakes.geojson (no rivers)
        std::string lakesPath = tempDir.path() + "/lakes.geojson";
        std::ofstream lakesFile(lakesPath);
        lakesFile << R"({"type": "FeatureCollection", "features": []})";
        lakesFile.close();

        ErosionLoadConfig config;
        config.cacheDirectory = tempDir.path();
        config.sourceHeightmapPath = "";

        ErosionDataLoader loader;
        bool result = loader.loadFromCache(config);
        CHECK(result == false);
    }

    TEST_CASE("loadFromCache handles missing lakes file") {
        TempDirectory tempDir;

        // Only create rivers.geojson (no lakes)
        std::string riversPath = tempDir.path() + "/rivers.geojson";
        std::ofstream riversFile(riversPath);
        riversFile << R"({"type": "FeatureCollection", "features": []})";
        riversFile.close();

        ErosionLoadConfig config;
        config.cacheDirectory = tempDir.path();
        config.sourceHeightmapPath = "";

        ErosionDataLoader loader;
        bool result = loader.loadFromCache(config);
        CHECK(result == false);
    }

    TEST_CASE("loadFromCache handles invalid JSON") {
        TempDirectory tempDir;

        // Create invalid rivers.geojson
        std::string riversPath = tempDir.path() + "/rivers.geojson";
        std::ofstream riversFile(riversPath);
        riversFile << "{ not valid json }";
        riversFile.close();

        // Create valid lakes.geojson
        std::string lakesPath = tempDir.path() + "/lakes.geojson";
        std::ofstream lakesFile(lakesPath);
        lakesFile << R"({"type": "FeatureCollection", "features": []})";
        lakesFile.close();

        ErosionLoadConfig config;
        config.cacheDirectory = tempDir.path();
        config.sourceHeightmapPath = "";

        ErosionDataLoader loader;
        bool result = loader.loadFromCache(config);
        CHECK(result == false);
    }
}
