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

set(SOURCES
    src/main.cpp
    src/Application.cpp
    src/Renderer.cpp
    src/SceneBuilder.cpp
    src/Camera.cpp
    src/Mesh.cpp
    src/Texture.cpp
    src/ShaderLoader.cpp
    src/GrassSystem.cpp
    src/CelestialCalculator.cpp
    src/WindSystem.cpp
    src/PostProcessSystem.cpp
    src/FroxelSystem.cpp
    src/AtmosphereLUTSystem.cpp
    src/Player.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_update.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_integrate.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp.spv
)

set_source_files_properties(${RESOURCE_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/textures"
)

set_source_files_properties(${SHADER_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/shaders"
)

target_sources(${PROJECT_NAME} PRIVATE ${RESOURCE_FILES} ${SHADER_FILES})

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
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp
        COMMENT "Compiling transmittance LUT compute shader"
    )
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp.spv
        COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp
        COMMENT "Compiling multi-scatter LUT compute shader"
    )
    add_custom_target(shaders DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_shadow.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/postprocess.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_update.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/froxel_integrate.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/transmittance_lut.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/multiscatter_lut.comp.spv
    )
    add_dependencies(${PROJECT_NAME} shaders)
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
