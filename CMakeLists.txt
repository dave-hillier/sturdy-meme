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
find_package(fastgltf CONFIG REQUIRED)
find_package(unofficial-openfbx CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

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
    src/MaterialDescriptorFactory.cpp
    src/MaterialRegistry.cpp
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
    src/TerrainMeshlet.cpp
    src/TerrainImporter.cpp
    src/ErosionSimulator.cpp
    src/ClothSimulation.cpp
    src/PlayerCape.cpp
    src/SnowMaskSystem.cpp
    src/VolumetricSnowSystem.cpp
    src/SkySystem.cpp
    src/ShadowSystem.cpp
    src/GuiSystem.cpp
    src/InputSystem.cpp
    src/CatmullClarkCBT.cpp
    src/CatmullClarkMesh.cpp
    src/CatmullClarkSystem.cpp
    src/OBJLoader.cpp
    src/RockSystem.cpp
    src/CloudShadowSystem.cpp
    src/GLTFLoader.cpp
    src/SkinnedMesh.cpp
    src/Animation.cpp
    src/AnimatedCharacter.cpp
    src/AnimationStateMachine.cpp
    src/AnimationEventLoader.cpp
    src/AnimationBlend.cpp
    src/AnimationLayer.cpp
    src/AnimationLayerController.cpp
    src/BlendSpace.cpp
    src/BoneMask.cpp
    src/IKSolver.cpp
    src/FBXLoader.cpp
    src/FBXPostProcess.cpp
    src/HiZSystem.cpp
    src/GpuProfiler.cpp
    src/CpuProfiler.cpp
    src/WaterSystem.cpp
    src/FlowMapGenerator.cpp
    src/WaterGBuffer.cpp
    src/WaterDisplacement.cpp
    src/FoamBuffer.cpp
    src/SSRSystem.cpp
    src/WaterTileCull.cpp
    src/TreeGenerator.cpp
    src/TreeEditSystem.cpp
    src/TreeEditorGui.cpp
    src/BillboardCapture.cpp
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
    fastgltf::fastgltf
    unoffical::openfbx::openfbx
    lodepng
    nlohmann_json::nlohmann_json
)

# Platform-specific setup
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "VulkanGame"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.vulkangame"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )

    # Copy textures directory to app bundle (from root textures/)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/textures
            $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/textures
        COMMENT "Copying root textures to app bundle"
    )

    # Copy assets/textures to Resources/textures (for terrain textures)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures
            $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/textures
        COMMENT "Copying assets/textures to app bundle"
    )

    # Copy assets directory to app bundle
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/assets
        COMMENT "Copying assets to app bundle"
    )
else()
    # Copy textures directory to build output (Linux/Windows)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/textures
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/textures
        COMMENT "Copying textures to build directory"
    )

    # Copy assets directory to build output (Linux/Windows)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
        COMMENT "Copying assets to build directory"
    )
endif()

# Terrain preprocessing - runs tools to generate data if inputs changed
set(TERRAIN_HEIGHTMAP ${CMAKE_CURRENT_SOURCE_DIR}/assets/terrain/isleofwight-0m-200m.png)
set(TERRAIN_DATA_DIR ${CMAKE_BINARY_DIR}/terrain_data)
set(TERRAIN_TILES_META ${TERRAIN_DATA_DIR}/terrain_tiles.meta)
set(EROSION_DATA_META ${TERRAIN_DATA_DIR}/erosion_data.meta)

# Create terrain data directory
file(MAKE_DIRECTORY ${TERRAIN_DATA_DIR})

# Run terrain_preprocess when heightmap changes
# Note: We don't depend on terrain_preprocess executable to avoid re-running when code changes
add_custom_command(
    OUTPUT ${TERRAIN_TILES_META}
    COMMAND $<TARGET_FILE:terrain_preprocess>
        ${TERRAIN_HEIGHTMAP}
        ${TERRAIN_DATA_DIR}
        --min-altitude -15
        --max-altitude 220
    DEPENDS ${TERRAIN_HEIGHTMAP}
    COMMENT "Preprocessing terrain tiles"
)

