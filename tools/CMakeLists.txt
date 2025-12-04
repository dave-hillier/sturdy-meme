cmake_minimum_required(VERSION 3.20)

find_package(unofficial-spirv-reflect CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h")

add_executable(shader_reflect shader_reflect.cpp)

target_link_libraries(shader_reflect PRIVATE
    unofficial::spirv-reflect
)

target_compile_features(shader_reflect PRIVATE cxx_std_17)

# Terrain tile preprocessing tool
add_executable(terrain_preprocess
    terrain_preprocess.cpp
    stb_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/TerrainImporter.cpp
)

target_include_directories(terrain_preprocess PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${STB_INCLUDE_DIRS}
)

target_link_libraries(terrain_preprocess PRIVATE
    SDL3::SDL3
    glm::glm
)

target_compile_features(terrain_preprocess PRIVATE cxx_std_17)

# Erosion and water placement preprocessing tool
add_executable(erosion_preprocess
    erosion_preprocess.cpp
    stb_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ErosionSimulator.cpp
)

target_include_directories(erosion_preprocess PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${STB_INCLUDE_DIRS}
)

target_link_libraries(erosion_preprocess PRIVATE
    SDL3::SDL3
    glm::glm
)

target_compile_features(erosion_preprocess PRIVATE cxx_std_17)
