cmake_minimum_required(VERSION 3.20)
project(vulkan-game VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

find_path(STB_INCLUDE_DIRS "stb_image.h")

set(SOURCES
    src/main.cpp
    src/Application.cpp
    src/Renderer.cpp
    src/Camera.cpp
    src/Mesh.cpp
    src/Texture.cpp
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
)

set(SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag.spv
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
    add_custom_target(shaders DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag.spv
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
