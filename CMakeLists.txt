cmake_minimum_required(VERSION 3.20)
project(vulkan-game VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)

find_path(STB_INCLUDE_DIRS "stb_image.h")

# Add tools subdirectory for shader reflection
add_subdirectory(tools)

set(SOURCES
    src/main.cpp
    src/Application.cpp
    src/Renderer.cpp
    src/SceneBuilder.cpp
    src/SceneManager.cpp
    src/Camera.cpp
    src/Mesh.cpp
    src/BufferUtils.cpp
    src/Texture.cpp
    src/ShaderLoader.cpp
    src/PipelineBuilder.cpp
    src/BindingBuilder.cpp
    src/RenderableBuilder.cpp
    src/GrassSystem.cpp
    src/CelestialCalculator.cpp
    src/WindSystem.cpp
    src/WeatherSystem.cpp
    src/LeafSystem.cpp
    src/PostProcessSystem.cpp
    src/BloomSystem.cpp
    src/FroxelSystem.cpp
    src/AtmosphereLUTSystem.cpp
    src/Player.cpp
    src/PhysicsSystem.cpp
    src/ParticleSystem.cpp
    src/TerrainSystem.cpp
    src/ClothSimulation.cpp
    src/SnowMaskSystem.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${STB_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
    glm::glm
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    Jolt::Jolt
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "VulkanGame"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.vulkangame"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)

set(RESOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/crate.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/grass.png
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/metal.png
)

set(SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_displacement.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_downsample.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_upsample.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_composite.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_update.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_integrate.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skyview_lut.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_build.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_reduce.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/snow_accumulation.comp.spv
)

set(TERRAIN_SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_wireframe.frag.spv
)

set_source_files_properties(${RESOURCE_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/textures"
)

set_source_files_properties(${SHADER_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/shaders"
)

set_source_files_properties(${TERRAIN_SHADER_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/shaders/terrain"
)

target_sources(${PROJECT_NAME} PRIVATE ${RESOURCE_FILES} ${SHADER_FILES} ${TERRAIN_SHADER_FILES})

find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/bin)

if(GLSLC)
    set(SHADER_COMPILE_CMD ${GLSLC} -o)
elseif(GLSLANG_VALIDATOR)
    set(SHADER_COMPILE_CMD ${GLSLANG_VALIDATOR} -V -o)
endif()

if(SHADER_COMPILE_CMD)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert
        COMMENT "Compiling vertex shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag
        COMMENT "Compiling fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert
        COMMENT "Compiling sky vertex shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag
        COMMENT "Compiling sky fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.comp
        COMMENT "Compiling grass compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_displacement.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_displacement.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_displacement.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_displacement.comp
        COMMENT "Compiling grass displacement compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.vert.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.vert
        COMMENT "Compiling grass vertex shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.frag
        COMMENT "Compiling grass fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert
        COMMENT "Compiling shadow vertex shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag
        COMMENT "Compiling shadow fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.vert.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.vert
        COMMENT "Compiling grass shadow vertex shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.frag
        COMMENT "Compiling grass shadow fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.vert.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.vert
        COMMENT "Compiling postprocess vertex shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag
        COMMENT "Compiling postprocess fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_downsample.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_downsample.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_downsample.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_downsample.frag
        COMMENT "Compiling bloom downsample fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_upsample.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_upsample.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_upsample.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_upsample.frag
        COMMENT "Compiling bloom upsample fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_composite.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_composite.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_composite.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_composite.frag
        COMMENT "Compiling bloom composite fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_update.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_update.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_update.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_update.comp
        COMMENT "Compiling froxel update compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_integrate.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_integrate.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_integrate.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_integrate.comp
        COMMENT "Compiling froxel integration compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.comp
        COMMENT "Compiling weather compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.vert.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.vert
        COMMENT "Compiling weather vertex shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.frag
        COMMENT "Compiling weather fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.comp
        COMMENT "Compiling leaf compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.vert.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.vert
        COMMENT "Compiling leaf vertex shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.frag.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.frag
        COMMENT "Compiling leaf fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_build.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_build.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_build.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_build.comp
        COMMENT "Compiling histogram build compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_reduce.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_reduce.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_reduce.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_reduce.comp
        COMMENT "Compiling histogram reduce compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/snow_accumulation.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/snow_accumulation.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/snow_accumulation.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/snow_accumulation.comp
        COMMENT "Compiling snow accumulation compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp ${CMAKE_CURRENT_SOURCE_DIR}/shaders/atmosphere_common.glsl
        COMMENT "Compiling transmittance LUT compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp ${CMAKE_CURRENT_SOURCE_DIR}/shaders/atmosphere_common.glsl
        COMMENT "Compiling multi-scatter LUT compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skyview_lut.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skyview_lut.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skyview_lut.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skyview_lut.comp ${CMAKE_CURRENT_SOURCE_DIR}/shaders/atmosphere_common.glsl
        COMMENT "Compiling sky-view LUT compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp
        COMMENT "Compiling cloud map LUT compute shader"
    )
    # Terrain shaders (LEB terrain system)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/cbt.glsl
        COMMENT "Compiling terrain dispatcher compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/cbt.glsl
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/leb.glsl
        COMMENT "Compiling terrain subdivision compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass.comp
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/cbt.glsl
        COMMENT "Compiling terrain sum reduction prepass compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction.comp
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/cbt.glsl
        COMMENT "Compiling terrain sum reduction compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/cbt.glsl
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/leb.glsl
        COMMENT "Compiling terrain vertex shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.frag.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.frag
        COMMENT "Compiling terrain fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.vert.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.vert
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/cbt.glsl
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/leb.glsl
        COMMENT "Compiling terrain shadow vertex shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.frag.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.frag
        COMMENT "Compiling terrain shadow fragment shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_wireframe.frag.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_wireframe.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_wireframe.frag
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_wireframe.frag
        COMMENT "Compiling terrain wireframe fragment shader"
    )
    add_custom_target(shaders DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_displacement.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_downsample.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_upsample.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_composite.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_update.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_integrate.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skyview_lut.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_build.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_reduce.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/snow_accumulation.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_wireframe.frag.spv
    )

    # Generate UBO headers from SPIR-V reflection
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/GeneratedUBOs.h
        COMMAND shader_reflect
            ${CMAKE_CURRENT_SOURCE_DIR}/src/GeneratedUBOs.h
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.vert.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.comp.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_update.comp.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/weather.comp.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/leaf.comp.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.frag.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_build.comp.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_reduce.comp.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp.spv
        DEPENDS shaders shader_reflect
        COMMENT "Generating UBO headers from SPIR-V reflection"
    )

    add_custom_target(generated_headers DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/GeneratedUBOs.h
    )

    add_dependencies(${PROJECT_NAME} shaders generated_headers)
else()
    message(WARNING "glslc/glslangValidator not found. Shaders must be compiled manually.")
endif()

if(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_METAL_EXT)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        "-framework QuartzCore"
        "-framework Metal"
        "-framework Foundation"
        "-framework IOSurface"
        "-framework CoreGraphics"
        "-framework IOKit"
        "-framework AppKit"
    )
endif()
