cmake_minimum_required(VERSION 3.20)
project(vulkan-game VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to skip terrain preprocessing (faster builds)
option(SKIP_TERRAIN_PREPROCESSING "Skip building terrain preprocessing tools" OFF)

find_package(Vulkan REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)  # vulkan-hpp headers
find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(unofficial-openfbx CONFIG REQUIRED)
find_package(lodepng CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)

find_path(STB_INCLUDE_DIRS "stb_image.h")

# Add tools subdirectory for shader reflection
add_subdirectory(tools)

set(SOURCES
    src/main.cpp
    # ECS
    src/ecs/Components.cpp
    src/ecs/ECSMaterialDemo.cpp
    # Scene
    src/scene/Application.cpp
    src/scene/SceneBuilder.cpp
    src/scene/SceneManager.cpp
    src/scene/SceneMaterial.cpp
    src/scene/SceneCollection.cpp
    src/scene/Camera.cpp
    src/scene/Transform.cpp
    src/scene/InputSystem.cpp
    # NPC
    src/npc/NPCSimulation.cpp
    src/npc/NPCRenderer.cpp
    src/npc/CharacterTemplate.cpp
    # Core
    src/core/CrashHandler.cpp
    src/core/Renderer.cpp
    src/core/SelectionOutlineRenderer.cpp
    src/core/FrameExecutor.cpp
    src/core/RendererInitPhases.cpp
    src/core/CoreResources.cpp
    src/core/SystemWiring.cpp
    src/core/ResizeCoordinator.cpp
    src/core/UBOBuilder.cpp
    src/core/RendererSystems.cpp
    src/core/InitContext.cpp
    src/core/ScenePipeline.cpp
    # Control subsystems (coordinate multiple systems for GUI interfaces)
    # Note: Some systems implement their interfaces directly (CelestialCalculator -> ILocationControl,
    #       TerrainSystem -> ITerrainControl, Profiler -> IProfilerControl, WeatherSystem -> IWeatherState,
    #       PostProcessSystem -> IPostProcessState, CloudShadowSystem -> ICloudShadowControl)
    src/core/controls/EnvironmentControlSubsystem.cpp
    src/core/controls/WaterControlSubsystem.cpp
    src/core/controls/TreeControlSubsystem.cpp
    src/core/controls/DebugControlSubsystem.cpp
    src/core/controls/SceneControlSubsystem.cpp
    src/core/controls/PlayerControlSubsystem.cpp
    src/core/vulkan/VulkanContext.cpp
    src/core/vulkan/AsyncTransferManager.cpp
    src/core/vulkan/ThreadedCommandPool.cpp
    $<IF:$<PLATFORM_ID:Darwin>,src/core/vulkan/MetalLayerFix.mm,src/core/vulkan/MetalLayerFix.cpp>
    src/core/threading/TaskScheduler.cpp
    src/core/pipeline/FrameGraph.cpp
    src/core/pipeline/FrameGraphBuilder.cpp
    src/core/passes/ComputePasses.cpp
    src/core/passes/ShadowPasses.cpp
    src/core/passes/ShadowPassRecorder.cpp
    src/core/passes/ShadowPassResources.cpp
    src/core/passes/WaterPasses.cpp
    src/core/passes/HDRPass.cpp
    src/core/passes/HDRPassRecorder.cpp
    src/core/passes/HDRPassResources.cpp
    src/core/passes/SceneObjectsDrawable.cpp
    src/core/passes/SkinnedCharDrawable.cpp
    src/core/passes/DebugLinesDrawable.cpp
    src/core/passes/WaterDrawable.cpp
    src/core/passes/PostPasses.cpp
    src/core/FrameUpdater.cpp
    src/core/FrameDataBuilder.cpp
    src/core/updaters/VegetationUpdater.cpp
    src/core/updaters/AtmosphereUpdater.cpp
    src/core/updaters/EnvironmentUpdater.cpp
    src/core/updaters/UBOUpdater.cpp
    src/core/LoadingRenderer.cpp
    src/core/loading/LoadJobQueue.cpp
    src/core/loading/AsyncStartupLoader.cpp
    src/core/loading/AsyncSystemLoader.cpp
    src/core/loading/LoadJobFactory.cpp
    src/core/Mesh.cpp
    src/core/SingleBuffer.cpp
    src/core/PerFrameBuffer.cpp
    src/core/DoubleBufferedBuffer.cpp
    src/core/DynamicUniformBuffer.cpp
    src/core/DoubleBufferedImage.cpp
    src/core/FrameIndexedBuffers.cpp
    src/core/TripleBuffering.cpp
    src/core/Texture.cpp
    src/core/ShaderLoader.cpp
    src/core/pipeline/PipelineBuilder.cpp
    src/core/pipeline/PipelineCache.cpp
    src/core/pipeline/GraphicsPipelineFactory.cpp
    src/core/material/DescriptorManager.cpp
    src/core/material/MaterialDescriptorFactory.cpp
    src/core/material/MaterialRegistry.cpp
    src/core/asset/AssetRegistry.cpp
    src/core/RenderableBuilder.cpp
    # Atmosphere
    src/atmosphere/TimeSystem.cpp
    src/atmosphere/CelestialCalculator.cpp
    src/atmosphere/WindSystem.cpp
    src/atmosphere/WeatherSystem.cpp
    src/atmosphere/AtmosphereLUTSystem.cpp
    src/atmosphere/AtmosphereLUTResources.cpp
    src/atmosphere/AtmosphereLUTDescriptors.cpp
    src/atmosphere/AtmosphereLUTPipelines.cpp
    src/atmosphere/AtmosphereLUTCompute.cpp
    src/atmosphere/AtmosphereLUTExport.cpp
    src/atmosphere/SkySystem.cpp
    src/atmosphere/CloudShadowSystem.cpp
    src/atmosphere/SnowMaskSystem.cpp
    src/atmosphere/VolumetricSnowSystem.cpp
    src/atmosphere/AtmosphereSystemGroup.cpp
    src/atmosphere/SnowSystemGroup.cpp
    # Vegetation
    src/vegetation/DisplacementSystem.cpp
    src/vegetation/GrassSystem.cpp
    src/vegetation/GrassLODStrategy.cpp
    src/vegetation/GrassTile.cpp
    src/vegetation/VegetationRenderContext.cpp
    src/vegetation/GrassTileManager.cpp
    src/vegetation/GrassTileTracker.cpp
    src/vegetation/GrassTileResourcePool.cpp
    src/vegetation/GrassControlAdapter.cpp
    src/vegetation/LeafSystem.cpp
    src/vegetation/ScatterSystem.cpp
    src/vegetation/ScatterSystemFactory.cpp
    src/vegetation/TreeSystem.cpp
    src/vegetation/TreeGenerator.cpp
    src/vegetation/ThreadedTreeGenerator.cpp
    src/vegetation/VegetationContentGenerator.cpp
    src/vegetation/DeferredTerrainObjects.cpp
    src/vegetation/TreeOptions.cpp
    src/vegetation/TreeRenderer.cpp
    src/vegetation/TreeLeafCulling.cpp
    src/vegetation/TreeBranchCulling.cpp
    src/vegetation/TreeImpostorAtlas.cpp
    src/vegetation/TreeImpostorAtlasCapture.cpp
    src/vegetation/TreeLODSystem.cpp
    src/vegetation/TreeSpatialIndex.cpp
    src/vegetation/ImpostorCullSystem.cpp
    src/vegetation/BranchGenerator.cpp
    src/vegetation/VegetationSystemGroup.cpp
    # Postprocess
    src/postprocess/PostProcessSystem.cpp
    src/postprocess/BloomSystem.cpp
    src/postprocess/ComputeBloomSystem.cpp
    src/postprocess/GodRaysSystem.cpp
    src/postprocess/BilateralGridSystem.cpp
    src/postprocess/SSRSystem.cpp
    src/postprocess/HiZSystem.cpp
    src/postprocess/ScreenSpaceShadowSystem.cpp
    # GPU Culling
    src/core/GPUSceneBuffer.cpp
    src/culling/GPUCullPass.cpp
    # Lighting
    src/lighting/FroxelSystem.cpp
    src/lighting/ShadowSystem.cpp
    # Physics
    src/physics/PhysicsSystem.cpp
    src/physics/JoltLayerConfig.cpp
    src/physics/JoltRuntime.cpp
    src/physics/CharacterController.cpp
    src/physics/PhysicsTerrainTileManager.cpp
    src/physics/ParticleSystem.cpp
    src/physics/ClothSimulation.cpp
    src/physics/PlayerCape.cpp
    src/physics/PhysicsDebugRenderer.cpp
    src/physics/RagdollBuilder.cpp
    src/physics/RagdollInstance.cpp
    # Terrain
    src/terrain/TerrainSystem.cpp
    src/terrain/TerrainFactory.cpp
    src/terrain/TerrainBuffers.cpp
    src/terrain/TerrainCameraOptimizer.cpp
    src/terrain/TerrainEffects.cpp
    src/terrain/TerrainPipelines.cpp
    src/terrain/TerrainTextures.cpp
    src/terrain/TerrainCBT.cpp
    src/terrain/TerrainMeshlet.cpp
    src/terrain/TerrainTileCache.cpp
    src/terrain/ErosionDataLoader.cpp
    src/terrain/RoadNetworkLoader.cpp
    src/terrain/virtual_texture/VirtualTextureCache.cpp
    src/terrain/virtual_texture/VirtualTexturePageTable.cpp
    src/terrain/virtual_texture/VirtualTextureFeedback.cpp
    src/terrain/virtual_texture/VirtualTextureTileLoader.cpp
    src/terrain/virtual_texture/VirtualTextureSystem.cpp
    # Water
    src/water/WaterSystem.cpp
    src/water/FlowMapGenerator.cpp
    src/water/WaterGBuffer.cpp
    src/water/WaterDisplacement.cpp
    src/water/FoamBuffer.cpp
    src/water/WaterTileCull.cpp
    src/water/WaterSystemGroup.cpp
    # GUI
    src/gui/GuiSystem.cpp
    src/gui/GuiTimeTab.cpp
    src/gui/GuiWeatherTab.cpp
    src/gui/GuiEnvironmentTab.cpp
    src/gui/GuiPostFXTab.cpp
    src/gui/GuiTerrainTab.cpp
    src/gui/GuiWaterTab.cpp
    src/gui/GuiDebugTab.cpp
    src/gui/GuiProfilerTab.cpp
    src/gui/GuiFlamegraph.cpp
    src/gui/GuiIKTab.cpp
    src/gui/GuiPlayerTab.cpp
    src/gui/GuiPerformanceTab.cpp
    src/gui/GuiTreeTab.cpp
    src/gui/GuiGrassTab.cpp
    src/gui/GuiSceneGraphTab.cpp
    src/gui/GuiHierarchyPanel.cpp
    src/gui/GuiInspectorPanel.cpp
    src/gui/GuiSceneEditor.cpp
    src/gui/GuiGizmo.cpp
    # Subdivision
    src/subdivision/CatmullClarkCBT.cpp
    src/subdivision/CatmullClarkMesh.cpp
    src/subdivision/CatmullClarkSystem.cpp
    src/subdivision/GeometrySystemGroup.cpp
    # Loaders
    src/loaders/OBJLoader.cpp
    src/loaders/GLTFLoader.cpp
    src/loaders/FBXLoader.cpp
    src/loaders/FBXPostProcess.cpp
    # Animation
    src/animation/SkinnedMesh.cpp
    src/animation/SkinnedMeshRenderer.cpp
    src/animation/Animation.cpp
    src/animation/AnimatedCharacter.cpp
    src/animation/AnimationArchetypeManager.cpp
    src/animation/AnimationStateMachine.cpp
    src/animation/AnimationEventLoader.cpp
    src/animation/AnimationBlend.cpp
    src/animation/AnimationLayer.cpp
    src/animation/AnimationLayerController.cpp
    src/animation/BlendSpace.cpp
    src/animation/BoneMask.cpp
    src/animation/FootPhaseTracker.cpp
    src/animation/MotionMatchingFeature.cpp
    src/animation/MotionMatchingTrajectory.cpp
    src/animation/MotionMatchingKDTree.cpp
    src/animation/MotionDatabase.cpp
    src/animation/MotionMatchingController.cpp
    # IK
    src/ik/IKSolver.cpp
    src/ik/IKUtils.cpp
    src/ik/TwoBoneIKSolver.cpp
    src/ik/LookAtIKSolver.cpp
    src/ik/FootPlacementIKSolver.cpp
    src/ik/StraddleIKSolver.cpp
    src/ik/ClimbingIKSolver.cpp
    # Debug
    src/debug/GpuProfiler.cpp
    src/debug/CpuProfiler.cpp
    src/debug/DebugLineSystem.cpp
    src/debug/RoadRiverVisualization.cpp
    # Machine Learning
    src/ml/MLPNetwork.cpp
    src/ml/ModelLoader.cpp
    src/ml/CALMCharacterConfig.cpp
    src/ml/CALMObservation.cpp
    src/ml/CALMActionApplier.cpp
    src/ml/CALMLatentSpace.cpp
    src/ml/CALMLowLevelController.cpp
    src/ml/CALMController.cpp
    src/ml/CALMHighLevelController.cpp
    src/ml/CALMBehaviorFSM.cpp
    src/ml/CALMAnimationIntegration.cpp
    src/ml/CALMGPUInference.cpp
    src/ml/CALMModelLoader.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ecs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/animation
    ${CMAKE_CURRENT_SOURCE_DIR}/src/atmosphere
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/vulkan
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/threading
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/material
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/asset
    ${CMAKE_CURRENT_SOURCE_DIR}/src/debug
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ik
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lighting
    ${CMAKE_CURRENT_SOURCE_DIR}/src/loaders
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ml
    ${CMAKE_CURRENT_SOURCE_DIR}/src/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/src/postprocess
    ${CMAKE_CURRENT_SOURCE_DIR}/src/scene
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stages
    ${CMAKE_CURRENT_SOURCE_DIR}/src/subdivision
    ${CMAKE_CURRENT_SOURCE_DIR}/src/terrain
    ${CMAKE_CURRENT_SOURCE_DIR}/src/terrain/virtual_texture
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vegetation
    ${CMAKE_CURRENT_SOURCE_DIR}/src/water
    ${CMAKE_CURRENT_SOURCE_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}  # For shaders/bindings.h
    ${STB_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    Vulkan::Headers
    SDL3::SDL3
    glm::glm
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    Jolt::Jolt
    imgui::imgui
    imguizmo::imguizmo
    fastgltf::fastgltf
    unoffical::openfbx::openfbx
    lodepng
    nlohmann_json::nlohmann_json
    EnTT::EnTT
)

# Enable Jolt Physics debug renderer for visualization
target_compile_definitions(${PROJECT_NAME} PRIVATE JPH_DEBUG_RENDERER)

# Configure Vulkan-HPP: disable positional constructors to enforce builder pattern
target_compile_definitions(${PROJECT_NAME} PRIVATE
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS   # Force use of setters/.set*() builder methods
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1  # Use dynamic dispatch for flexibility
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

    # Copy assets/textures to Resources/textures
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
    # Copy assets/textures to build output (Linux/Windows)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/textures
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
# Tools are organized into parallel targets where possible for faster builds
# Output goes to generated/terrain_data/ (shared between debug/release builds)
set(TERRAIN_HEIGHTMAP ${CMAKE_CURRENT_SOURCE_DIR}/assets/terrain/isleofwight-0m-200m.png)
set(TERRAIN_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated/terrain_data)
set(TERRAIN_TILES_META ${TERRAIN_DATA_DIR}/terrain_tiles.meta)

# Create terrain data directory
file(MAKE_DIRECTORY ${TERRAIN_DATA_DIR})

# ============================================================================
# PARALLEL PHASE: terrain_preprocess and watershed run concurrently
# Both only depend on the heightmap, so Ninja can run them in parallel
# ============================================================================

# Run terrain_preprocess when heightmap changes
add_custom_command(
    OUTPUT ${TERRAIN_TILES_META}
    COMMAND $<TARGET_FILE:terrain_preprocess>
        ${TERRAIN_HEIGHTMAP}
        ${TERRAIN_DATA_DIR}
        --min-altitude -15
        --max-altitude 220
    DEPENDS ${TERRAIN_HEIGHTMAP}
    COMMENT "Preprocessing terrain tiles (parallel)"
)

# Explicit target for terrain tile generation (enables parallel build)
add_custom_target(terrain_tiles_gen DEPENDS ${TERRAIN_TILES_META})
add_dependencies(terrain_tiles_gen terrain_preprocess)

# Watershed analysis outputs
set(WATERSHED_DATA_DIR ${TERRAIN_DATA_DIR}/watershed)
set(WATERSHED_RIVERS_SVG ${WATERSHED_DATA_DIR}/rivers.svg)
set(WATERSHED_FLOW_PNG ${WATERSHED_DATA_DIR}/flow.png)
set(WATERSHED_FLOW_BIN ${WATERSHED_DATA_DIR}/flow_accumulation.bin)
set(WATERSHED_DIR_BIN ${WATERSHED_DATA_DIR}/flow_direction.bin)
set(WATERSHED_LABELS_BIN ${WATERSHED_DATA_DIR}/watershed_labels.bin)
set(WATERSHED_RIVERS_DAT ${WATERSHED_DATA_DIR}/rivers.dat)
set(WATERSHED_LAKES_DAT ${WATERSHED_DATA_DIR}/lakes.dat)
set(WATERSHED_META ${WATERSHED_DATA_DIR}/watershed.meta)

# Run watershed tool when heightmap changes
add_custom_command(
    OUTPUT ${WATERSHED_RIVERS_SVG} ${WATERSHED_FLOW_PNG}
           ${WATERSHED_FLOW_BIN} ${WATERSHED_DIR_BIN} ${WATERSHED_LABELS_BIN}
           ${WATERSHED_RIVERS_DAT} ${WATERSHED_LAKES_DAT} ${WATERSHED_META}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WATERSHED_DATA_DIR}
    COMMAND $<TARGET_FILE:watershed>
        ${TERRAIN_HEIGHTMAP}
        --output ${WATERSHED_DATA_DIR}
        --threshold 5000
        --sea-level 6000
        --resolution 512
        --terrain-size 16384
        --min-altitude 0
        --max-altitude 200
    DEPENDS ${TERRAIN_HEIGHTMAP}
    COMMENT "Running watershed analysis (parallel)"
)

# Explicit target for watershed generation (enables parallel build)
add_custom_target(watershed_gen DEPENDS ${WATERSHED_RIVERS_SVG} ${WATERSHED_FLOW_PNG}
                                        ${WATERSHED_FLOW_BIN} ${WATERSHED_DIR_BIN} ${WATERSHED_LABELS_BIN}
                                        ${WATERSHED_RIVERS_DAT} ${WATERSHED_LAKES_DAT} ${WATERSHED_META})
add_dependencies(watershed_gen watershed)

# ============================================================================
# SEQUENTIAL PHASE: biome_preprocess runs after watershed completes
# Biome classification requires watershed flow data
# ============================================================================

# Biome preprocessing outputs
set(BIOME_DATA_DIR ${TERRAIN_DATA_DIR}/biome)
set(BIOME_MAP_PNG ${BIOME_DATA_DIR}/biome_map.png)
set(BIOME_SETTLEMENTS_JSON ${BIOME_DATA_DIR}/settlements.json)
set(BIOME_META ${BIOME_DATA_DIR}/biome.meta)

# Run biome_preprocess after watershed completes
add_custom_command(
    OUTPUT ${BIOME_MAP_PNG} ${BIOME_SETTLEMENTS_JSON} ${BIOME_META}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BIOME_DATA_DIR}
    COMMAND $<TARGET_FILE:biome_preprocess>
        ${TERRAIN_HEIGHTMAP}
        ${WATERSHED_DATA_DIR}
        ${BIOME_DATA_DIR}
        --sea-level 23
        --min-altitude -15
        --max-altitude 220
        --terrain-size 16384
        --output-resolution 1024
        --num-settlements 20
    DEPENDS ${TERRAIN_HEIGHTMAP} ${WATERSHED_FLOW_BIN} ${WATERSHED_DIR_BIN}
    COMMENT "Generating biome classification (after watershed)"
)

# Explicit target for biome generation
add_custom_target(biome_gen DEPENDS ${BIOME_MAP_PNG} ${BIOME_SETTLEMENTS_JSON} ${BIOME_META})
add_dependencies(biome_gen biome_preprocess watershed_gen)

# ============================================================================
# SEQUENTIAL PHASE: road_generator runs after biome_preprocess completes
# Road generation requires settlements and biome data
# ============================================================================

# Road generation outputs
set(ROADS_DATA_DIR ${TERRAIN_DATA_DIR}/roads)
set(ROADS_JSON ${ROADS_DATA_DIR}/roads.json)
set(ROADS_BIN ${ROADS_DATA_DIR}/roads.bin)
set(ROADS_META ${ROADS_DATA_DIR}/roads.meta)

# Run road_generator after biome_preprocess completes
add_custom_command(
    OUTPUT ${ROADS_JSON} ${ROADS_BIN} ${ROADS_META}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ROADS_DATA_DIR}
    COMMAND $<TARGET_FILE:road_generator>
        ${TERRAIN_HEIGHTMAP}
        ${BIOME_MAP_PNG}
        ${BIOME_SETTLEMENTS_JSON}
        ${ROADS_DATA_DIR}
        --terrain-size 16384
        --min-altitude -15
        --max-altitude 220
        --grid-resolution 512
    DEPENDS ${TERRAIN_HEIGHTMAP} ${BIOME_MAP_PNG} ${BIOME_SETTLEMENTS_JSON}
    COMMENT "Generating road network (after biome)"
)

