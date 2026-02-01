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
#include "ScatterSystem.h"
#include "TreeSystem.h"
#include "TreeCollision.h"
#include "DeferredTerrainObjects.h"
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
#include "DebugLineSystem.h"
#include "npc/NPCSimulation.h"
#include "Texture.h"

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

    // Create loading screen renderer - kept alive during full renderer initialization
    std::unique_ptr<LoadingRenderer> loadingRenderer;
    {
        INIT_PROFILE_PHASE("LoadingScreen");
        LoadingRenderer::InitInfo loadingInfo{};
        loadingInfo.vulkanContext = vulkanContext.get();
        loadingInfo.shaderPath = resourcePath + "/shaders";

        loadingRenderer = LoadingRenderer::create(loadingInfo);
        if (loadingRenderer) {
            // Show initial loading screen while we start initialization
            loadingRenderer->setProgress(0.0f);
            loadingRenderer->render();
            SDL_PumpEvents();
        } else {
            SDL_Log("Warning: LoadingRenderer creation failed, initialization will proceed without visual feedback");
        }
    }

    // Create full renderer with progress callback to update loading screen
    // The loading screen stays visible and animated during subsystem initialization
    Renderer::InitInfo rendererInfo{};
    rendererInfo.window = window;
    rendererInfo.resourcePath = resourcePath;
    rendererInfo.vulkanContext = std::move(vulkanContext);  // Transfer ownership
    rendererInfo.asyncInit = true;  // Enable async subsystem loading

    // Progress callback renders loading screen during initialization
    if (loadingRenderer) {
        rendererInfo.progressCallback = [&loadingRenderer](float progress, const char* phase) {
            loadingRenderer->setProgress(progress);
            loadingRenderer->render();
            SDL_PumpEvents();
            SDL_Delay(1);  // Small yield to keep window responsive
        };
    }

    renderer_ = Renderer::create(rendererInfo);

    // If async init is enabled, poll for completion while rendering loading screen
    if (renderer_ && !renderer_->isAsyncInitComplete()) {
        SDL_Log("Async initialization started, running loading loop...");

        while (!renderer_->pollAsyncInit()) {
            // Render loading screen
            if (loadingRenderer) {
                loadingRenderer->render();
            }

            // Keep window responsive
            SDL_PumpEvents();

            // Check for quit events during loading
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    SDL_Log("Quit requested during loading");
                    if (loadingRenderer) {
                        loadingRenderer->cleanup();
                    }
                    return false;
                }
            }

            SDL_Delay(1);  // Small yield
        }

        SDL_Log("Async initialization complete");
    }

    // Cleanup loading renderer now that full renderer is ready
    if (loadingRenderer) {
        loadingRenderer->cleanup();
        loadingRenderer.reset();
    }

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

                // Pre-load physics terrain tiles around scene origin (where objects are placed)
                // Scene is at Town 1: settlement coords (9200, 3000) -> world coords (1008, -5192)
                const float halfTerrain = 8192.0f;
                glm::vec3 sceneSpawnPos(9200.0f - halfTerrain, 0.0f, 3000.0f - halfTerrain);
                for (int i = 0; i < 50; i++) {  // Load up to 50 tiles synchronously
                    physicsTerrainManager_.update(sceneSpawnPos);
                }
                SDL_Log("Pre-loaded physics terrain tiles around scene origin (%.0f, %.0f)",
                        sceneSpawnPos.x, sceneSpawnPos.z);
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
    const auto& rockSystem = renderer_->getSystems().rocks();
    const auto& rockInstances = rockSystem.getInstances();
    const auto& rockMeshes = rockSystem.getMeshes();

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
    if (const ScatterSystem* detritusSystem = renderer_->getSystems().detritus()) {
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
    // Track how many trees currently have colliders for deferred generation
    size_t treesWithColliders = 0;
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
        treesWithColliders = treeInstances.size();
        SDL_Log("Created %zu tree compound capsule colliders", treeInstances.size());
    }

    // Register callback for deferred tree generation (forest/woods trees)
    // These trees are generated after terrain is ready, so we need to create colliders then
    if (auto* deferred = renderer_->getSystems().deferredTerrainObjects()) {
        deferred->setOnTreesGeneratedCallback([this, treesWithColliders](TreeSystem& treeSystem) {
            const auto& treeInstances = treeSystem.getTreeInstances();

            // Only create colliders for trees added after initial setup
            if (treeInstances.size() <= treesWithColliders) {
                return;  // No new trees to process
            }

            TreeCollision::Config treeCollisionConfig;
            treeCollisionConfig.maxBranchLevel = 2;
            treeCollisionConfig.minBranchRadius = 0.05f;

            size_t newColliders = 0;
            for (size_t i = treesWithColliders; i < treeInstances.size(); ++i) {
                const auto& tree = treeInstances[i];
                auto capsules = treeSystem.getTreeCollisionCapsules(static_cast<uint32_t>(i), treeCollisionConfig);

                if (!capsules.empty()) {
                    physics().createStaticCompoundCapsules(tree.position(), capsules, tree.rotation());
                    ++newColliders;
                }
            }
            SDL_Log("Created %zu tree colliders for deferred forest generation", newColliders);
        });
    }

    // Create player entity and character controller
    // Spawn at Town 1 settlement location (same as camera and scene origin)
    const float halfTerrain = 8192.0f;
    const float settlementX = 9200.0f;  // Town 1 in 0-16384 space
    const float settlementZ = 3000.0f;
    float playerSpawnX = settlementX - halfTerrain;  // 1008
    float playerSpawnZ = settlementZ - halfTerrain;  // -5192

    // Pre-load high-res tiles around spawn before querying height
    // This ensures we get LOD0 height data instead of low-res base LOD fallback
    if (auto* tileCachePtr = terrain.getTileCache()) {
        tileCachePtr->preloadTilesAround(playerSpawnX, playerSpawnZ, 600.0f);
    }

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

    // Initialize ECS world with scene entities
    initECS();

    // Wire up ECS world for lighting
    renderer_->setECSWorld(&ecsWorld_);
    renderer_->getSystems().scene().setECSWorld(&ecsWorld_);
    renderer_->getSystems().scene().initializeECSLights();

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
            systems.grassControl(),
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

            // Get facing mode settings
            auto& playerSettings = gui_->getPlayerSettings();
            FacingMode facingMode = playerSettings.facingMode;
            bool guiStrafeEnabled = (facingMode != FacingMode::FollowMovement);

            // Handle FollowTarget mode - place target if not set
            if (facingMode == FacingMode::FollowTarget && !playerSettings.hasTarget) {
                // Place target 5m in front of player
                glm::vec3 forward = playerTransform.getForward();
                playerSettings.targetPosition = playerTransform.position + forward * 5.0f;
                playerSettings.hasTarget = true;
                SDL_Log("Target placed at (%.1f, %.1f, %.1f)",
                    playerSettings.targetPosition.x,
                    playerSettings.targetPosition.y,
                    playerSettings.targetPosition.z);
            }

            glm::vec3 moveDir = input.getMovementDirection();
            if (glm::length(moveDir) > 0.001f) {
                moveDir = glm::normalize(moveDir);
                float currentSpeed = input.isSprinting() ? sprintSpeed : moveSpeed;
                desiredVelocity = moveDir * currentSpeed;

                // Only rotate player to face movement direction if not locked and not in GUI strafe mode
                if (!effectiveLock && !guiStrafeEnabled) {
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

            // Handle strafe/lock-on facing modes
            if (guiStrafeEnabled) {
                glm::vec3 targetDir;
                if (facingMode == FacingMode::FollowCamera) {
                    // Face camera direction
                    targetDir = camera.getForward();
                } else if (facingMode == FacingMode::FollowTarget && playerSettings.hasTarget) {
                    // Face target position
                    targetDir = playerSettings.targetPosition - playerTransform.position;
                } else {
                    targetDir = playerTransform.getForward();
                }

                targetDir.y = 0.0f;
                if (glm::length(targetDir) > 0.001f) {
                    targetDir = glm::normalize(targetDir);
                    float targetYaw = glm::degrees(atan2(targetDir.x, targetDir.z));
                    float currentYaw = playerTransform.getYaw();
                    float yawDiff = targetYaw - currentYaw;
                    while (yawDiff > 180.0f) yawDiff -= 360.0f;
                    while (yawDiff < -180.0f) yawDiff += 360.0f;
                    // Faster rotation for strafe mode responsiveness
                    float smoothedYaw = currentYaw + yawDiff * 15.0f * deltaTime;
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

        // Update ECS systems (visibility culling, LOD)
        updateECS(deltaTime);

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

        // Sync settings from GUI
        renderer_->getSystems().scene().getSceneBuilder().setCapeEnabled(gui_->getPlayerSettings().capeEnabled);
        renderer_->getSystems().scene().getSceneBuilder().setShowSword(gui_->getPlayerSettings().showSword);
        renderer_->getSystems().scene().getSceneBuilder().setShowShield(gui_->getPlayerSettings().showShield);
        renderer_->getSystems().scene().getSceneBuilder().setShowWeaponAxes(gui_->getPlayerSettings().showWeaponAxes);

        // Pass motion matching parameters: position, facing direction, and input direction
        glm::vec3 inputDirection = glm::vec3(desiredVelocity.x, 0.0f, desiredVelocity.z);
        glm::vec3 facingDirection = playerTransform.getForward();

        // Determine strafe mode (GUI-enabled or orientation lock is active)
        auto& settings = gui_->getPlayerSettings();
        bool strafeMode = (settings.facingMode != FacingMode::FollowMovement) ||
            (input.isThirdPersonMode() &&
             (playerMovement.orientationLocked || input.isOrientationLockHeld()));

        // Get facing direction for strafe mode
        glm::vec3 strafeFacingDirection;
        if (settings.facingMode == FacingMode::FollowTarget && settings.hasTarget) {
            // Face toward target
            strafeFacingDirection = settings.targetPosition - playerTransform.position;
        } else {
            // Face camera direction
            strafeFacingDirection = camera.getForward();
        }
        strafeFacingDirection.y = 0.0f;  // Horizontal only
        if (glm::length(strafeFacingDirection) > 0.001f) {
            strafeFacingDirection = glm::normalize(strafeFacingDirection);
        } else {
            strafeFacingDirection = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        renderer_->getSystems().scene().getSceneBuilder().updateAnimatedCharacter(
            deltaTime, renderer_->getVulkanContext().getAllocator(), renderer_->getVulkanContext().getVkDevice(),
            renderer_->getCommandPool(), renderer_->getVulkanContext().getVkGraphicsQueue(),
            movementSpeed, isGrounded, isJumping,
            playerTransform.position, facingDirection, inputDirection,
            strafeMode, strafeFacingDirection);

        // Draw debug target indicator when in FollowTarget mode
        if (settings.facingMode == FacingMode::FollowTarget && settings.hasTarget) {
            auto& debugLines = renderer_->getSystems().debugControl().getDebugLineSystem();
            glm::vec3 targetPos = settings.targetPosition;

            // Draw a small sphere at target position
            debugLines.addSphere(targetPos, 0.3f, glm::vec4(1.0f, 0.3f, 0.3f, 1.0f), 12);

            // Draw a vertical line to make it more visible
            debugLines.addLine(targetPos, targetPos + glm::vec3(0.0f, 2.0f, 0.0f),
                               glm::vec4(1.0f, 0.3f, 0.3f, 1.0f));

            // Draw line from player to target
            debugLines.addLine(playerTransform.position + glm::vec3(0.0f, 1.0f, 0.0f),
                               targetPos + glm::vec3(0.0f, 1.0f, 0.0f),
                               glm::vec4(1.0f, 0.5f, 0.0f, 0.5f));
        }

        // Update NPC animations with LOD based on camera position
        renderer_->getSystems().scene().getSceneBuilder().updateNPCs(
            deltaTime, camera.getPosition());

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
                else if (event.key.scancode == SDL_SCANCODE_9) {
                    // Terrain height diagnostic - compare CPU height vs physics raycast
                    // Samples on a grid at integer positions (should align with physics samples)
                    auto& debugLines = sys.debugControl().getDebugLineSystem();
                    debugLines.clearPersistentLines();

                    // Use scene origin (where objects are placed) as center for diagnostic
                    // Scene is at Town 1: settlement coords (9200, 3000) -> world coords (1008, -5192)
                    const float halfTerrain = 8192.0f;
                    float centerX = 9200.0f - halfTerrain;  // 1008
                    float centerZ = 3000.0f - halfTerrain;  // -5192

                    int gridSize = 5;  // 5x5 grid of samples at 1m spacing
                    float rayStartY = 500.0f;

                    SDL_Log("=== Terrain Height Diagnostic (center=%.0f,%.0f grid=%dx%d) ===",
                            centerX, centerZ, gridSize, gridSize);
                    SDL_Log("Format: (x,z) cpu=H phys=H diff=D [tile info] hits=N");

                    for (int gz = -gridSize/2; gz <= gridSize/2; gz++) {
                        for (int gx = -gridSize/2; gx <= gridSize/2; gx++) {
                            float x = centerX + gx;
                            float z = centerZ + gz;

                            // CPU height with debug info
                            auto cpuInfo = sys.terrain().getHeightAtDebug(x, z);
                            float cpuH = cpuInfo.height;

                            // Physics height via raycast - get ALL hits to see overlapping tiles
                            glm::vec3 rayFrom(x, rayStartY, z);
                            glm::vec3 rayTo(x, -100.0f, z);
                            auto hits = physics().castRayAllHits(rayFrom, rayTo);

                            // Sort by Y to get highest hit
                            float physicsH = cpuH;  // Default if no hit
                            bool hasPhysicsHit = false;
                            size_t numHits = hits.size();

                            // Log all hits if multiple (indicates overlapping tiles)
                            if (numHits > 1) {
                                SDL_Log("  (%.0f, %.0f) MULTIPLE HITS (%zu):", x, z, numHits);
                                for (size_t i = 0; i < numHits; i++) {
                                    SDL_Log("    hit[%zu] y=%.3f bodyId=%u",
                                            i, hits[i].position.y, hits[i].bodyId);
                                }
                            }

                            for (const auto& hit : hits) {
                                if (hit.hit && hit.position.y > physicsH - 50.0f) {
                                    physicsH = hit.position.y;
                                    hasPhysicsHit = true;
                                    break;
                                }
                            }

                            float diff = physicsH - cpuH;
                            SDL_Log("  (%.0f, %.0f) cpu=%.3f phys=%.3f diff=%.4f [%s LOD%u tile(%d,%d)] hits=%zu%s",
                                    x, z, cpuH, physicsH, diff,
                                    cpuInfo.source, cpuInfo.lod, cpuInfo.tileX, cpuInfo.tileZ,
                                    numHits, hasPhysicsHit ? "" : " (no hit)");

                            // Add debug sphere at CPU height (green)
                            debugLines.addSphere(glm::vec3(x, cpuH, z), 0.3f, glm::vec4(0.0f, 1.0f, 0.0f, 1.0f), 8);

                            // Add debug sphere at physics height (red) - only if different
                            if (std::abs(diff) > 0.001f && hasPhysicsHit) {
                                debugLines.addSphere(glm::vec3(x, physicsH, z), 0.25f, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), 8);
                            }
                        }
                    }

                    SDL_Log("=== End Diagnostic (Green=CPU, Red=Physics) ===");
                    SDL_Log("Press 9 again to re-run, spheres visible in debug mode");
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

    // Update opacities using ECS queries - entities with PhysicsBody and Opacity components
    auto& sceneObjects = renderer_->getSystems().scene().getRenderables();

    // Query entities with PhysicsBody component (excludes player via PlayerTag check)
    for (auto [entity, physicsBody] : ecsWorld_.view<ecs::PhysicsBody>().each()) {
        // Skip player entity
        if (ecsWorld_.has<ecs::PlayerTag>(entity)) continue;

        PhysicsBodyID bodyID = static_cast<PhysicsBodyID>(physicsBody.bodyId);
        if (bodyID == INVALID_BODY_ID) continue;

        bool isOccluding = currentlyOccluding.count(bodyID) > 0;
        float targetOpacity = isOccluding ? occludedOpacity : 1.0f;

        // Find the renderable index for this entity and update opacity
        const auto& sceneEntities = sceneBuilder.getSceneEntities();
        for (size_t i = 0; i < sceneEntities.size(); ++i) {
            if (sceneEntities[i] == entity && i < sceneObjects.size()) {
                float fadeFactor = 1.0f - std::exp(-occlusionFadeSpeed * deltaTime);
                sceneObjects[i].opacity += (targetOpacity - sceneObjects[i].opacity) * fadeFactor;
                break;
            }
        }
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

    // Add collision spheres for dynamic physics objects using ECS query
    for (auto [entity, physicsBody] : ecsWorld_.view<ecs::PhysicsBody>().each()) {
        // Skip player, flag pole, and flag cloth
        if (ecsWorld_.has<ecs::PlayerTag>(entity)) continue;
        if (ecsWorld_.has<ecs::FlagPoleTag>(entity)) continue;
        if (ecsWorld_.has<ecs::FlagClothTag>(entity)) continue;

        PhysicsBodyID bodyID = static_cast<PhysicsBodyID>(physicsBody.bodyId);
        if (bodyID == INVALID_BODY_ID) continue;

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

void Application::initECS() {
    INIT_PROFILE_PHASE("ECS");

    auto& sceneManager = renderer_->getSystems().scene();
    const auto& renderables = sceneManager.getRenderables();
    SceneBuilder& sceneBuilder = sceneManager.getSceneBuilder();

    // Phase 6: Let SceneBuilder create ECS entities directly
    sceneBuilder.setECSWorld(&ecsWorld_);
    sceneBuilder.createEntitiesFromRenderables();

    // Get entity references from SceneBuilder
    const auto& sceneEntities = sceneBuilder.getSceneEntities();

    // Link physics bodies from SceneManager to ECS entities
    const auto& physicsBodies = sceneManager.getPhysicsBodies();
    for (size_t i = 0; i < physicsBodies.size() && i < sceneEntities.size(); ++i) {
        PhysicsBodyID bodyId = physicsBodies[i];
        if (bodyId != INVALID_BODY_ID) {
            ecsWorld_.add<ecs::PhysicsBody>(sceneEntities[i], static_cast<ecs::PhysicsBodyId>(bodyId));
        }
    }

    // Phase 6: Connect ECS world to renderer systems for direct entity queries
    renderer_->getSystems().setECSWorld(&ecsWorld_);

    SDL_Log("ECS initialized with %zu entities from scene", sceneEntities.size());

    // Initialize ECS Material Demo to showcase material components
    if (!renderables.empty()) {
        ecs::ECSMaterialDemo::InitInfo demoInfo{};
        demoInfo.world = &ecsWorld_;
        demoInfo.cubeMesh = sceneBuilder.getCubeMesh();
        demoInfo.sphereMesh = sceneBuilder.getSphereMesh();
        demoInfo.metalTexture = const_cast<Texture*>(sceneBuilder.getMetalTexture());
        demoInfo.crateTexture = const_cast<Texture*>(sceneBuilder.getCrateTexture());
        demoInfo.materialRegistry = &sceneManager.getSceneBuilder().getMaterialRegistry();
        demoInfo.sceneOrigin = glm::vec2(
            sceneBuilder.getWellEntranceX() - 20.0f,
            sceneBuilder.getWellEntranceZ() - 20.0f
        );

        // Use terrain height function if available from renderer
        auto& terrainSystem = renderer_->getSystems().terrain();
        demoInfo.getTerrainHeight = [&terrainSystem](float x, float z) {
            return terrainSystem.getHeightAt(x, z);
        };

        ecsMaterialDemo_ = ecs::ECSMaterialDemo::create(demoInfo);
        if (ecsMaterialDemo_) {
            SDL_Log("ECS Material Demo: Initialized with demo entities");
        }
    }
}

void Application::updateECS(float deltaTime) {

    auto& sceneManager = renderer_->getSystems().scene();
    auto& sceneBuilder = sceneManager.getSceneBuilder();
    auto& renderables = sceneManager.getRenderables();

    // Get scene entities from SceneBuilder (Phase 6: entities managed by SceneBuilder)
    const auto& sceneEntities = sceneBuilder.getSceneEntities();

    // Lazy initialization: if ECS world was set but entities not yet created (deferred mode)
    if (sceneEntities.empty() && !renderables.empty() && sceneBuilder.getECSWorld() == nullptr) {
        sceneBuilder.setECSWorld(&ecsWorld_);
        sceneBuilder.createEntitiesFromRenderables();
        SDL_Log("ECS: Populated %zu entities from deferred renderables", sceneBuilder.getSceneEntities().size());
    }

    // Re-fetch after potential creation
    const auto& currentEntities = sceneBuilder.getSceneEntities();

    // Set up bone attachments for weapons using tag queries (once, after entities are populated)
    if (!ecsWeaponsInitialized_ && !currentEntities.empty() && sceneBuilder.hasWeapons()) {
        // Find and attach sword (right hand weapon) using WeaponTag query
        for (auto [entity, weaponTag] : ecsWorld_.view<ecs::WeaponTag>().each()) {
            if (weaponTag.slot == ecs::WeaponSlot::RightHand) {
                if (!ecsWorld_.has<ecs::BoneAttachment>(entity)) {
                    ecsWorld_.add<ecs::BoneAttachment>(entity,
                        sceneBuilder.getRightHandBoneIndex(),
                        sceneBuilder.getSwordOffset());
                    SDL_Log("ECS: Attached sword to right hand bone %d", sceneBuilder.getRightHandBoneIndex());
                }
            } else if (weaponTag.slot == ecs::WeaponSlot::LeftHand) {
                if (!ecsWorld_.has<ecs::BoneAttachment>(entity)) {
                    ecsWorld_.add<ecs::BoneAttachment>(entity,
                        sceneBuilder.getLeftHandBoneIndex(),
                        sceneBuilder.getShieldOffset());
                    SDL_Log("ECS: Attached shield to left hand bone %d", sceneBuilder.getLeftHandBoneIndex());
                }
            }
        }

        ecsWeaponsInitialized_ = true;
    }

    // Sync transforms from Renderables to ECS (for objects NOT driven by bone attachments)
    for (size_t i = 0; i < currentEntities.size() && i < renderables.size(); ++i) {
        ecs::Entity entity = currentEntities[i];
        if (ecsWorld_.valid(entity) && ecsWorld_.has<ecs::Transform>(entity)) {
            // Skip if entity has BoneAttachment (skeleton drives it) or LocalTransform (hierarchy drives it)
            if (!ecsWorld_.has<ecs::BoneAttachment>(entity) && !ecsWorld_.has<ecs::LocalTransform>(entity)) {
                ecsWorld_.get<ecs::Transform>(entity).matrix = renderables[i].transform;
            }
        }
    }

    // Update bone attachments from skeleton
    if (sceneBuilder.hasCharacter() && ecsWeaponsInitialized_) {
        const auto& skeleton = sceneBuilder.getAnimatedCharacter().getSkeleton();
        std::vector<glm::mat4> globalBoneTransforms;
        skeleton.computeGlobalTransforms(globalBoneTransforms);

        // Get character world transform from player state
        glm::mat4 characterWorld = player_.movement.getModelMatrix(player_.transform);

        // Update all bone-attached entities
        ecs::systems::updateBoneAttachments(ecsWorld_, characterWorld, globalBoneTransforms);

        // Sync bone-attached ECS transforms back to renderables for rendering
        for (auto [entity, attachment, transform] :
             ecsWorld_.view<ecs::BoneAttachment, ecs::Transform>().each()) {
            // Find the renderable index for this entity
            for (size_t i = 0; i < currentEntities.size(); ++i) {
                if (currentEntities[i] == entity && i < renderables.size()) {
                    renderables[i].transform = transform.matrix;
                    break;
                }
            }
        }
    }

    // Update ECS material demo (wetness/damage cycling, selection toggling)
    static float totalTime = 0.0f;
    totalTime += deltaTime;
    if (ecsMaterialDemo_) {
        ecsMaterialDemo_->update(deltaTime, totalTime);
    }

    // Update hierarchical world transforms (parent * local -> world)
    // This must run before visibility culling so world transforms are current
    ecs::systems::updateWorldTransforms(ecsWorld_);

    // Update visibility culling based on camera frustum
    glm::mat4 viewProj = camera.getProjectionMatrix() * camera.getViewMatrix();
    ecs::Frustum frustum = ecs::Frustum::fromViewProjection(viewProj);
    ecs::systems::updateVisibility(ecsWorld_, frustum);

    // Update LOD levels based on camera distance
    ecs::systems::updateLOD(ecsWorld_, camera.getPosition());

    // Get culling stats for debugging (could expose to GUI later)
    [[maybe_unused]] ecs::render::CullStats stats = ecs::render::getCullStats(ecsWorld_);
}
