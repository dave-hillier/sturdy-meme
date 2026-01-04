cmake_minimum_required(VERSION 3.16)
project(town_generator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(GEOM_SOURCES
    src/geom/Point.cpp
    src/geom/Segment.cpp
    src/geom/Circle.cpp
    src/geom/GeomUtils.cpp
    src/geom/Polygon.cpp
    src/geom/Graph.cpp
    src/geom/Spline.cpp
    src/geom/Voronoi.cpp
)

set(UTILS_SOURCES
    src/utils/Random.cpp
    src/utils/MathUtils.cpp
)

set(BUILDING_SOURCES
    src/building/Patch.cpp
    src/building/Cutter.cpp
    src/building/Topology.cpp
    src/building/CurtainWall.cpp
    src/building/Model.cpp
)

set(WARD_SOURCES
    src/wards/Ward.cpp
    src/wards/Castle.cpp
    src/wards/Cathedral.cpp
    src/wards/Market.cpp
    src/wards/CraftsmenWard.cpp
    src/wards/MerchantWard.cpp
    src/wards/PatriciateWard.cpp
    src/wards/CommonWard.cpp
    src/wards/AdministrationWard.cpp
    src/wards/MilitaryWard.cpp
    src/wards/GateWard.cpp
    src/wards/Slum.cpp
    src/wards/Farm.cpp
    src/wards/Park.cpp
)

set(SVG_SOURCES
    src/svg/SVGWriter.cpp
)

add_executable(town_generator
    src/main.cpp
    ${GEOM_SOURCES}
    ${UTILS_SOURCES}
    ${BUILDING_SOURCES}
    ${WARD_SOURCES}
    ${SVG_SOURCES}
)

target_include_directories(town_generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Installation
install(TARGETS town_generator DESTINATION bin)
