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
find_package(imgui CONFIG REQUIRED)

find_path(STB_INCLUDE_DIRS "stb_image.h")

# Add tools subdirectory for shader reflection
add_subdirectory(tools)

set(SOURCES
    src/main.cpp
    src/Application.cpp
    src/Renderer.cpp
    src/VulkanContext.cpp
    src/SceneBuilder.cpp
    src/SceneManager.cpp
    src/Camera.cpp
    src/Mesh.cpp
    src/BufferUtils.cpp
    src/Texture.cpp
    src/ShaderLoader.cpp
    src/PipelineBuilder.cpp
    src/GraphicsPipelineFactory.cpp
    src/BindingBuilder.cpp
    src/DescriptorManager.cpp
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
    src/TerrainHeightMap.cpp
    src/TerrainTextures.cpp
    src/TerrainCBT.cpp
    src/ClothSimulation.cpp
    src/SnowMaskSystem.cpp
    src/VolumetricSnowSystem.cpp
    src/SkySystem.cpp
    src/ShadowSystem.cpp
    src/GuiSystem.cpp
    src/InputSystem.cpp
    src/CatmullClarkCBT.cpp
    src/CatmullClarkMesh.cpp
    src/CatmullClarkSystem.cpp
    src/RockSystem.cpp
    src/CloudShadowSystem.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
    ${STB_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3
    glm::glm
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    Jolt::Jolt
    imgui::imgui
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "VulkanGame"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.vulkangame"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)

# Copy entire textures directory to app bundle
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/textures
        $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/textures
    COMMENT "Copying textures to app bundle"
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
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/irradiance_lut.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud_shadow.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_build.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_reduce.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/snow_accumulation.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_snow.comp.spv
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

set(CATMULLCLARK_SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_subdivision.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.frag.spv
)

set_source_files_properties(${SHADER_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/shaders"
)

set_source_files_properties(${TERRAIN_SHADER_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/shaders/terrain"
)

set_source_files_properties(${CATMULLCLARK_SHADER_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/shaders"
)

target_sources(${PROJECT_NAME} PRIVATE ${SHADER_FILES} ${TERRAIN_SHADER_FILES} ${CATMULLCLARK_SHADER_FILES})

find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/bin)

if(GLSLC)
    set(SHADER_COMPILE_CMD ${GLSLC} -o)
elseif(GLSLANG_VALIDATOR)
    set(SHADER_COMPILE_CMD ${GLSLANG_VALIDATOR} -V -o)
endif()

if(SHADER_COMPILE_CMD)
    # Simple shaders using SHADER_COMPILE_CMD
    set(SIMPLE_SHADERS
        shader.vert shader.frag
        sky.vert sky.frag
        grass.comp grass_displacement.comp grass.vert grass.frag
        shadow.vert shadow.frag
        grass_shadow.vert grass_shadow.frag
        postprocess.vert postprocess.frag
        bloom_downsample.frag bloom_upsample.frag bloom_composite.frag
        froxel_update.comp froxel_integrate.comp
        weather.comp weather.vert weather.frag
        leaf.comp leaf.vert leaf.frag
        histogram_build.comp histogram_reduce.comp
        snow_accumulation.comp
    )

    foreach(SHADER ${SIMPLE_SHADERS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv
            COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            COMMENT "Compiling shader: ${SHADER}"
        )
    endforeach()

    # Shaders requiring glslangValidator with specific flags
    set(ATMOSPHERE_SHADERS
        transmittance_lut.comp
        multiscatter_lut.comp
        skyview_lut.comp
        irradiance_lut.comp
    )

    foreach(SHADER ${ATMOSPHERE_SHADERS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv
            COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/atmosphere_common.glsl
            COMMENT "Compiling atmosphere shader: ${SHADER}"
        )
    endforeach()

    # Special compute shaders with includes
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_snow.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_snow.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_snow.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_snow.comp
        COMMENT "Compiling volumetric snow compute shader"
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp
        COMMENT "Compiling cloud map LUT compute shader"
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud_shadow.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud_shadow.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud_shadow.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud_shadow.comp
        COMMENT "Compiling cloud shadow compute shader"
    )
    # Terrain shaders (LEB terrain system)
    set(TERRAIN_CBT_DEPS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/cbt.glsl
    )
    set(TERRAIN_LEB_DEPS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/cbt.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/leb.glsl
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp ${TERRAIN_CBT_DEPS}
        COMMENT "Compiling terrain dispatcher compute shader"
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp ${TERRAIN_LEB_DEPS}
        COMMENT "Compiling terrain subdivision compute shader"
    )

    set(TERRAIN_REDUCTION_SHADERS
        terrain_sum_reduction_prepass.comp
        terrain_sum_reduction.comp
    )

    foreach(SHADER ${TERRAIN_REDUCTION_SHADERS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}.spv
            COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER} ${TERRAIN_CBT_DEPS}
            COMMENT "Compiling terrain shader: ${SHADER}"
        )
    endforeach()

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert ${TERRAIN_LEB_DEPS}
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
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.vert ${TERRAIN_LEB_DEPS}
        COMMENT "Compiling terrain shadow vertex shader"
    )

    set(TERRAIN_SIMPLE_SHADERS
        terrain_shadow.frag
        terrain_wireframe.frag
    )

    foreach(SHADER ${TERRAIN_SIMPLE_SHADERS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}.spv
            COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}
            COMMENT "Compiling terrain shader: ${SHADER}"
        )
    endforeach()

    # Catmull-Clark subdivision shaders
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_subdivision.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_subdivision.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_subdivision.comp.glsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_subdivision.comp.glsl
        COMMENT "Compiling Catmull-Clark subdivision compute shader"
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.vert.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.vert.glsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.vert.glsl
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cbt_common.glsl
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_mesh.glsl
                ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_tessellation.glsl
        COMMENT "Compiling Catmull-Clark vertex shader"
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.frag.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.frag.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.frag.glsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.frag.glsl
        COMMENT "Compiling Catmull-Clark fragment shader"
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
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/irradiance_lut.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloudmap_lut.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cloud_shadow.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_build.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/histogram_reduce.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/snow_accumulation.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_snow.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_wireframe.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_subdivision.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.frag.spv
    )

    # Generate UBO headers from SPIR-V reflection
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/UBOs.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/generated
        COMMAND shader_reflect
            ${CMAKE_CURRENT_SOURCE_DIR}/generated/UBOs.h
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
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/UBOs.h
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
