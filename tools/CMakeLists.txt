cmake_minimum_required(VERSION 3.20)

find_package(unofficial-spirv-reflect CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)
find_package(LibArchive REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tinyexr CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h")

# Shader reflection tool
add_executable(shader_reflect
    shader_reflect/shader_reflect.cpp
)

target_link_libraries(shader_reflect PRIVATE
    unofficial::spirv-reflect
)

target_compile_features(shader_reflect PRIVATE cxx_std_17)

# Terrain tile preprocessing tool
add_executable(terrain_preprocess
    terrain_preprocess/terrain_preprocess.cpp
    terrain_preprocess/TerrainImporter.cpp
    common/stb_impl.cpp
)

target_include_directories(terrain_preprocess PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/terrain_preprocess
    ${STB_INCLUDE_DIRS}
)

target_link_libraries(terrain_preprocess PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
)

target_compile_features(terrain_preprocess PRIVATE cxx_std_17)

# Biome map preprocessing tool
add_executable(biome_preprocess
    biome_preprocess/biome_preprocess.cpp
    biome_preprocess/BiomeGenerator.cpp
    biome_preprocess/WatershedMetrics.cpp
    biome_preprocess/SettlementSVG.cpp
    common/stb_impl.cpp
)

target_include_directories(biome_preprocess PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/biome_preprocess
    ${STB_INCLUDE_DIRS}
)

target_link_libraries(biome_preprocess PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
    unofficial::tinyexr::tinyexr
)

target_compile_features(biome_preprocess PRIVATE cxx_std_17)

# Settlement generator tool (standalone settlement placement)
add_executable(settlement_generator
    settlement_generator/main.cpp
    settlement_generator/SettlementGenerator.cpp
    settlement_generator/SettlementSVG.cpp
    common/stb_impl.cpp
)

target_include_directories(settlement_generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/settlement_generator
    ${STB_INCLUDE_DIRS}
)

target_link_libraries(settlement_generator PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
    nlohmann_json::nlohmann_json
    unofficial::tinyexr::tinyexr
)

target_compile_features(settlement_generator PRIVATE cxx_std_17)

# Foam noise texture generator
add_executable(foam_noise_gen
    foam_noise_gen/foam_noise_gen.cpp
)

target_link_libraries(foam_noise_gen PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
)

target_compile_features(foam_noise_gen PRIVATE cxx_std_17)

# Caustics texture generator
add_executable(caustics_gen
    caustics_gen/caustics_gen.cpp
)

target_link_libraries(caustics_gen PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
)

target_compile_features(caustics_gen PRIVATE cxx_std_17)

# SBSAR file renderer (Substance Archive)
add_executable(sbsar_render
    sbsar_render/sbsar_render.cpp
)

target_link_libraries(sbsar_render PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
    miniz::miniz
    LibArchive::LibArchive
)

target_compile_features(sbsar_render PRIVATE cxx_std_17)

# Material texture generator (procedural placeholder textures)
add_executable(material_texture_gen
    material_texture_gen/material_texture_gen.cpp
)

target_link_libraries(material_texture_gen PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
)

target_compile_features(material_texture_gen PRIVATE cxx_std_17)

# Tile generator library (for virtual texturing)
add_library(tile_generator_lib STATIC
    tile_generator/MaterialLibrary.cpp
    tile_generator/SplineRasterizer.cpp
    tile_generator/TileCompositor.cpp
)

target_include_directories(tile_generator_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/water
    ${CMAKE_CURRENT_SOURCE_DIR}/road_generator
    ${CMAKE_CURRENT_SOURCE_DIR}/biome_preprocess
)

target_link_libraries(tile_generator_lib PUBLIC
    SDL3::SDL3
    glm::glm
    lodepng
    nlohmann_json::nlohmann_json
)

target_compile_features(tile_generator_lib PRIVATE cxx_std_17)

# Virtual texture tile generator
add_executable(tile_generator
    tile_generator/main.cpp
)

target_link_libraries(tile_generator PRIVATE
    tile_generator_lib
)

target_compile_features(tile_generator PRIVATE cxx_std_17)

# Road network generator (A* pathfinding for settlement connections)
add_executable(road_generator
    road_generator/main.cpp
    road_generator/RoadPathfinder.cpp
    road_generator/SpaceColonization.cpp
    road_generator/RoadSVG.cpp
    road_generator/StreetGenerator.cpp
)

target_include_directories(road_generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/biome_preprocess
)

target_link_libraries(road_generator PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
    nlohmann_json::nlohmann_json
)

target_compile_features(road_generator PRIVATE cxx_std_17)

# Watershed analysis tool (D8 flow direction, river extraction)
add_executable(watershed
    watershed/src/main.cpp
    watershed/src/d8.cpp
    watershed/src/watershed.cpp
    watershed/src/png_io.cpp
    watershed/src/river_svg.cpp
    watershed/src/river_binary.cpp
)

target_include_directories(watershed PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/watershed/include
)

target_link_libraries(watershed PRIVATE
    SDL3::SDL3
    lodepng
    nlohmann_json::nlohmann_json
    unofficial::tinyexr::tinyexr
)

target_compile_features(watershed PRIVATE cxx_std_17)

# Vegetation placement generator (Poisson disk sampling for trees, rocks, detritus)
add_executable(vegetation_generator
    vegetation_generator/main.cpp
    vegetation_generator/VegetationPlacer.cpp
)

target_include_directories(vegetation_generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vegetation_generator
)

target_link_libraries(vegetation_generator PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
    nlohmann_json::nlohmann_json
)

target_compile_features(vegetation_generator PRIVATE cxx_std_17)

# Dwelling generator (procedural house floor plans)
add_executable(dwelling_generator
    dwelling_generator/main.cpp
    dwelling_generator/DwellingGrid.cpp
    dwelling_generator/DwellingPlan.cpp
    dwelling_generator/DwellingHouse.cpp
    dwelling_generator/DwellingSVG.cpp
)

target_include_directories(dwelling_generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dwelling_generator
)

target_link_libraries(dwelling_generator PRIVATE
    SDL3::SDL3
)

target_compile_features(dwelling_generator PRIVATE cxx_std_17)

# Town generator (Medieval Fantasy City Generator port)
add_executable(town_generator
    town_generator/src/main.cpp
    town_generator/src/geom/Point.cpp
    town_generator/src/geom/Segment.cpp
    town_generator/src/geom/Circle.cpp
    town_generator/src/geom/GeomUtils.cpp
    town_generator/src/geom/PolyBool.cpp
    town_generator/src/geom/Polygon.cpp
    town_generator/src/geom/Graph.cpp
    town_generator/src/geom/Spline.cpp
    town_generator/src/geom/Voronoi.cpp
    town_generator/src/geom/DCEL.cpp
    town_generator/src/utils/Random.cpp
    town_generator/src/utils/MathUtils.cpp
    town_generator/src/utils/Forester.cpp
    town_generator/src/utils/Bisector.cpp
    town_generator/src/building/Cell.cpp
    town_generator/src/building/Topology.cpp
    town_generator/src/building/CurtainWall.cpp
    town_generator/src/building/District.cpp
    town_generator/src/building/Canal.cpp
    town_generator/src/building/WardGroup.cpp
    town_generator/src/building/Block.cpp
    town_generator/src/building/Building.cpp
    town_generator/src/building/Landmark.cpp
    town_generator/src/building/City.cpp
    town_generator/src/wards/Ward.cpp
    town_generator/src/wards/Alleys.cpp
    town_generator/src/wards/Castle.cpp
    town_generator/src/wards/Cathedral.cpp
    town_generator/src/wards/Market.cpp
    town_generator/src/wards/Farm.cpp
    town_generator/src/wards/Park.cpp
    town_generator/src/wards/Harbour.cpp
    town_generator/src/wards/Wilderness.cpp
    town_generator/src/svg/SVGWriter.cpp
)

target_include_directories(town_generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/town_generator/include
)

target_link_libraries(town_generator PRIVATE
    SDL3::SDL3
)

target_compile_features(town_generator PRIVATE cxx_std_17)

# Town generator tests
find_package(doctest CONFIG REQUIRED)
add_executable(town_generator_tests
    town_generator/tests/test_main.cpp
    town_generator/tests/test_point.cpp
    town_generator/tests/test_polygon.cpp
    town_generator/tests/test_segment.cpp
    town_generator/tests/test_geometry_utils.cpp
    town_generator/tests/test_graph.cpp
    town_generator/src/geom/Point.cpp
    town_generator/src/geom/Segment.cpp
    town_generator/src/geom/Circle.cpp
    town_generator/src/geom/GeomUtils.cpp
    town_generator/src/geom/Polygon.cpp
    town_generator/src/geom/Graph.cpp
    town_generator/src/geom/Spline.cpp
    town_generator/src/geom/Voronoi.cpp
    town_generator/src/utils/Random.cpp
    town_generator/src/utils/MathUtils.cpp
)

target_include_directories(town_generator_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/town_generator/include
)

target_link_libraries(town_generator_tests PRIVATE
    SDL3::SDL3
    doctest::doctest
)

target_compile_features(town_generator_tests PRIVATE cxx_std_17)

# Terrain-aware patch generator (preview tool for town placement)
add_executable(terrain_patch_generator
    terrain_patch_generator/main.cpp
)

target_include_directories(terrain_patch_generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/terrain_patch_generator
)

target_link_libraries(terrain_patch_generator PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
    nlohmann_json::nlohmann_json
)

target_compile_features(terrain_patch_generator PRIVATE cxx_std_17)

# Test data generator for terrain_patch_generator
add_executable(terrain_patch_test_data
    terrain_patch_generator/generate_test_data.cpp
)

target_link_libraries(terrain_patch_test_data PRIVATE
    SDL3::SDL3
    lodepng
    nlohmann_json::nlohmann_json
)

target_compile_features(terrain_patch_test_data PRIVATE cxx_std_17)

# Skinned mesh LOD generator (mesh simplification for skinned characters)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)

add_executable(skinned_mesh_lod
    skinned_mesh_lod/main.cpp
    skinned_mesh_lod/MeshSimplifier.cpp
)

target_include_directories(skinned_mesh_lod PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/skinned_mesh_lod
)

target_link_libraries(skinned_mesh_lod PRIVATE
    SDL3::SDL3
    glm::glm
    meshoptimizer::meshoptimizer
    fastgltf::fastgltf
    nlohmann_json::nlohmann_json
)

target_compile_features(skinned_mesh_lod PRIVATE cxx_std_17)