# Run erosion_preprocess when heightmap changes
add_custom_command(
    OUTPUT ${EROSION_DATA_META}
    COMMAND $<TARGET_FILE:erosion_preprocess>
        ${TERRAIN_HEIGHTMAP}
        ${TERRAIN_DATA_DIR}
        --min-altitude -15
        --max-altitude 220
        --sea-level 23
        --terrain-size 16384
    DEPENDS ${TERRAIN_HEIGHTMAP}
    COMMENT "Preprocessing erosion and water placement"
)

# Custom target that depends on both preprocessing outputs
add_custom_target(terrain_preprocessing
    DEPENDS ${TERRAIN_TILES_META} ${EROSION_DATA_META}
)

# Ensure tools are built before preprocessing runs
add_dependencies(terrain_preprocessing terrain_preprocess erosion_preprocess)

# Foam noise texture generation
set(FOAM_TEXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures)
set(FOAM_TEXTURE ${FOAM_TEXTURE_DIR}/foam_noise.png)

# Generate foam noise texture (only regenerate if tool source changes)
add_custom_command(
    OUTPUT ${FOAM_TEXTURE}
    COMMAND $<TARGET_FILE:foam_noise_gen>
        --resolution 512
        --octaves 4
        --output ${FOAM_TEXTURE}
    DEPENDS foam_noise_gen
    COMMENT "Generating foam noise texture"
)

# Custom target for foam texture generation
add_custom_target(foam_texture_gen
    DEPENDS ${FOAM_TEXTURE}
)

# Main target depends on foam texture
add_dependencies(${PROJECT_NAME} foam_texture_gen)

# Cape texture generation
set(CAPE_TEXTURE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures)
set(CAPE_DIFFUSE ${CAPE_TEXTURE_DIR}/cape_diffuse.png)
set(CAPE_NORMAL ${CAPE_TEXTURE_DIR}/cape_normal.png)

# Generate cape textures
add_custom_command(
    OUTPUT ${CAPE_DIFFUSE} ${CAPE_NORMAL}
    COMMAND $<TARGET_FILE:cape_texture_gen>
        --width 256
        --height 512
        --output ${CAPE_DIFFUSE}
        --normal ${CAPE_NORMAL}
    DEPENDS cape_texture_gen
    COMMENT "Generating cape textures"
)

# Custom target for cape texture generation
add_custom_target(cape_texture_gen_target
    DEPENDS ${CAPE_DIFFUSE} ${CAPE_NORMAL}
)

# Main target depends on cape textures
add_dependencies(${PROJECT_NAME} cape_texture_gen_target)

# SBSAR (Substance Archive) texture processing
set(SBSAR_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/substances)
set(SBSAR_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/substances)
set(SBSAR_RESOLUTION 1024)

# Create output directory
file(MAKE_DIRECTORY ${SBSAR_OUTPUT_DIR})

# Collect all SBSAR files from the substances directory
file(GLOB SBSAR_FILES "${SBSAR_SOURCE_DIR}/*.sbsar")

# Create a list to track all generated texture manifests
set(SBSAR_MANIFESTS "")

# Process each SBSAR file
foreach(SBSAR_FILE ${SBSAR_FILES})
    get_filename_component(SBSAR_NAME ${SBSAR_FILE} NAME_WE)
    set(SBSAR_MANIFEST ${SBSAR_OUTPUT_DIR}/${SBSAR_NAME}_manifest.txt)

    add_custom_command(
        OUTPUT ${SBSAR_MANIFEST}
        COMMAND $<TARGET_FILE:sbsar_render>
            "${SBSAR_FILE}"
            "${SBSAR_OUTPUT_DIR}"
            --name "${SBSAR_NAME}"
            --resolution ${SBSAR_RESOLUTION}
        DEPENDS ${SBSAR_FILE} sbsar_render
        COMMENT "Processing SBSAR: ${SBSAR_NAME}"
    )

    list(APPEND SBSAR_MANIFESTS ${SBSAR_MANIFEST})