# Explicit target for road generation
add_custom_target(roads_gen DEPENDS ${ROADS_JSON} ${ROADS_BIN} ${ROADS_META})
add_dependencies(roads_gen road_generator biome_gen)

# ============================================================================
# UMBRELLA TARGET: terrain_preprocessing depends on all outputs
# ============================================================================

# Custom target that depends on all preprocessing outputs
add_custom_target(terrain_preprocessing
    DEPENDS ${TERRAIN_TILES_META} ${WATERSHED_RIVERS_SVG} ${BIOME_MAP_PNG} ${ROADS_BIN}
)

# Dependencies ensure correct build order:
# - terrain_tiles_gen and watershed_gen can run in parallel
# - biome_gen runs after watershed_gen completes
# - roads_gen runs after biome_gen completes
add_dependencies(terrain_preprocessing terrain_tiles_gen watershed_gen biome_gen roads_gen)

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

# Caustics texture generation
set(CAUSTICS_TEXTURE ${FOAM_TEXTURE_DIR}/caustics.png)

# Generate caustics texture (only regenerate if tool source changes)
add_custom_command(
    OUTPUT ${CAUSTICS_TEXTURE}
    COMMAND $<TARGET_FILE:caustics_gen>
        --resolution 512
        --waves 8
        --output ${CAUSTICS_TEXTURE}
    DEPENDS caustics_gen
    COMMENT "Generating caustics texture"
)

