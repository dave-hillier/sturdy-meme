#include "Application.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include "core/vulkan/VulkanContext.h"
#include "core/LoadingRenderer.h"
#include "core/loading/LoadJobQueue.h"
#include "core/loading/LoadJobFactory.h"
#include "core/threading/TaskScheduler.h"
#include "InitProfiler.h"
#include "Profiler.h"

#include "TerrainSystem.h"
#include "TerrainTileCache.h"
#include "RockSystem.h"
#include "TreeSystem.h"
#include "DetritusSystem.h"
#include "SceneManager.h"
#include "WaterSystem.h"
#include "WindSystem.h"
#include "GuiInterfaces.h"
#include "core/RendererSystems.h"
#include "EnvironmentSettings.h"
#include "TimeSystem.h"
#include "core/interfaces/ITimeSystem.h"
#include "core/interfaces/IEnvironmentControl.h"
#include "core/interfaces/IDebugControl.h"
#include "core/interfaces/IWeatherState.h"
#include "core/interfaces/ITerrainControl.h"
#include "core/interfaces/IPlayerControl.h"

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

bool Application::init(const std::string& title, int width, int height) {
    // Reset and start init profiler
    InitProfiler::get().reset();

    {
        INIT_PROFILE_PHASE("SDL");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
            return false;
        }
    }

    // Initialize task scheduler early for multi-threaded operations
    {
        INIT_PROFILE_PHASE("TaskScheduler");
        TaskScheduler::instance().initialize();
    }

    // Early Vulkan initialization: create instance BEFORE window
    // This allows validation layers and dispatcher to start earlier
    std::unique_ptr<VulkanContext> vulkanContext;
    {
        INIT_PROFILE_PHASE("VulkanInstance");
        vulkanContext = std::make_unique<VulkanContext>();
        if (!vulkanContext->initInstance()) {
            SDL_Log("Failed to initialize Vulkan instance (early init)");
            SDL_Quit();
            return false;
        }
    }

    // InputSystem initializes itself in constructor (RAII)

    {
        INIT_PROFILE_PHASE("Window");
        window = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window) {
            SDL_Log("Failed to create window: %s", SDL_GetError());
            SDL_Quit();
            return false;
        }
    }

    std::string resourcePath = getResourcePath();

    // Complete Vulkan device initialization (surface, device, swapchain)
    // This must happen before LoadingRenderer can be created
    {
        INIT_PROFILE_PHASE("VulkanDevice");
        if (!vulkanContext->initDevice(window)) {
            SDL_Log("Failed to initialize Vulkan device");
            SDL_DestroyWindow(window);
            SDL_Quit();
            return false;
        }
    }

    // Create and show loading screen while assets load asynchronously
    {
        INIT_PROFILE_PHASE("LoadingScreen");
        LoadingRenderer::InitInfo loadingInfo{};
        loadingInfo.vulkanContext = vulkanContext.get();
        loadingInfo.shaderPath = resourcePath + "/shaders";

        auto loadingRenderer = LoadingRenderer::create(loadingInfo);
        if (loadingRenderer) {
            // Create async job queue with worker threads
            auto jobQueue = Loading::LoadJobQueue::create(2);  // 2 worker threads

            if (jobQueue) {
                // Queue texture loads to run in parallel during loading screen
                // These demonstrate the async loading pattern - resources are loaded
                // to CPU memory on background threads while loading screen animates
                std::vector<Loading::LoadJob> jobs;

                // Queue scene textures (priority 0 = highest)
                jobs.push_back(Loading::LoadJobFactory::createTextureJob(
                    "crate_diffuse", resourcePath + "/assets/textures/crates/crate1/crate1_diffuse.png", true, 0));
                jobs.push_back(Loading::LoadJobFactory::createTextureJob(
                    "crate_normal", resourcePath + "/assets/textures/crates/crate1/crate1_normal.png", false, 0));

                // Queue grass textures
                jobs.push_back(Loading::LoadJobFactory::createTextureJob(
                    "grass_diffuse", resourcePath + "/assets/textures/grass/grass/grass01.jpg", true, 1));
                jobs.push_back(Loading::LoadJobFactory::createTextureJob(
                    "grass_normal", resourcePath + "/assets/textures/grass/grass/grass01_n.jpg", false, 1));

                // Queue metal textures
                jobs.push_back(Loading::LoadJobFactory::createTextureJob(
                    "metal_diffuse", resourcePath + "/assets/textures/industrial/metal_1.jpg", true, 1));
                jobs.push_back(Loading::LoadJobFactory::createTextureJob(
                    "metal_normal", resourcePath + "/assets/textures/industrial/metal_1_norm.jpg", false, 1));

                // Queue rock textures
                jobs.push_back(Loading::LoadJobFactory::createTextureJob(
                    "rock_diffuse", resourcePath + "/assets/textures/industrial/concrete_1.jpg", true, 2));
                jobs.push_back(Loading::LoadJobFactory::createTextureJob(
                    "rock_normal", resourcePath + "/assets/textures/industrial/concrete_1_norm.jpg", false, 2));

                // Set total job count for progress tracking
                jobQueue->setTotalJobs(static_cast<uint32_t>(jobs.size()));
                jobQueue->submitBatch(std::move(jobs));

                // Run loading loop - renders loading screen while jobs complete
                while (!jobQueue->isComplete()) {
                    // Update progress display
                    Loading::LoadProgress progress = jobQueue->getProgress();
                    loadingRenderer->setProgress(progress.getProgress());

                    // Render loading screen frame
                    loadingRenderer->render();

                    // Process completed jobs (just consume them for now - Renderer
                    // will reload these textures during its init phase)
                    auto completed = jobQueue->getCompletedJobs();
                    for (auto& result : completed) {
                        if (result.success) {
                            SDL_Log("Async load complete: %s (%s)",
                                   result.jobId.c_str(), result.phase.c_str());
                        }
                    }

                    // Keep window responsive
                    SDL_PumpEvents();

                    // Small sleep to avoid spinning too fast
                    SDL_Delay(1);
                }

                SDL_Log("Async loading complete: %llu bytes loaded",
                       static_cast<unsigned long long>(jobQueue->getProgress().bytesLoaded));
            } else {
                // Fallback: just show loading screen for a bit
                for (int i = 0; i < 30; ++i) {
                    loadingRenderer->render();
                    SDL_PumpEvents();
                }
            }

            loadingRenderer->cleanup();
        } else {
            SDL_Log("Warning: LoadingRenderer creation failed, skipping loading screen");
        }
    }

    // Create full renderer (takes ownership of vulkanContext with device already initialized)
    Renderer::InitInfo rendererInfo{};
    rendererInfo.window = window;
    rendererInfo.resourcePath = resourcePath;
    rendererInfo.vulkanContext = std::move(vulkanContext);  // Transfer ownership
    renderer_ = Renderer::create(rendererInfo);
    if (!renderer_) {
        SDL_Log("Failed to initialize renderer");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    camera.setAspectRatio(static_cast<float>(width) / static_cast<float>(height));

    // Position camera at a settlement (Town 1: market town with coastal/agricultural features)
    // Settlement coords are 0-16384, world coords are centered (-8192 to +8192)
    {
        const auto& terrain = renderer_->getSystems().terrain();
        // Town 1 at settlement coords (9200, 3000) -> world coords (1008, -5192)
        const float settlementX = 9200.0f;
        const float settlementZ = 3000.0f;
        const float halfTerrain = 8192.0f;
        float cameraX = settlementX - halfTerrain;  // 1008
        float cameraZ = settlementZ - halfTerrain;  // -5192
        float terrainY = terrain.getHeightAt(cameraX, cameraZ);
        camera.setPosition(glm::vec3(cameraX, terrainY + 2.0f, cameraZ));  // Eye level above ground
        camera.setYaw(45.0f);    // Look roughly northeast
        camera.setPitch(0.0f);   // Level view
    }

    // Initialize physics system using RAII factory
    {
        INIT_PROFILE_PHASE("Physics");
        physics_ = PhysicsWorld::create();
        if (!physics_) {
            SDL_Log("Failed to initialize physics system");
            return false;
        }
    }

    // Create terrain hole at well entrance location
    // This must be done before terrain physics is initialized
    {
        auto& terrain = renderer_->getSystems().terrain();
        const auto& sceneBuilder = renderer_->getSystems().scene().getSceneBuilder();
        float wellX = sceneBuilder.getWellEntranceX();
        float wellZ = sceneBuilder.getWellEntranceZ();
        terrain.addHoleCircle(wellX, wellZ, SceneBuilder::WELL_HOLE_RADIUS);
        terrain.uploadHoleMaskToGPU();
        SDL_Log("Created terrain hole at well entrance (%.1f, %.1f) radius %.1f",
                wellX, wellZ, SceneBuilder::WELL_HOLE_RADIUS);
    }

    // Get terrain reference for spawning objects
    auto& terrain = renderer_->getSystems().terrain();

    // Initialize tiled physics terrain manager
    // Uses high-resolution terrain tiles (~1m spacing) within 1000m of player
    // instead of a single coarse heightfield (~32m spacing)
    {
        TerrainTileCache* tileCache = terrain.getTileCache();
        if (tileCache) {
            PhysicsTerrainTileManager::Config config;
            config.loadRadius = 1000.0f;
            config.unloadRadius = 1200.0f;
            config.maxTilesPerFrame = 2;
            config.terrainSize = terrain.getConfig().size;
            config.heightScale = terrain.getConfig().heightScale;

            if (physicsTerrainManager_.init(physics(), *tileCache, config)) {
                SDL_Log("Physics terrain tile manager initialized");
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize physics terrain tile manager!");
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Terrain tile cache not available for physics!");
        }
    }

    // Initialize scene physics (dynamic objects)
    renderer_->getSystems().scene().initPhysics(physics());

    // Create convex hull colliders for rocks using actual mesh geometry
    const auto& rockSystem = renderer_->getSystems().rock();
    const auto& rockInstances = rockSystem.getRockInstances();
    const auto& rockMeshes = rockSystem.getRockMeshes();

    for (const auto& rock : rockInstances) {
        // Rock position is adjusted down by 15% of scale in rendering
        glm::vec3 colliderPos = rock.position();
        colliderPos.y -= rock.scale() * 0.15f;

        // Get the mesh for this rock variation
        const Mesh& mesh = rockMeshes[rock.meshVariation];
        const auto& vertices = mesh.getVertices();

        // Extract just the positions from the vertex data
        std::vector<glm::vec3> positions;
        positions.reserve(vertices.size());
        for (const auto& v : vertices) {
            positions.push_back(v.position);
        }

        // Create convex hull from mesh vertices with rock's scale
        physics().createStaticConvexHull(colliderPos, positions.data(), positions.size(),
                                       rock.scale(), rock.rotation());
    }
    SDL_Log("Created %zu rock convex hull colliders", rockInstances.size());

    // Create convex hull colliders for fallen branches (detritus)
    if (const DetritusSystem* detritusSystem = renderer_->getSystems().detritus()) {
        const auto& detritusInstances = detritusSystem->getInstances();
        const auto& detritusMeshes = detritusSystem->getMeshes();

        for (const auto& detritus : detritusInstances) {
            // Get the mesh for this detritus variation
            const Mesh& mesh = detritusMeshes[detritus.meshVariation];
            const auto& vertices = mesh.getVertices();

            // Extract positions from vertex data
            std::vector<glm::vec3> positions;
            positions.reserve(vertices.size());
            for (const auto& v : vertices) {
                positions.push_back(v.position);
            }

            // Create convex hull from mesh vertices with detritus scale
            physics().createStaticConvexHull(detritus.position(), positions.data(), positions.size(),
                                           detritus.scale(), detritus.rotation());
        }
        SDL_Log("Created %zu detritus convex hull colliders", detritusInstances.size());
    }

    // Create compound capsule colliders for trees (trunk + major branches)
    if (TreeSystem* treeSystem = renderer_->getSystems().tree()) {
        const auto& treeInstances = treeSystem->getTreeInstances();
        TreeCollision::Config treeCollisionConfig;
        treeCollisionConfig.maxBranchLevel = 2;  // Trunk + first 2 levels of branches
        treeCollisionConfig.minBranchRadius = 0.05f;

        for (size_t i = 0; i < treeInstances.size(); ++i) {
            const auto& tree = treeInstances[i];
            auto capsules = treeSystem->getTreeCollisionCapsules(static_cast<uint32_t>(i), treeCollisionConfig);

            if (!capsules.empty()) {
                physics().createStaticCompoundCapsules(tree.position(), capsules, tree.rotation());
            }
        }
        SDL_Log("Created %zu tree compound capsule colliders", treeInstances.size());
    }

    // Create player entity and character controller
    // Spawn at Town 1 settlement location (same as camera and scene origin)
    const float halfTerrain = 8192.0f;
    const float settlementX = 9200.0f;  // Town 1 in 0-16384 space
    const float settlementZ = 3000.0f;
    float playerSpawnX = settlementX - halfTerrain;  // 1008
    float playerSpawnZ = settlementZ - halfTerrain;  // -5192
    float playerSpawnY = terrain.getHeightAt(playerSpawnX, playerSpawnZ) + 0.1f;

    // Debug: Sample terrain height at spawn position using different methods
    float heightFromTerrainSystem = terrain.getHeightAt(playerSpawnX, playerSpawnZ);
    float heightFromTileCache = 0.0f;
    bool tileHasHeight = false;
    if (auto* tileCachePtr = terrain.getTileCache()) {
        tileHasHeight = tileCachePtr->getHeightAt(playerSpawnX, playerSpawnZ, heightFromTileCache);
    }
    SDL_Log("DEBUG Height at spawn (%.1f, %.1f):", playerSpawnX, playerSpawnZ);
    SDL_Log("  TerrainSystem.getHeightAt(): %.2f", heightFromTerrainSystem);
    SDL_Log("  TileCache.getHeightAt(): %.2f (found=%d)", heightFromTileCache, tileHasHeight ? 1 : 0);
    SDL_Log("  Player spawn Y (height + 0.1): %.2f", playerSpawnY);

    // Initialize player state
    player_.transform = PlayerTransform::withYaw(glm::vec3(playerSpawnX, playerSpawnY, playerSpawnZ), 0.0f);
    player_.grounded = false;

    physics().createCharacter(glm::vec3(playerSpawnX, playerSpawnY, playerSpawnZ),
                              PlayerMovement::CAPSULE_HEIGHT, PlayerMovement::CAPSULE_RADIUS);

    SDL_Log("Physics initialized with %d active bodies", physics().getActiveBodyCount());

    // Configure breadcrumb tracker for safe respawn positions
    // Safety check: not in water, not in terrain holes
    breadcrumbTracker.setSafetyCheck([this](const glm::vec3& pos) {
        // Check if position is above water level (with margin)
        float waterLevel = renderer_->getSystems().water().getWaterLevel();
        if (pos.y < waterLevel + 0.5f) {
            return false;  // In or near water
        }
        // Check if position is in a terrain hole
        if (renderer_->getSystems().terrain().isHole(pos.x, pos.z)) {
            return false;  // In terrain hole (cave entrance, etc.)
        }
        return true;
    });
    breadcrumbTracker.setMinDistance(5.0f);  // Breadcrumb every 5 meters
    breadcrumbTracker.setMaxBreadcrumbs(200);  // Keep last 200 positions (~1km of travel)
    SDL_Log("Breadcrumb tracker configured for respawn optimization");

    // Initialize flag simulation
    initFlag();

    // Initialize GUI system via factory
    {
        INIT_PROFILE_PHASE("GUI");
        gui_ = GuiSystem::create(window, renderer_->getInstance(), renderer_->getPhysicalDevice(),
                                  renderer_->getDevice(), renderer_->getGraphicsQueueFamily(),
                                  renderer_->getGraphicsQueue(), renderer_->getSwapchainRenderPass(),
                                  renderer_->getSwapchainImageCount());
        if (!gui_) {
            SDL_Log("Failed to initialize GUI system");
            return false;
        }
    }

    // Set GUI render callback
    renderer_->setGuiRenderCallback([this](VkCommandBuffer cmd) {
        gui_->endFrame(cmd);
    });

    // Set up input system with GUI reference for input blocking
    input.setGuiSystem(gui_.get());
    input.setMoveSpeed(moveSpeed);

    // Finalize init profiler and log results
    InitProfiler::get().finalize();

    // Capture init timing to flamegraph
    renderer_->getSystems().profiler().captureInitFlamegraph();

    running = true;
    return true;
}

void Application::run() {
    auto lastTime = std::chrono::high_resolution_clock::now();
    float smoothedFps = 60.0f;

    while (running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // Store for GUI
        lastDeltaTime = deltaTime;
        if (deltaTime > 0.0f) {
            currentFps = currentFps * 0.95f + (1.0f / deltaTime) * 0.05f;
        }

        processEvents();

        // Begin GUI frame
        gui_->beginFrame();
        auto& systems = renderer_->getSystems();
        GuiInterfaces guiInterfaces{
            systems.time(),
            systems.locationControl(),
            systems.weatherState(),
            systems.environmentControl(),
            systems.postProcessState(),
            systems.cloudShadowControl(),
            systems.terrainControl(),
            systems.waterControl(),
            systems.treeControl(),
            systems.debugControl(),
            systems.profilerControl(),
            systems.performanceControl(),
            systems.sceneControl(),
            systems.playerControl(),
            systems.environmentSettings(),
            &physicsTerrainManager_
        };
        gui_->render(guiInterfaces, camera, lastDeltaTime, currentFps);

        // Update input system
        input.update(deltaTime, camera.getYaw());

        // Apply input to camera
        applyInputToCamera();

        // Process movement input for third-person mode
        glm::vec3 desiredVelocity(0.0f);
        auto& playerTransform = player_.transform;
        auto& playerMovement = player_.movement;

        if (input.isThirdPersonMode()) {
            // Handle orientation lock toggle
            if (input.wantsOrientationLockToggle()) {
                playerMovement.orientationLocked = !playerMovement.orientationLocked;
                if (playerMovement.orientationLocked) {
                    playerMovement.lockedYaw = playerTransform.getYaw();
                }
                SDL_Log("Orientation lock: %s", playerMovement.orientationLocked ? "ON" : "OFF");
            }

            // Temporarily lock orientation if holding trigger/middle mouse
            bool effectiveLock = playerMovement.orientationLocked || input.isOrientationLockHeld();

            glm::vec3 moveDir = input.getMovementDirection();
            if (glm::length(moveDir) > 0.001f) {
                moveDir = glm::normalize(moveDir);
                float currentSpeed = input.isSprinting() ? sprintSpeed : moveSpeed;
                desiredVelocity = moveDir * currentSpeed;

                // Only rotate player to face movement direction if not locked
                if (!effectiveLock) {
                    float newYaw = glm::degrees(atan2(moveDir.x, moveDir.z));
                    float currentYaw = playerTransform.getYaw();
                    float yawDiff = newYaw - currentYaw;
                    // Normalize yaw difference
                    while (yawDiff > 180.0f) yawDiff -= 360.0f;
                    while (yawDiff < -180.0f) yawDiff += 360.0f;
                    float smoothedYaw = currentYaw + yawDiff * 10.0f * deltaTime;  // Smooth rotation
                    // Keep yaw in reasonable range
                    while (smoothedYaw > 360.0f) smoothedYaw -= 360.0f;
                    while (smoothedYaw < 0.0f) smoothedYaw += 360.0f;
                    playerTransform.setYaw(smoothedYaw);
                }
            }
        }

        // Detect jump BEFORE physics update (character is still grounded when jump is requested)
        bool wasGrounded = physics().isCharacterOnGround();
        bool wantsJump = input.wantsJump();
        bool isJumping = wantsJump && wasGrounded;

        // If starting a jump, compute trajectory for animation sync
        if (isJumping) {
            glm::vec3 startPos = physics().getCharacterPosition();
            // Velocity: horizontal from input + jump impulse (5.0 m/s up, matching PhysicsSystem)
            glm::vec3 jumpVelocity = desiredVelocity;
            jumpVelocity.y = 5.0f;
            renderer_->getSystems().scene().getSceneBuilder().startCharacterJump(startPos, jumpVelocity, 9.81f, &physics());
        }

        // Always update physics character controller (handles gravity, jumping, and movement)
        physics().updateCharacter(deltaTime, desiredVelocity, wantsJump);

        // Update physics simulation
        physics().update(deltaTime);

        // Update physics terrain tiles based on player position
        glm::vec3 playerPos = physics().getCharacterPosition();
        physicsTerrainManager_.update(playerPos);

        // Sync player entity position from physics character controller
        glm::vec3 physicsVelocity = physics().getCharacterVelocity();
        playerTransform.position = playerPos;
        player_.grounded = physics().isCharacterOnGround();

        // Update breadcrumb tracker (Ghost of Tsushima respawn optimization)
        // Only track positions when player is grounded and not in water/hazards
        if (player_.grounded) {
            breadcrumbTracker.update(playerPos);
        }

        // Update scene object transforms from physics
        renderer_->getSystems().scene().update(physics());

        // Update player state in PlayerControlSubsystem for grass/snow/leaf interaction
        renderer_->getSystems().playerControl().setPlayerState(playerTransform.position, physicsVelocity, PlayerMovement::CAPSULE_RADIUS);

        // Wait for previous frame's GPU work to complete before updating dynamic meshes.
        // This prevents race conditions where we destroy mesh buffers while the GPU
        // is still reading them from the previous frame.
        renderer_->waitForPreviousFrame();

        // Update flag cloth simulation
        updateFlag(deltaTime);

        // Update animated character (skeletal animation)
        // Calculate movement speed from desired velocity for animation state machine
        float movementSpeed = glm::length(glm::vec2(desiredVelocity.x, desiredVelocity.z));
        bool isGrounded = physics().isCharacterOnGround();

        // Sync cape enabled state from GUI
        renderer_->getSystems().scene().getSceneBuilder().setCapeEnabled(gui_->getPlayerSettings().capeEnabled);

        renderer_->getSystems().scene().getSceneBuilder().updateAnimatedCharacter(
            deltaTime, renderer_->getVulkanContext().getAllocator(), renderer_->getVulkanContext().getVkDevice(),
            renderer_->getCommandPool(), renderer_->getVulkanContext().getVkGraphicsQueue(),
            movementSpeed, isGrounded, isJumping);

        // Update camera and player based on mode
        if (input.isThirdPersonMode()) {
            camera.setThirdPersonTarget(playerMovement.getFocusPoint(playerTransform.position));
            camera.updateThirdPerson(deltaTime);
            renderer_->getSystems().scene().updatePlayerTransform(playerMovement.getModelMatrix(playerTransform));

            // Dynamic FOV: widen during sprinting for sense of speed
            float targetFov = 45.0f;  // Base FOV
            if (input.isSprinting() && glm::length(desiredVelocity) > 0.1f) {
                targetFov = 55.0f;  // Sprint FOV
            }
            camera.setTargetFov(targetFov);

            // Update camera occlusion (fade objects between camera and player)
            updateCameraOcclusion(deltaTime);
        }

        camera.setAspectRatio(static_cast<float>(renderer_->getWidth()) / static_cast<float>(renderer_->getHeight()));

        // Update physics debug visualization (before render)
#ifdef JPH_DEBUG_RENDERER
        renderer_->updatePhysicsDebug(physics(), camera.getPosition());
#endif

        // Render frame - if skipped (window minimized/suspended), cancel GUI frame
        if (!renderer_->render(camera)) {
            gui_->cancelFrame();
        }

        // Update window title with FPS, time of day, and camera mode
        if (deltaTime > 0.0f) {
            smoothedFps = smoothedFps * 0.95f + (1.0f / deltaTime) * 0.05f;
        }
        float timeOfDay = systems.time().getTimeOfDay();
        int hours = static_cast<int>(timeOfDay * 24.0f);
        int minutes = static_cast<int>((timeOfDay * 24.0f - hours) * 60.0f);
        char title[96];
        const char* modeStr = input.isThirdPersonMode() ? "3rd Person" : "Free Cam";
        snprintf(title, sizeof(title), "Vulkan Game - FPS: %.0f | Time: %02d:%02d | %s (Tab to toggle)",
                 smoothedFps, hours, minutes, modeStr);
        SDL_SetWindowTitle(window, title);
    }

    renderer_->waitIdle();
}

void Application::shutdown() {
    renderer_->waitIdle();
    gui_.reset();  // RAII cleanup via destructor
    // InputSystem cleanup handled by destructor (RAII)
    physicsTerrainManager_.cleanup();
    physics_.reset();  // RAII cleanup via optional reset
    renderer_.reset();  // RAII cleanup via unique_ptr reset

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    // Shutdown task scheduler (waits for all tasks to complete)
    TaskScheduler::instance().shutdown();

    SDL_Quit();
}

void Application::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Pass events to GUI first
        gui_->processEvent(event);

        // Pass events to input system
        input.processEvent(event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                renderer_->notifyWindowResized();
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_HIDDEN:
            case SDL_EVENT_WINDOW_OCCLUDED:
                // Window minimized or hidden (e.g., macOS screen lock)
                SDL_Log("Window suspended");
                renderer_->notifyWindowSuspended();
                break;
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_SHOWN:
            case SDL_EVENT_WINDOW_EXPOSED:
                // Window restored (e.g., macOS screen unlock)
                if (renderer_->isWindowSuspended()) {
                    SDL_Log("Window restored, recreating swapchain");
                    renderer_->notifyWindowRestored();
                }
                break;
            case SDL_EVENT_KEY_DOWN: {
                auto& sys = renderer_->getSystems();
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                }
                else if (event.key.scancode == SDL_SCANCODE_F1) {
                    gui_->toggleVisibility();
                }
                else if (event.key.scancode == SDL_SCANCODE_1) {
                    sys.time().setTimeOfDay(0.25f);
                }
                else if (event.key.scancode == SDL_SCANCODE_2) {
                    sys.time().setTimeOfDay(0.5f);
                }
                else if (event.key.scancode == SDL_SCANCODE_3) {
                    sys.time().setTimeOfDay(0.75f);
                }
                else if (event.key.scancode == SDL_SCANCODE_4) {
                    sys.time().setTimeOfDay(0.0f);
                }
                else if (event.key.scancode == SDL_SCANCODE_EQUALS) {
                    sys.time().setTimeScale(sys.time().getTimeScale() * 2.0f);
                }
                else if (event.key.scancode == SDL_SCANCODE_MINUS) {
                    sys.time().setTimeScale(sys.time().getTimeScale() * 0.5f);
                }
                else if (event.key.scancode == SDL_SCANCODE_R) {
                    sys.time().resumeAutoTime();
                    sys.time().setTimeScale(1.0f);
                }
                else if (event.key.scancode == SDL_SCANCODE_6) {
                    sys.debugControl().toggleCascadeDebug();
                    SDL_Log("Cascade debug visualization: %s", sys.debugControl().isShowingCascadeDebug() ? "ON" : "OFF");
                }
                else if (event.key.scancode == SDL_SCANCODE_7) {
                    sys.debugControl().toggleSnowDepthDebug();
                    SDL_Log("Snow depth debug visualization: %s", sys.debugControl().isShowingSnowDepthDebug() ? "ON" : "OFF");
                }
                else if (event.key.scancode == SDL_SCANCODE_8) {
                    sys.debugControl().setHiZCullingEnabled(!sys.debugControl().isHiZCullingEnabled());
                    SDL_Log("Hi-Z occlusion culling: %s", sys.debugControl().isHiZCullingEnabled() ? "ON" : "OFF");
                }
                else if (event.key.scancode == SDL_SCANCODE_Z) {
                    float currentIntensity = sys.weatherState().getIntensity();
                    sys.weatherState().setIntensity(std::max(0.0f, currentIntensity - 0.1f));
                    SDL_Log("Weather intensity: %.1f", sys.weatherState().getIntensity());
                }
                else if (event.key.scancode == SDL_SCANCODE_X) {
                    float currentIntensity = sys.weatherState().getIntensity();
                    sys.weatherState().setIntensity(std::min(1.0f, currentIntensity + 0.1f));
                    SDL_Log("Weather intensity: %.1f", sys.weatherState().getIntensity());
                }
                else if (event.key.scancode == SDL_SCANCODE_C) {
                    uint32_t currentType = sys.weatherState().getWeatherType();
                    if (sys.weatherState().getIntensity() == 0.0f && currentType == 0) {
                        sys.weatherState().setWeatherType(0);
                        sys.weatherState().setIntensity(0.5f);
                    } else if (currentType == 0) {
                        sys.weatherState().setWeatherType(1);
                        sys.weatherState().setIntensity(0.5f);
                    } else if (currentType == 1) {
                        sys.weatherState().setWeatherType(0);
                        sys.weatherState().setIntensity(0.0f);
                    }

                    std::string weatherStatus = "Clear";
                    if (sys.weatherState().getIntensity() > 0.0f) {
                        if (sys.weatherState().getWeatherType() == 0) {
                            weatherStatus = "Rain";
                        } else if (sys.weatherState().getWeatherType() == 1) {
                            weatherStatus = "Snow";
                        }
                    }
                    SDL_Log("Weather type: %s, Intensity: %.1f", weatherStatus.c_str(), sys.weatherState().getIntensity());
                }
                else if (event.key.scancode == SDL_SCANCODE_F) {
                    glm::vec3 playerPos = player_.transform.position;
                    sys.environmentControl().spawnConfetti(playerPos, 8.0f, 100.0f, 0.5f);
                    SDL_Log("Confetti!");
                }
                else if (event.key.scancode == SDL_SCANCODE_V) {
                    sys.environmentControl().toggleCloudStyle();
                    SDL_Log("Cloud style: %s", sys.environmentControl().isUsingParaboloidClouds() ? "Paraboloid LUT Hybrid" : "Procedural");
                }
                else if (event.key.scancode == SDL_SCANCODE_LEFTBRACKET) {
                    float density = sys.environmentControl().getFogDensity();
                    sys.environmentControl().setFogDensity(std::max(0.0f, density - 0.0025f));
                    SDL_Log("Fog density: %.3f", sys.environmentControl().getFogDensity());
                }
                else if (event.key.scancode == SDL_SCANCODE_RIGHTBRACKET) {
                    float density = sys.environmentControl().getFogDensity();
                    sys.environmentControl().setFogDensity(std::min(0.2f, density + 0.0025f));
                    SDL_Log("Fog density: %.3f", sys.environmentControl().getFogDensity());
                }
                else if (event.key.scancode == SDL_SCANCODE_BACKSLASH) {
                    sys.environmentControl().setFogEnabled(!sys.environmentControl().isFogEnabled());
                    SDL_Log("Fog: %s", sys.environmentControl().isFogEnabled() ? "ON" : "OFF");
                }
                else if (event.key.scancode == SDL_SCANCODE_COMMA) {
                    float snow = sys.environmentSettings().snowAmount;
                    sys.environmentSettings().snowAmount = std::max(0.0f, snow - 0.1f);
                    SDL_Log("Snow amount: %.1f", sys.environmentSettings().snowAmount);
                }
                else if (event.key.scancode == SDL_SCANCODE_PERIOD) {
                    float snow = sys.environmentSettings().snowAmount;
                    sys.environmentSettings().snowAmount = std::min(1.0f, snow + 0.1f);
                    SDL_Log("Snow amount: %.1f", sys.environmentSettings().snowAmount);
                }
                else if (event.key.scancode == SDL_SCANCODE_SLASH) {
                    float snow = sys.environmentSettings().snowAmount;
                    sys.environmentSettings().snowAmount = (snow < 0.5f ? 1.0f : 0.0f);
                    SDL_Log("Snow amount: %.1f", sys.environmentSettings().snowAmount);
                }
                else if (event.key.scancode == SDL_SCANCODE_T) {
                    sys.terrainControl().toggleTerrainWireframe();
                    SDL_Log("Terrain wireframe: %s", sys.terrainControl().isTerrainWireframeMode() ? "ON" : "OFF");
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
                auto& time = renderer_->getSystems().time();
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                    time.setTimeOfDay(0.25f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
                    time.setTimeOfDay(0.5f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                    time.setTimeOfDay(0.75f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                    time.setTimeOfDay(0.0f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                    time.resumeAutoTime();
                    time.setTimeScale(1.0f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                    running = false;
                }
                break;
            }
            default:
                break;
        }
    }

    // Handle camera mode switch initialization
    if (input.wasModeSwitchedThisFrame()) {
        if (input.isThirdPersonMode()) {
            // Update player Y position to match terrain height (fixes spawn below terrain)
            auto& playerTransform = player_.transform;
            float terrainY = renderer_->getSystems().terrain().getHeightAt(
                playerTransform.position.x, playerTransform.position.z);
            float newY = terrainY + 0.1f;  // Slightly above terrain

            // Only update if significantly different (avoid jitter)
            if (std::abs(playerTransform.position.y - newY) > 1.0f) {
                playerTransform.position.y = newY;
                // Also update physics character position
                physics().setCharacterPosition(glm::vec3(
                    playerTransform.position.x, newY, playerTransform.position.z));
                SDL_Log("Player height corrected to %.2f (terrain=%.2f)", newY, terrainY);
            }

            // Initialize third-person camera from current free camera position
            // This ensures smooth transition instead of snapping to origin
            auto& playerMovement = player_.movement;
            camera.initializeThirdPersonFromCurrentPosition(playerMovement.getFocusPoint(playerTransform.position));
        } else {
            // Switching to free camera - just reset smoothing
            camera.resetSmoothing();
        }
    }
}

void Application::applyInputToCamera() {
    if (input.isThirdPersonMode()) {
        // Third-person: orbit camera around player
        camera.orbitYaw(input.getCameraYawInput());
        camera.orbitPitch(input.getCameraPitchInput());
        camera.adjustDistance(input.getCameraZoomInput());
    } else {
        // Free camera: direct movement and rotation
        camera.moveForward(input.getFreeCameraForward());
        camera.moveRight(input.getFreeCameraRight());
        camera.moveUp(input.getFreeCameraUp());
        camera.rotateYaw(input.getCameraYawInput());
        camera.rotatePitch(input.getCameraPitchInput());
    }

    // Handle gamepad time scale input
    float timeScaleInput = input.getTimeScaleInput();
    if (timeScaleInput != 0.0f) {
        auto& time = renderer_->getSystems().time();
        time.setTimeScale(time.getTimeScale() * timeScaleInput);
    }
}

std::string Application::getResourcePath() {
#ifdef __APPLE__
    CFBundleRef mainBundle = CFBundleGetMainBundle();
    if (mainBundle) {
        CFURLRef resourcesURL = CFBundleCopyResourcesDirectoryURL(mainBundle);
        if (resourcesURL) {
            char path[PATH_MAX];
            if (CFURLGetFileSystemRepresentation(resourcesURL, TRUE, (UInt8*)path, PATH_MAX)) {
                CFRelease(resourcesURL);
                return std::string(path);
            }
            CFRelease(resourcesURL);
        }
    }
    return ".";
#else
    return ".";
#endif
}

void Application::initPhysics() {
    physics_ = PhysicsWorld::create();
    if (!physics_) {
        SDL_Log("Failed to initialize physics system");
        return;
    }

    // Helper to get terrain height at a position
    auto getTerrainY = [this](float x, float z) -> float {
        return renderer_->getSystems().terrain().getHeightAt(x, z);
    };

    // Scene object layout from SceneBuilder (after multi-lights update):
    // 0: Ground disc (static terrain - already created above)
    // 1: Wooden crate 1 at (2.0, terrain+0.5, 0.0) - unit cube
    // 2: Rotated wooden crate at (-1.5, terrain+0.5, 1.0)
    // 3: Polished metal sphere at (0.0, terrain+0.5, -2.0) - radius 0.5
    // 4: Rough metal sphere at (-3.0, terrain+0.5, -1.0) - radius 0.5
    // 5: Polished metal cube at (3.0, terrain+0.5, -2.0)
    // 6: Brushed metal cube at (-3.0, terrain+0.5, -3.0)
    // 7: Emissive sphere at (2.0, terrain+1.3, 0.0) - scaled 0.3, visual radius 0.15
    // 8: Blue light at (-3.0, 2.0, 2.0) - fixed, no physics
    // 9: Green light at (4.0, 1.5, -2.0) - fixed, no physics
    // 10: Player capsule (handled by character controller)
    // 11: Flag pole at (5.0, terrain+1.5, 0.0) - static physics
    // 12: Flag cloth - no physics (soft body simulation handles it)

    const size_t numSceneObjects = 13;
    scenePhysicsBodies.resize(numSceneObjects, INVALID_BODY_ID);

    // Box half-extent for unit cube
    glm::vec3 cubeHalfExtents(0.5f, 0.5f, 0.5f);
    float boxMass = 10.0f;
    float sphereMass = 5.0f;

    // Spawn objects slightly above terrain to let them settle
    const float spawnOffset = 0.1f;

    // Index 1: Wooden crate 1
    float x1 = 2.0f, z1 = 0.0f;
    scenePhysicsBodies[1] = physics().createBox(glm::vec3(x1, getTerrainY(x1, z1) + 0.5f + spawnOffset, z1), cubeHalfExtents, boxMass);

    // Index 2: Rotated wooden crate
    float x2 = -1.5f, z2 = 1.0f;
    scenePhysicsBodies[2] = physics().createBox(glm::vec3(x2, getTerrainY(x2, z2) + 0.5f + spawnOffset, z2), cubeHalfExtents, boxMass);

    // Index 3: Polished metal sphere (mesh radius 0.5)
    float x3 = 0.0f, z3 = -2.0f;
    scenePhysicsBodies[3] = physics().createSphere(glm::vec3(x3, getTerrainY(x3, z3) + 0.5f + spawnOffset, z3), 0.5f, sphereMass);

    // Index 4: Rough metal sphere (mesh radius 0.5)
    float x4 = -3.0f, z4 = -1.0f;
    scenePhysicsBodies[4] = physics().createSphere(glm::vec3(x4, getTerrainY(x4, z4) + 0.5f + spawnOffset, z4), 0.5f, sphereMass);

    // Index 5: Polished metal cube
    float x5 = 3.0f, z5 = -2.0f;
    scenePhysicsBodies[5] = physics().createBox(glm::vec3(x5, getTerrainY(x5, z5) + 0.5f + spawnOffset, z5), cubeHalfExtents, boxMass);

    // Index 6: Brushed metal cube
    float x6 = -3.0f, z6 = -3.0f;
    scenePhysicsBodies[6] = physics().createBox(glm::vec3(x6, getTerrainY(x6, z6) + 0.5f + spawnOffset, z6), cubeHalfExtents, boxMass);

    // Index 7: Emissive sphere - mesh radius 0.5, scaled 0.3 = visual radius 0.15
    float x7 = 2.0f, z7 = 0.0f;
    scenePhysicsBodies[7] = physics().createSphere(glm::vec3(x7, getTerrainY(x7, z7) + 1.3f + spawnOffset, z7), 0.5f * 0.3f, 1.0f);

    // Index 8 & 9: Blue and green lights - NO PHYSICS (fixed light indicators)
    // scenePhysicsBodies[8] and [9] remain INVALID_BODY_ID

    // Index 11: Flag pole - static cylinder (radius 0.05m, height 3m)
    // Create as a static box approximation for simplicity
    float x11 = 5.0f, z11 = 0.0f;
    glm::vec3 poleHalfExtents(0.05f, 1.5f, 0.05f);  // Half of 3m height
    scenePhysicsBodies[11] = physics().createStaticBox(glm::vec3(x11, getTerrainY(x11, z11) + 1.5f, z11), poleHalfExtents);

    // Index 12: Flag cloth - NO PHYSICS (soft body simulation)
    // scenePhysicsBodies[12] remains INVALID_BODY_ID

    // Create character controller for player at terrain height
    float playerX = 0.0f, playerZ = 0.0f;
    float playerTerrainY = getTerrainY(playerX, playerZ);
    physics().createCharacter(glm::vec3(playerX, playerTerrainY + spawnOffset, playerZ),
                              PlayerMovement::CAPSULE_HEIGHT, PlayerMovement::CAPSULE_RADIUS);

    SDL_Log("Physics initialized with %d active bodies", physics().getActiveBodyCount());
}

void Application::updatePhysicsToScene() {
    // Update scene object transforms from physics simulation
    auto& sceneObjects = renderer_->getSystems().scene().getRenderables();

    for (size_t i = 1; i < scenePhysicsBodies.size() && i < sceneObjects.size(); i++) {
        PhysicsBodyID bodyID = scenePhysicsBodies[i];
        if (bodyID == INVALID_BODY_ID) continue;

        // Skip player object (handled separately)
        if (i == renderer_->getSystems().scene().getPlayerObjectIndex()) continue;

        // Get transform from physics (position and rotation only)
        glm::mat4 physicsTransform = physics().getBodyTransform(bodyID);

        // Extract scale from current transform to preserve it
        glm::vec3 scale;
        scale.x = glm::length(glm::vec3(sceneObjects[i].transform[0]));
        scale.y = glm::length(glm::vec3(sceneObjects[i].transform[1]));
        scale.z = glm::length(glm::vec3(sceneObjects[i].transform[2]));

        // Apply scale to physics transform
        physicsTransform = glm::scale(physicsTransform, scale);

        // Update scene object transform
        sceneObjects[i].transform = physicsTransform;

        // Update orb light position to follow the emissive sphere (index 7)
        if (i == 7) {
            glm::vec3 orbPosition = glm::vec3(physicsTransform[3]);
            renderer_->getSystems().scene().setOrbLightPosition(orbPosition);
        }
    }
}

void Application::initFlag() {
    // Create cloth simulation: 20x15 grid, 0.15m spacing
    const int clothWidth = 20;
    const int clothHeight = 15;
    const float particleSpacing = 0.15f;

    // Position the cloth at the top of the pole
    // Flag pole is at (5, 0) with center at terrain + 1.5m (3m tall pole)
    // Top of pole is at terrain height + 1.5 + 1.5 = terrain + 3.0
    const float flagPoleX = 5.0f;
    const float flagPoleZ = 0.0f;
    float terrainHeight = renderer_->getSystems().terrain().getHeightAt(flagPoleX, flagPoleZ);
    float poleTopY = terrainHeight + 3.0f;  // Pole center is 1.5m above terrain, pole is 3m tall
    glm::vec3 clothTopLeft(flagPoleX - 0.1f, poleTopY, flagPoleZ);  // Slightly to the left of pole center

    clothSim.create(clothWidth, clothHeight, particleSpacing, clothTopLeft);

    // Pin the left edge of the cloth to the pole
    for (int y = 0; y < clothHeight; ++y) {
        clothSim.pinParticle(0, y);  // Pin left edge
    }

    // Create initial mesh geometry and upload to GPU
    auto& sceneBuilder = renderer_->getSystems().scene().getSceneBuilder();
    clothSim.createMesh(sceneBuilder.getFlagClothMesh());
    sceneBuilder.uploadFlagClothMesh(
        renderer_->getVulkanContext().getAllocator(), renderer_->getVulkanContext().getVkDevice(),
        renderer_->getCommandPool(), renderer_->getVulkanContext().getVkGraphicsQueue());

    SDL_Log("Flag initialized with %dx%d cloth simulation", clothWidth, clothHeight);
}

void Application::updateCameraOcclusion(float deltaTime) {
    // Only in third-person mode
    if (!input.isThirdPersonMode()) return;

    // Raycast from player focus point to camera position
    const auto& playerTransform = player_.transform;
    const auto& playerMovement = player_.movement;
    glm::vec3 playerFocus = playerMovement.getFocusPoint(playerTransform.position);
    glm::vec3 cameraPos = camera.getPosition();

    std::vector<RaycastHit> hits = physics().castRayAllHits(playerFocus, cameraPos);

    // If there are any hits, apply camera collision to pull camera closer
    if (!hits.empty()) {
        // Use the closest hit for camera collision
        float closestHitDistance = hits[0].distance;
        camera.applyCollisionDistance(closestHitDistance);
    }

    // Build set of currently occluding body IDs (for opacity fading)
    std::unordered_set<PhysicsBodyID> currentlyOccluding;
    for (const auto& hit : hits) {
        currentlyOccluding.insert(hit.bodyId);
    }

    // Update opacities for all scene objects
    auto& sceneObjects = renderer_->getSystems().scene().getRenderables();
    for (size_t i = 0; i < scenePhysicsBodies.size() && i < sceneObjects.size(); i++) {
        PhysicsBodyID bodyID = scenePhysicsBodies[i];
        if (bodyID == INVALID_BODY_ID) continue;

        // Skip player (handled separately)
        if (i == renderer_->getSystems().scene().getPlayerObjectIndex()) continue;

        bool isOccluding = currentlyOccluding.count(bodyID) > 0;
        float targetOpacity = isOccluding ? occludedOpacity : 1.0f;

        // Smooth fade
        float fadeFactor = 1.0f - std::exp(-occlusionFadeSpeed * deltaTime);
        sceneObjects[i].opacity += (targetOpacity - sceneObjects[i].opacity) * fadeFactor;
    }

    // Update tracking set
    occludingBodies = std::move(currentlyOccluding);
}

void Application::updateFlag(float deltaTime) {
    // Clear previous frame collisions
    clothSim.clearCollisions();

    // Add player collision sphere
    glm::vec3 playerPos = physics().getCharacterPosition();
    float playerRadius = PlayerMovement::CAPSULE_RADIUS;
    float playerHeight = PlayerMovement::CAPSULE_HEIGHT;

    // Add collision spheres for the player capsule (one at bottom, middle, and top)
    clothSim.addSphereCollision(playerPos + glm::vec3(0, playerRadius, 0), playerRadius);
    clothSim.addSphereCollision(playerPos + glm::vec3(0, playerHeight * 0.5f, 0), playerRadius);
    clothSim.addSphereCollision(playerPos + glm::vec3(0, playerHeight - playerRadius, 0), playerRadius);

    // Add collision spheres for dynamic physics objects
    auto& sceneObjects = renderer_->getSystems().scene().getRenderables();
    for (size_t i = 1; i < scenePhysicsBodies.size() && i < sceneObjects.size(); i++) {
        PhysicsBodyID bodyID = scenePhysicsBodies[i];
        if (bodyID == INVALID_BODY_ID) continue;
        if (i == renderer_->getSystems().scene().getPlayerObjectIndex()) continue; // Skip player (already handled)
        if (i == 11 || i == 12) continue; // Skip flag pole and cloth itself

        PhysicsBodyInfo info = physics().getBodyInfo(bodyID);

        // Add approximate collision spheres for physics objects
        // For simplicity, use a sphere of radius 0.5 for all objects
        clothSim.addSphereCollision(info.position, 0.5f);
    }

    // Update cloth simulation with wind
    clothSim.update(deltaTime, &renderer_->getSystems().wind());

    // Update the mesh vertices from cloth particles and re-upload to GPU
    auto& flagSceneBuilder = renderer_->getSystems().scene().getSceneBuilder();
    clothSim.updateMesh(flagSceneBuilder.getFlagClothMesh());
    flagSceneBuilder.uploadFlagClothMesh(
        renderer_->getVulkanContext().getAllocator(), renderer_->getVulkanContext().getVkDevice(),
        renderer_->getCommandPool(), renderer_->getVulkanContext().getVkGraphicsQueue());
}
