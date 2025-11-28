cmake_minimum_required(VERSION 3.20)

find_package(unofficial-spirv-reflect CONFIG REQUIRED)

add_executable(shader_reflect shader_reflect.cpp)

target_link_libraries(shader_reflect PRIVATE
    unofficial::spirv-reflect
)

target_compile_features(shader_reflect PRIVATE cxx_std_17)