# Custom target for caustics texture generation
add_custom_target(caustics_texture_gen
    DEPENDS ${CAUSTICS_TEXTURE}
)

# Main target depends on caustics texture
add_dependencies(${PROJECT_NAME} caustics_texture_gen)

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

# Main target depends on preprocessing (unless skipped for faster builds)
if(NOT SKIP_TERRAIN_PREPROCESSING)
    add_dependencies(${PROJECT_NAME} terrain_preprocessing)
endif()

# Copy terrain data to output location
if(APPLE)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${TERRAIN_DATA_DIR}
            $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/terrain_data
        COMMENT "Copying terrain data to app bundle"
    )
    # Copy virtual texture tiles if they exist
    if(EXISTS ${CMAKE_SOURCE_DIR}/generated/vt_tiles)
        add_custom_command(
            TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/generated/vt_tiles
                $<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/vt_tiles
            COMMENT "Copying virtual texture tiles to app bundle"
        )
    endif()
else()
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${TERRAIN_DATA_DIR}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/terrain_data
        COMMENT "Copying terrain data to build directory"
    )
    # Copy virtual texture tiles if they exist
    if(EXISTS ${CMAKE_SOURCE_DIR}/generated/vt_tiles)
        add_custom_command(
            TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/generated/vt_tiles
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/vt_tiles
            COMMENT "Copying virtual texture tiles to build directory"
        )
    endif()