endforeach()

# Custom target for all SBSAR processing
add_custom_target(sbsar_textures
    DEPENDS ${SBSAR_MANIFESTS}
)

# Main target depends on SBSAR textures (only if there are any)
if(SBSAR_MANIFESTS)
    add_dependencies(${PROJECT_NAME} sbsar_textures)
endif()

# Main target depends on preprocessing
add_dependencies(${PROJECT_NAME} terrain_preprocessing)

# Copy terrain data to output location
if(APPLE)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${TERRAIN_DATA_DIR}
            $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/terrain_data
        COMMENT "Copying terrain data to app bundle"
    )
else()
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${TERRAIN_DATA_DIR}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/terrain_data
        COMMENT "Copying terrain data to build directory"
    )
endif()

set(SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skinned.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skinned_shadow.vert.spv
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
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hiz_downsample.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hiz_culling.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_position.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_position.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_displacement.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/foam_blur.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_tile_cull.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_billboard.frag.spv
)

set(TERRAIN_SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_frustum_cull.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_prepare_cull_dispatch.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass_subgroup.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_batched.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow_cull.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow_culled.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_meshlet_shadow_culled.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_wireframe.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_meshlet.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_meshlet_shadow.vert.spv
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
    # Common shader includes that many shaders depend on
    set(BINDINGS_DEPS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bindings.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ubo_common.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ubo_snow.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ubo_cloud_shadow.glsl
    )

    # Simple shaders using SHADER_COMPILE_CMD (no includes)
    set(SIMPLE_SHADERS
        shadow.vert shadow.frag
        postprocess.vert postprocess.frag
        bloom_downsample.frag bloom_upsample.frag bloom_composite.frag
        histogram_build.comp histogram_reduce.comp
        hiz_downsample.comp hiz_culling.comp
    )

    foreach(SHADER ${SIMPLE_SHADERS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv
            COMMAND ${SHADER_COMPILE_CMD} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            COMMENT "Compiling shader: ${SHADER}"
        )
    endforeach()

    # Shaders that use bindings.glsl and other includes
    set(SHADERS_WITH_BINDINGS
        shader.vert shader.frag
        skinned.vert skinned_shadow.vert
        sky.vert sky.frag
        grass.comp grass_displacement.comp grass.vert grass.frag
        grass_shadow.vert grass_shadow.frag
        froxel_update.comp froxel_integrate.comp
        weather.comp weather.vert weather.frag
        leaf.comp leaf.vert leaf.frag
        snow_accumulation.comp
        tree.vert tree.frag tree_billboard.frag
    )

    foreach(SHADER ${SHADERS_WITH_BINDINGS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv
            COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER} ${BINDINGS_DEPS}
            COMMENT "Compiling shader with includes: ${SHADER}"
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
    set(SPECIAL_COMPUTE_SHADERS
        volumetric_snow.comp
        cloudmap_lut.comp
        cloud_shadow.comp
    )

    foreach(SHADER ${SPECIAL_COMPUTE_SHADERS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv
            COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            COMMENT "Compiling special compute shader: ${SHADER}"
        )
    endforeach()

    # Water shader dependencies (common includes)
    set(WATER_DEPS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/constants_common.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/lighting_common.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_common.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/atmosphere_common.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain_height_common.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/flow_common.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fbm_common.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/foam.glsl
    )

    # Water shaders with common includes
    set(WATER_SHADERS
        water.vert
        water.frag
        water_position.vert
        water_position.frag
    )

    foreach(SHADER ${WATER_SHADERS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv
            COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER} ${WATER_DEPS}
            COMMENT "Compiling water shader: ${SHADER}"
        )
    endforeach()

    # Water displacement compute shader (Phase 4)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_displacement.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_displacement.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_displacement.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_displacement.comp
        COMMENT "Compiling water displacement compute shader"
    )

    # Foam blur compute shader (Phase 14: temporal foam persistence)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/foam_blur.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/foam_blur.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/foam_blur.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/foam_blur.comp
        COMMENT "Compiling foam blur compute shader"
    )

    # SSR compute shader (Phase 10: Screen-Space Reflections)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr.comp
        COMMENT "Compiling SSR compute shader"
    )

    # Water tile cull compute shader (Phase 7: Screen-Space Tessellation)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_tile_cull.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_tile_cull.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_tile_cull.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_tile_cull.comp
        COMMENT "Compiling water tile cull compute shader"
    )

    # Terrain shaders (LEB terrain system)
    set(TERRAIN_CBT_DEPS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/cbt.glsl
    )
    set(TERRAIN_LEB_DEPS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/cbt.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/leb.glsl
    )

    # Terrain shaders with CBT dependencies
    set(TERRAIN_CBT_SHADERS
        terrain_dispatcher.comp
        terrain_sum_reduction_prepass.comp
        terrain_sum_reduction_prepass_subgroup.comp
        terrain_sum_reduction.comp
        terrain_sum_reduction_batched.comp
    )

    foreach(SHADER ${TERRAIN_CBT_SHADERS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}.spv
            COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER} ${TERRAIN_CBT_DEPS}
            COMMENT "Compiling terrain shader: ${SHADER}"
        )
    endforeach()

    # Terrain shaders with LEB dependencies
    set(TERRAIN_LEB_SHADERS
        terrain_subdivision.comp
        terrain_frustum_cull.comp
        terrain_shadow_cull.comp
        terrain.vert
        terrain_shadow.vert
        terrain_shadow_culled.vert
        terrain_meshlet.vert
        terrain_meshlet_shadow.vert
        terrain_meshlet_shadow_culled.vert
    )

    foreach(SHADER ${TERRAIN_LEB_SHADERS})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}.spv
            COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/${SHADER} ${TERRAIN_LEB_DEPS}
            COMMENT "Compiling terrain shader: ${SHADER}"
        )
    endforeach()

    # Simple terrain shaders without special dependencies
    set(TERRAIN_SIMPLE_SHADERS
        terrain.frag
        terrain_shadow.frag
        terrain_wireframe.frag
        terrain_prepare_cull_dispatch.comp
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
    set(CATMULLCLARK_SIMPLE_SHADERS
        catmullclark_subdivision.comp.glsl
        catmullclark_render.frag.glsl
    )

    foreach(SHADER ${CATMULLCLARK_SIMPLE_SHADERS})
        get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
        get_filename_component(SHADER_EXT ${SHADER} EXT)
        string(REPLACE ".glsl" "" SHADER_EXT ${SHADER_EXT})
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER_NAME}${SHADER_EXT}.spv
            COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER_NAME}${SHADER_EXT}.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}
            COMMENT "Compiling Catmull-Clark shader: ${SHADER_NAME}${SHADER_EXT}"
        )
    endforeach()

    # Catmull-Clark vertex shader with special dependencies
    set(CATMULLCLARK_VERT_DEPS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cbt_common.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_mesh.glsl
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_tessellation.glsl
    )

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.vert.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -I${CMAKE_CURRENT_SOURCE_DIR}/shaders -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.vert.glsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/catmullclark_render.vert.glsl ${CATMULLCLARK_VERT_DEPS}
        COMMENT "Compiling Catmull-Clark vertex shader"
    )
    add_custom_target(shaders DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skinned.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skinned_shadow.vert.spv
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
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hiz_downsample.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hiz_culling.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_position.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_position.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_displacement.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/foam_blur.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_dispatcher.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_subdivision.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_frustum_cull.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_prepare_cull_dispatch.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_prepass_subgroup.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_sum_reduction_batched.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow_cull.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_shadow_culled.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_meshlet_shadow_culled.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_wireframe.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_meshlet.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain/terrain_meshlet_shadow.vert.spv
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
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag.spv
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
