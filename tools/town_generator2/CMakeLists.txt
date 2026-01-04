cmake_minimum_required(VERSION 3.20)
project(town_generator2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(town_generator2
    src/main.cpp
    src/Model.cpp
    src/CurtainWall.cpp
    src/Topology.cpp
    src/Ward.cpp
    src/Castle.cpp
)

target_include_directories(town_generator2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Enable warnings
if(MSVC)
    target_compile_options(town_generator2 PRIVATE /W4)
else()
    target_compile_options(town_generator2 PRIVATE -Wall -Wextra -Wpedantic)
endif()
