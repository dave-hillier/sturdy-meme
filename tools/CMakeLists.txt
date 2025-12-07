cmake_minimum_required(VERSION 3.20)

find_package(unofficial-spirv-reflect CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(miniz CONFIG REQUIRED)
find_package(LibArchive REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
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

# Erosion and water placement preprocessing tool
add_executable(erosion_preprocess
    erosion_preprocess/erosion_preprocess.cpp
    erosion_preprocess/ErosionSimulator.cpp
    common/stb_impl.cpp
)

target_include_directories(erosion_preprocess PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/erosion_preprocess
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${STB_INCLUDE_DIRS}
)

target_link_libraries(erosion_preprocess PRIVATE
    SDL3::SDL3
    glm::glm
)

target_compile_features(erosion_preprocess PRIVATE cxx_std_17)

# Biome map preprocessing tool
add_executable(biome_preprocess
    biome_preprocess/biome_preprocess.cpp
    biome_preprocess/BiomeGenerator.cpp
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
)

target_compile_features(biome_preprocess PRIVATE cxx_std_17)

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

# Cape texture generator
add_executable(cape_texture_gen
    cape_texture_gen/cape_texture_gen.cpp
)

target_link_libraries(cape_texture_gen PRIVATE
    SDL3::SDL3
    glm::glm
    lodepng
)

target_compile_features(cape_texture_gen PRIVATE cxx_std_17)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/road_generator
    ${CMAKE_CURRENT_SOURCE_DIR}/biome_preprocess
    ${CMAKE_CURRENT_SOURCE_DIR}/erosion_preprocess
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