endif()

# Check for shader compiler early so we can skip shader target_sources if not available
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/bin)

# Check if shaders already exist (pre-compiled) or if we have a compiler
set(FIRST_SHADER_FILE ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv)
if(EXISTS ${FIRST_SHADER_FILE} OR GLSLC OR GLSLANG_VALIDATOR)
    set(SHADERS_AVAILABLE TRUE)
else()
    set(SHADERS_AVAILABLE FALSE)
    message(WARNING "No shader compiler found and no pre-compiled shaders exist. Main executable will not be built.")
endif()

if(SHADERS_AVAILABLE)

set(SHADER_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skinned.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skinned_shadow.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_tiled.comp.spv
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
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bilateral_grid_build.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bilateral_grid_blur.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_resolve.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/snow_accumulation.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_snow.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hiz_downsample.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hiz_culling.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_cull.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_tess.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.tesc.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.tese.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_position.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_position.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_displacement.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/foam_blur.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr_blur.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_tile_cull.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_line.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_line.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/loading.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/loading.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_shadow.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf_shadow.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf_shadow.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_geometry.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf_cull.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_filter.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf_cull_phase3.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_cull.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_cell_cull.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_capture.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_capture.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_capture_leaf.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_shadow.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_shadow.frag.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_branch_shadow_cull.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_branch_shadow_instanced.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_instanced.vert.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_compute.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_upsample_compute.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_spd.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/godrays_compute.comp.spv
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/calm_inference.comp.spv
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
        hiz_downsample.comp
        debug_line.vert debug_line.frag
        loading.vert loading.frag
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
        grass.comp grass_tiled.comp grass_displacement.comp grass.vert grass.frag
        grass_shadow.vert grass_shadow.frag
        froxel_update.comp froxel_integrate.comp
        weather.comp weather.vert weather.frag
        leaf.comp leaf.vert leaf.frag
        shadow_resolve.comp
        snow_accumulation.comp
        bilateral_grid_build.comp bilateral_grid_blur.comp
        tree.vert tree.frag
        tree_leaf.vert tree_leaf.frag
        tree_shadow.vert
        tree_branch_shadow_instanced.vert
        shadow_instanced.vert
        tree_branch_shadow_cull.comp
        tree_leaf_shadow.vert tree_leaf_shadow.frag
        tree_geometry.comp tree_leaf.comp tree_leaf_cull.comp tree_filter.comp tree_leaf_cull_phase3.comp tree_impostor_cull.comp tree_cell_cull.comp
        tree_impostor.vert tree_impostor.frag
        tree_impostor_capture.vert tree_impostor_capture.frag
        tree_impostor_capture_leaf.vert
        tree_impostor_shadow.vert tree_impostor_shadow.frag
        hiz_culling.comp
        scene_cull.comp
        bloom_compute.comp bloom_upsample_compute.comp bloom_spd.comp godrays_compute.comp
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
        calm_inference.comp
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
        water_tess.vert
        water.tesc
        water.tese
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

    # SSR bilateral blur compute shader
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr_blur.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} --target-env vulkan1.2 -V -o ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr_blur.comp.spv ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr_blur.comp
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr_blur.comp
        COMMENT "Compiling SSR blur compute shader"
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
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/grass_tiled.comp.spv
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
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bilateral_grid_build.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bilateral_grid_blur.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_resolve.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/snow_accumulation.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_snow.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hiz_downsample.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hiz_culling.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_cull.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_tess.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.tesc.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water.tese.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_position.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_position.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_displacement.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/foam_blur.comp.spv
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
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_line.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/debug_line.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/loading.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/loading.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_shadow.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf_shadow.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf_shadow.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_geometry.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf_cull.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_filter.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_leaf_cull_phase3.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_cull.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_cell_cull.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_capture.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_capture.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_capture_leaf.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_shadow.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_impostor_shadow.frag.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_branch_shadow_cull.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/tree_branch_shadow_instanced.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_instanced.vert.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_compute.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_upsample_compute.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_spd.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/godrays_compute.comp.spv
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/calm_inference.comp.spv
    )

    # Source shaders used for UBO reflection (curated subset that defines complete UBO structures)
    set(UBO_REFLECT_SHADERS
        shaders/shader.vert
        shaders/shader.frag
        shaders/sky.frag
        shaders/grass.vert
        shaders/grass.comp
        shaders/postprocess.frag
        shaders/froxel_update.comp
        shaders/weather.comp
        shaders/leaf.comp
        shaders/terrain/terrain.vert
        shaders/terrain/terrain.frag
        shaders/terrain/terrain_subdivision.comp
        shaders/histogram_build.comp
        shaders/histogram_reduce.comp
        shaders/transmittance_lut.comp
    )

    # Derive .spv file paths from source shader list
    set(UBO_REFLECT_SPV_FILES "")
    foreach(SHADER ${UBO_REFLECT_SHADERS})
        list(APPEND UBO_REFLECT_SPV_FILES ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}.spv)
    endforeach()

    # Generate UBO headers from SPIR-V reflection
    # DEPENDS includes actual .spv files for proper rebuild when shaders change
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/generated/UBOs.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/generated
        COMMAND shader_reflect
            ${CMAKE_CURRENT_SOURCE_DIR}/generated/UBOs.h
            ${UBO_REFLECT_SPV_FILES}
        DEPENDS shader_reflect ${UBO_REFLECT_SPV_FILES}
        COMMENT "Generating UBO headers from SPIR-V reflection"
    )

    add_custom_target(generated_headers DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/generated/UBOs.h
    )

    add_dependencies(${PROJECT_NAME} shaders generated_headers)
else()
    message(WARNING "glslc/glslangValidator not found. Shaders must be compiled manually.")
endif()

endif() # SHADERS_AVAILABLE

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

# =============================================================================
# Unit Tests
# =============================================================================
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()
    find_package(doctest CONFIG REQUIRED)

    add_executable(vulkan_game_tests
        tests/test_main.cpp
        tests/test_celestial_calculator.cpp
        tests/test_terrain_height.cpp
        tests/test_animation.cpp
        tests/test_animation_blend.cpp
        tests/test_ik_utils.cpp
        tests/test_terrain_loaders.cpp
        tests/test_virtual_texture_types.cpp
        tests/test_virtual_texture_loader.cpp
        tests/test_tile_grid_logic.cpp
        tests/test_tile_composition.cpp
        tests/test_transform.cpp
        tests/test_breadcrumb_tracker.cpp
        tests/test_camera.cpp
        tests/test_deterministic_random.cpp
        tests/test_rotation_utils.cpp
        tests/test_kd_tree.cpp
        tests/test_flamegraph.cpp
        tests/test_player_state.cpp
        tests/test_motion_matching.cpp
        tests/test_mlp.cpp
        tests/test_calm_observation.cpp
        tests/test_calm_controller.cpp
        tests/test_calm_hlc_fsm.cpp
        tests/test_calm_model_loader.cpp
        # Source files needed by tests
        src/atmosphere/CelestialCalculator.cpp
        src/animation/Animation.cpp
        src/animation/AnimationBlend.cpp
        src/animation/MotionMatchingFeature.cpp
        src/animation/MotionMatchingKDTree.cpp
        src/animation/MotionMatchingTrajectory.cpp
        src/animation/MotionDatabase.cpp
        src/ik/IKUtils.cpp
        src/terrain/ErosionDataLoader.cpp
        src/terrain/RoadNetworkLoader.cpp
        src/terrain/virtual_texture/VirtualTextureTileLoader.cpp
        src/scene/Transform.cpp
        src/scene/Camera.cpp
        src/animation/AnimationBlend.cpp
        src/animation/MotionMatchingKDTree.cpp
        src/ml/MLPNetwork.cpp
        src/ml/ModelLoader.cpp
        src/ml/CALMCharacterConfig.cpp
        src/ml/CALMObservation.cpp
        src/ml/CALMActionApplier.cpp
        src/ml/CALMLatentSpace.cpp
        src/ml/CALMLowLevelController.cpp
        src/ml/CALMController.cpp
        src/ml/CALMHighLevelController.cpp
        src/ml/CALMBehaviorFSM.cpp
        src/ml/CALMAnimationIntegration.cpp
        src/ml/CALMModelLoader.cpp
    )

    target_include_directories(vulkan_game_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/loaders  # For GLTFLoader.h used by animation
        ${CMAKE_CURRENT_SOURCE_DIR}/src/animation # For AnimationBlend.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core     # For Mesh.h used by loaders
        ${CMAKE_CURRENT_SOURCE_DIR}/src/physics  # For CharacterController.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/terrain  # For TileGridLogic.h, ErosionDataLoader.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/terrain/virtual_texture  # For VirtualTextureTypes.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/water    # For WaterPlacementData.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/debug    # For Flamegraph.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ml       # For MLP inference
    )

    target_link_libraries(vulkan_game_tests PRIVATE
        doctest::doctest
        glm::glm
        Vulkan::Vulkan                           # For Vulkan types in Mesh.h, VK_FORMAT in DDSLoader
        GPUOpen::VulkanMemoryAllocator           # For vk_mem_alloc.h in Mesh.h
        nlohmann_json::nlohmann_json             # For GeoJSON parsing in terrain loaders
        SDL3::SDL3                               # For SDL_Log in terrain loaders
        lodepng                                  # For PNG loading in VirtualTextureTileLoader
    )

    target_compile_features(vulkan_game_tests PRIVATE cxx_std_17)

    # Define GLM_ENABLE_EXPERIMENTAL for GLM extensions used in IK tests
    target_compile_definitions(vulkan_game_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

    # Register tests with CTest
    add_test(NAME vulkan_game_tests COMMAND vulkan_game_tests)

    # =========================================================================
    # Integration tests: Motion matching with real FBX animation files
    # Separate executable because it needs GLTFLoader.cpp (real Skeleton methods)
    # which conflicts with the stubs in test_motion_matching.cpp
    # =========================================================================
    add_executable(motion_matching_integration_tests
        tests/test_motion_matching_integration.cpp
        # Loaders
        src/loaders/FBXLoader.cpp
        src/loaders/FBXPostProcess.cpp
        src/loaders/GLTFLoader.cpp
        # Animation
        src/animation/Animation.cpp
        src/animation/AnimationBlend.cpp
        src/animation/MotionMatchingFeature.cpp
        src/animation/MotionMatchingKDTree.cpp
        src/animation/MotionMatchingTrajectory.cpp
        src/animation/MotionDatabase.cpp
        src/animation/MotionMatchingController.cpp
        # Scene (for TransformHierarchy used by Skeleton)
        src/scene/Transform.cpp
    )

    target_include_directories(motion_matching_integration_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/loaders
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/animation
    )

    target_link_libraries(motion_matching_integration_tests PRIVATE
        doctest::doctest
        glm::glm
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        SDL3::SDL3
        fastgltf::fastgltf
        unoffical::openfbx::openfbx
    )

    target_compile_features(motion_matching_integration_tests PRIVATE cxx_std_17)
    target_compile_definitions(motion_matching_integration_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

    # Integration tests must run from project root (for asset paths)
    add_test(
        NAME motion_matching_integration_tests
        COMMAND motion_matching_integration_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # =========================================================================
    # Data-driven motion matching tests: trajectory-driven selection,
    # speed/direction discrimination, dance card scenarios, cost validation,
    # KD-tree correctness, normalization properties, locomotion transitions,
    # and regression tests using real FBX animation data.
    # =========================================================================
    add_executable(motion_matching_data_driven_tests
        tests/test_motion_matching_data_driven.cpp
        # Loaders
        src/loaders/FBXLoader.cpp
        src/loaders/FBXPostProcess.cpp
        src/loaders/GLTFLoader.cpp
        # Animation
        src/animation/Animation.cpp
        src/animation/AnimationBlend.cpp
        src/animation/MotionMatchingFeature.cpp
        src/animation/MotionMatchingKDTree.cpp
        src/animation/MotionMatchingTrajectory.cpp
        src/animation/MotionDatabase.cpp
        src/animation/MotionMatchingController.cpp
        # Scene (for TransformHierarchy used by Skeleton)
        src/scene/Transform.cpp
    )

    target_include_directories(motion_matching_data_driven_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/loaders
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/animation
    )

    target_link_libraries(motion_matching_data_driven_tests PRIVATE
        doctest::doctest
        glm::glm
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
        SDL3::SDL3
        fastgltf::fastgltf
        unoffical::openfbx::openfbx
    )

    target_compile_features(motion_matching_data_driven_tests PRIVATE cxx_std_17)
    target_compile_definitions(motion_matching_data_driven_tests PRIVATE GLM_ENABLE_EXPERIMENTAL)

    add_test(
        NAME motion_matching_data_driven_tests
        COMMAND motion_matching_data_driven_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Add a custom target to run tests
    add_custom_target(run_tests
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS vulkan_game_tests motion_matching_integration_tests motion_matching_data_driven_tests
        COMMENT "Running unit tests"
    )
endif()
