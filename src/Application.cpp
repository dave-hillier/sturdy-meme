#include "Application.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unordered_set>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

bool Application::init(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }

    // Initialize input system (handles gamepad detection)
    if (!input.init()) {
        SDL_Log("Failed to initialize input system");
        return false;
    }

    window = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    std::string resourcePath = getResourcePath();
    if (!renderer.init(window, resourcePath)) {
        SDL_Log("Failed to initialize renderer");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    camera.setAspectRatio(static_cast<float>(width) / static_cast<float>(height));

    // Position camera at terrain height, looking at scene objects
    {
        const auto& terrain = renderer.getTerrainSystem();
        float cameraX = 0.0f, cameraZ = 10.0f;  // Start behind the scene
        float terrainY = terrain.getHeightAt(cameraX, cameraZ);
        camera.setPosition(glm::vec3(cameraX, terrainY + 2.0f, cameraZ));
        camera.setYaw(-90.0f);   // Look toward -Z (toward scene objects)
        camera.setPitch(-10.0f); // Slight downward tilt
    }

    // Initialize physics system
    if (!physics.init()) {
        SDL_Log("Failed to initialize physics system");
        return false;
    }

    // Create terrain hole at well entrance location
    // This must be done before terrain physics is initialized
    {
        auto& terrain = renderer.getTerrainSystem();
        const auto& sceneBuilder = renderer.getSceneManager().getSceneBuilder();
        float wellX = sceneBuilder.getWellEntranceX();
        float wellZ = sceneBuilder.getWellEntranceZ();
        terrain.setHoleCircle(wellX, wellZ, SceneBuilder::WELL_HOLE_RADIUS, true);
        terrain.uploadHoleMaskToGPU();
        SDL_Log("Created terrain hole at well entrance (%.1f, %.1f) radius %.1f",
                wellX, wellZ, SceneBuilder::WELL_HOLE_RADIUS);
    }

    // Initialize tile-based terrain physics
    // Streams high-res collision tiles near player, coarse tiles for distant coverage
    auto& terrain = renderer.getTerrainSystem();
    const auto& terrainConfig = terrain.getConfig();
    terrainPhysicsTiles.init(
        &physics,
        &terrain.getTileCache(),
        terrainConfig.size,
        terrain.getTileCache().getHeightScale(),
        terrain.getTileCache().getMinAltitude()
    );
    SDL_Log("Terrain physics using tile-based streaming (1000m high detail radius)");

    // IMPORTANT: Preload terrain physics tiles BEFORE spawning any dynamic objects
    // Otherwise objects spawn and immediately fall through non-existent collision
    glm::vec3 spawnAreaCenter(0.0f, 0.0f, 0.0f);
    terrainPhysicsTiles.preloadTilesAt(spawnAreaCenter, 1000.0f);

    // Initialize scene physics (dynamic objects)
    renderer.getSceneManager().initPhysics(physics);

    // Create convex hull colliders for rocks using actual mesh geometry
    const auto& rockSystem = renderer.getRockSystem();
    const auto& rockInstances = rockSystem.getRockInstances();
    const auto& rockMeshes = rockSystem.getRockMeshes();

    for (const auto& rock : rockInstances) {
        // Rock position is adjusted down by 15% of scale in rendering
        glm::vec3 colliderPos = rock.position;
        colliderPos.y -= rock.scale * 0.15f;

        // Get the mesh for this rock variation
        const Mesh& mesh = rockMeshes[rock.meshVariation];
        const auto& vertices = mesh.getVertices();

        // Extract just the positions from the vertex data
        std::vector<glm::vec3> positions;
        positions.reserve(vertices.size());
        for (const auto& v : vertices) {
            positions.push_back(v.position);
        }

        // Create rotation quaternion from Y-axis rotation
        glm::quat rotation = glm::angleAxis(rock.rotation, glm::vec3(0.0f, 1.0f, 0.0f));

        // Create convex hull from mesh vertices with rock's scale
        physics.createStaticConvexHull(colliderPos, positions.data(), positions.size(),
                                       rock.scale, rotation);
    }
    SDL_Log("Created %zu rock convex hull colliders", rockInstances.size());

    // Create character controller for player at terrain height
    float playerSpawnX = 0.0f, playerSpawnZ = 0.0f;
    float playerSpawnY = terrain.getHeightAt(playerSpawnX, playerSpawnZ) + 0.1f;

    // IMPORTANT: Load physics tiles at spawn position BEFORE creating character
    // Otherwise character falls through non-existent terrain collision
    // Use preloadTilesAt() which loads all needed tiles at once (no per-frame limit)
    glm::vec3 spawnPos(playerSpawnX, playerSpawnY, playerSpawnZ);
    terrainPhysicsTiles.preloadTilesAt(spawnPos, 1000.0f);

    // Debug: Sample terrain height at spawn position using different methods
    float heightFromTerrainSystem = terrain.getHeightAt(playerSpawnX, playerSpawnZ);
    float heightFromTileCache = 0.0f;
    bool tileHasHeight = terrain.getTileCache().getHeightAt(playerSpawnX, playerSpawnZ, heightFromTileCache);
    SDL_Log("DEBUG Height at spawn (%.1f, %.1f):", playerSpawnX, playerSpawnZ);
    SDL_Log("  TerrainSystem.getHeightAt(): %.2f", heightFromTerrainSystem);
    SDL_Log("  TileCache.getHeightAt(): %.2f (found=%d)", heightFromTileCache, tileHasHeight ? 1 : 0);
    SDL_Log("  Player spawn Y (height + 0.1): %.2f", playerSpawnY);

    physics.createCharacter(glm::vec3(playerSpawnX, playerSpawnY, playerSpawnZ), Player::CAPSULE_HEIGHT, Player::CAPSULE_RADIUS);

    SDL_Log("Physics initialized with %d active bodies", physics.getActiveBodyCount());

    // Initialize flag simulation
    initFlag();

    // Initialize GUI system
    if (!gui.init(window, renderer.getInstance(), renderer.getPhysicalDevice(),
                  renderer.getDevice(), renderer.getGraphicsQueueFamily(),
                  renderer.getGraphicsQueue(), renderer.getSwapchainRenderPass(),
                  renderer.getSwapchainImageCount())) {
        SDL_Log("Failed to initialize GUI system");
        return false;
    }

    // Set GUI render callback
    renderer.setGuiRenderCallback([this](VkCommandBuffer cmd) {
        gui.endFrame(cmd);
    });

    // Set up input system with GUI reference for input blocking
    input.setGuiSystem(&gui);
    input.setMoveSpeed(moveSpeed);

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
        gui.beginFrame();
        gui.render(renderer, camera, lastDeltaTime, currentFps);

        // Update input system
        input.update(deltaTime, camera.getYaw());

        // Apply input to camera
        applyInputToCamera();

        // Process movement input for third-person mode
        glm::vec3 desiredVelocity(0.0f);
        if (input.isThirdPersonMode()) {
            // Handle orientation lock toggle
            if (input.wantsOrientationLockToggle()) {
                player.toggleOrientationLock();
                SDL_Log("Orientation lock: %s", player.isOrientationLocked() ? "ON" : "OFF");
            }

            // Temporarily lock orientation if holding trigger/middle mouse
            bool effectiveLock = player.isOrientationLocked() || input.isOrientationLockHeld();

            glm::vec3 moveDir = input.getMovementDirection();
            if (glm::length(moveDir) > 0.001f) {
                moveDir = glm::normalize(moveDir);
                float currentSpeed = input.isSprinting() ? sprintSpeed : moveSpeed;
                desiredVelocity = moveDir * currentSpeed;

                // Only rotate player to face movement direction if not locked
                if (!effectiveLock) {
                    float newYaw = glm::degrees(atan2(moveDir.x, moveDir.z));
                    float currentYaw = player.getYaw();
                    float yawDiff = newYaw - currentYaw;
                    // Normalize yaw difference
                    while (yawDiff > 180.0f) yawDiff -= 360.0f;
                    while (yawDiff < -180.0f) yawDiff += 360.0f;
                    player.rotate(yawDiff * 10.0f * deltaTime);  // Smooth rotation
                }
            }
        }

        // Detect jump BEFORE physics update (character is still grounded when jump is requested)
        bool wasGrounded = physics.isCharacterOnGround();
        bool wantsJump = input.wantsJump();
        bool isJumping = wantsJump && wasGrounded;

        // If starting a jump, compute trajectory for animation sync
        if (isJumping) {
            glm::vec3 startPos = physics.getCharacterPosition();
            // Velocity: horizontal from input + jump impulse (5.0 m/s up, matching PhysicsSystem)
            glm::vec3 jumpVelocity = desiredVelocity;
            jumpVelocity.y = 5.0f;
            renderer.startCharacterJump(startPos, jumpVelocity, 9.81f, &physics);
        }

        // Always update physics character controller (handles gravity, jumping, and movement)
        physics.updateCharacter(deltaTime, desiredVelocity, wantsJump);

        // Update physics simulation
        physics.update(deltaTime);

        // Update terrain physics tiles based on player position
        glm::vec3 playerPos = physics.getCharacterPosition();
        terrainPhysicsTiles.update(playerPos, 1000.0f);

        // Debug: Log orb position every second to track physics terrain offset
        static float debugLogTimer = 0.0f;
        debugLogTimer += deltaTime;
        if (debugLogTimer >= 1.0f) {
            debugLogTimer = 0.0f;
            auto& terrainSys = renderer.getTerrainSystem();

            // Use the orb light position (updated from physics body 6 - emissive sphere)
            // The orb physics radius is 0.15 (0.5 * 0.3 scale)
            glm::vec3 orbPos = renderer.getSceneManager().getOrbLightPosition();
            float cpuTerrainHeight = terrainSys.getHeightAt(orbPos.x, orbPos.z);

            // Cast ray down from above orb to find where physics terrain collision is
            glm::vec3 rayStart = glm::vec3(orbPos.x, orbPos.y + 10.0f, orbPos.z);
            glm::vec3 rayEnd = glm::vec3(orbPos.x, orbPos.y - 50.0f, orbPos.z);
            auto hits = physics.castRayAllHits(rayStart, rayEnd);

            float physicsTerrainHeight = -9999.0f;
            for (const auto& hit : hits) {
                // Find the terrain hit (should be below orb position)
                if (hit.position.y < orbPos.y && hit.position.y > physicsTerrainHeight) {
                    physicsTerrainHeight = hit.position.y;
                }
            }

            float orbBottom = orbPos.y - 0.15f;  // Physics radius = 0.5 * 0.3 = 0.15

            // Also log GPU active tile count to see if shaders are using tiles
            auto& tileCache = terrainSys.getTileCache();
            uint32_t gpuTileCount = tileCache.getActiveTileCount();

            // Check if orb position is covered by any GPU tile
            bool gpuTileCoversOrb = false;
            for (const auto* tile : tileCache.getActiveTiles()) {
                if (orbPos.x >= tile->worldMinX && orbPos.x < tile->worldMaxX &&
                    orbPos.z >= tile->worldMinZ && orbPos.z < tile->worldMaxZ) {
                    gpuTileCoversOrb = true;
                    break;
                }
            }

            SDL_Log("DEBUG Orb pos: (%.1f, %.2f, %.1f), physGnd: %.2f, cpuTerrain: %.2f, gpuTiles: %u, coversOrb: %d",
                    orbPos.x, orbPos.y, orbPos.z, physicsTerrainHeight, cpuTerrainHeight, gpuTileCount,
                    gpuTileCoversOrb ? 1 : 0);

            // Log first few GPU tile bounds to understand coverage
            if (!gpuTileCoversOrb && gpuTileCount > 0) {
                SDL_Log("  GPU tiles not covering orb at (%.1f, %.1f):", orbPos.x, orbPos.z);
                int count = 0;
                for (const auto* tile : tileCache.getActiveTiles()) {
                    if (count++ < 3) {
                        SDL_Log("    Tile[%d,%d]: X[%.0f,%.0f] Z[%.0f,%.0f]",
                                tile->coord.x, tile->coord.z,
                                tile->worldMinX, tile->worldMaxX,
                                tile->worldMinZ, tile->worldMaxZ);
                    }
                }
            }
        }

        // Update player position from physics character controller
        glm::vec3 physicsPos = playerPos;
        glm::vec3 physicsVelocity = physics.getCharacterVelocity();
        player.setPosition(physicsPos);

        // Update scene object transforms from physics
        renderer.getSceneManager().update(physics);

        // Update player position for grass interaction (always, regardless of camera mode)
        renderer.setPlayerState(player.getPosition(), physicsVelocity, Player::CAPSULE_RADIUS);

        // Wait for previous frame's GPU work to complete before updating dynamic meshes.
        // This prevents race conditions where we destroy mesh buffers while the GPU
        // is still reading them from the previous frame.
        renderer.waitForPreviousFrame();

        // Update flag cloth simulation
        updateFlag(deltaTime);

        // Update animated character (skeletal animation)
        // Calculate movement speed from desired velocity for animation state machine
        float movementSpeed = glm::length(glm::vec2(desiredVelocity.x, desiredVelocity.z));
        bool isGrounded = physics.isCharacterOnGround();

        // Sync cape enabled state from GUI
        renderer.getSceneBuilder().setCapeEnabled(gui.getPlayerSettings().capeEnabled);

        renderer.updateAnimatedCharacter(deltaTime, movementSpeed, isGrounded, isJumping);

        // Update camera and player based on mode
        if (input.isThirdPersonMode()) {
            camera.setThirdPersonTarget(player.getFocusPoint());
            camera.updateThirdPerson(deltaTime);
            renderer.getSceneManager().updatePlayerTransform(player.getModelMatrix());

            // Dynamic FOV: widen during sprinting for sense of speed
            float targetFov = 45.0f;  // Base FOV
            if (input.isSprinting() && glm::length(desiredVelocity) > 0.1f) {
                targetFov = 55.0f;  // Sprint FOV
            }
            camera.setTargetFov(targetFov);

            // Update camera occlusion (fade objects between camera and player)
            updateCameraOcclusion(deltaTime);
        }

        camera.setAspectRatio(static_cast<float>(renderer.getWidth()) / static_cast<float>(renderer.getHeight()));

        renderer.render(camera);

        // Update window title with FPS, time of day, and camera mode
        if (deltaTime > 0.0f) {
            smoothedFps = smoothedFps * 0.95f + (1.0f / deltaTime) * 0.05f;
        }
        float timeOfDay = renderer.getTimeOfDay();
        int hours = static_cast<int>(timeOfDay * 24.0f);
        int minutes = static_cast<int>((timeOfDay * 24.0f - hours) * 60.0f);
        char title[96];
        const char* modeStr = input.isThirdPersonMode() ? "3rd Person" : "Free Cam";
        snprintf(title, sizeof(title), "Vulkan Game - FPS: %.0f | Time: %02d:%02d | %s (Tab to toggle)",
                 smoothedFps, hours, minutes, modeStr);
        SDL_SetWindowTitle(window, title);
    }

    renderer.waitIdle();
}

void Application::shutdown() {
    renderer.waitIdle();
    gui.shutdown();
    input.shutdown();
    terrainPhysicsTiles.destroy();
    physics.shutdown();
    renderer.shutdown();

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    SDL_Quit();
}

void Application::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Pass events to GUI first
        gui.processEvent(event);

        // Pass events to input system
        input.processEvent(event);

        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                renderer.notifyWindowResized();
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                }
                else if (event.key.scancode == SDL_SCANCODE_F1) {
                    gui.toggleVisibility();
                }
                else if (event.key.scancode == SDL_SCANCODE_F2) {
                    gui.getTreeEditorGui().toggleVisibility();
                    SDL_Log("Tree Editor: %s", gui.getTreeEditorGui().isVisible() ? "ON" : "OFF");
                }
                else if (event.key.scancode == SDL_SCANCODE_P) {
                    gui.getTreeEditorGui().placeTreeAtCamera(renderer, camera);
                    gui.getTreeEditorGui().setVisible(true);
                    SDL_Log("Tree placed at camera position");
                }
                else if (event.key.scancode == SDL_SCANCODE_1) {
                    renderer.setTimeOfDay(0.25f);
                }
                else if (event.key.scancode == SDL_SCANCODE_2) {
                    renderer.setTimeOfDay(0.5f);
                }
                else if (event.key.scancode == SDL_SCANCODE_3) {
                    renderer.setTimeOfDay(0.75f);
                }
                else if (event.key.scancode == SDL_SCANCODE_4) {
                    renderer.setTimeOfDay(0.0f);
                }
                else if (event.key.scancode == SDL_SCANCODE_EQUALS) {
                    renderer.setTimeScale(renderer.getTimeScale() * 2.0f);
                }
                else if (event.key.scancode == SDL_SCANCODE_MINUS) {
                    renderer.setTimeScale(renderer.getTimeScale() * 0.5f);
                }
                else if (event.key.scancode == SDL_SCANCODE_R) {
                    renderer.resumeAutoTime();
                    renderer.setTimeScale(1.0f);
                }
                else if (event.key.scancode == SDL_SCANCODE_6) {
                    renderer.toggleCascadeDebug();
                    SDL_Log("Cascade debug visualization: %s", renderer.isShowingCascadeDebug() ? "ON" : "OFF");
                }
                else if (event.key.scancode == SDL_SCANCODE_7) {
                    renderer.toggleSnowDepthDebug();
                    SDL_Log("Snow depth debug visualization: %s", renderer.isShowingSnowDepthDebug() ? "ON" : "OFF");
                }
                else if (event.key.scancode == SDL_SCANCODE_8) {
                    renderer.setHiZCullingEnabled(!renderer.isHiZCullingEnabled());
                    SDL_Log("Hi-Z occlusion culling: %s", renderer.isHiZCullingEnabled() ? "ON" : "OFF");
                }
                else if (event.key.scancode == SDL_SCANCODE_Z) {
                    float currentIntensity = renderer.getIntensity();
                    renderer.setWeatherIntensity(std::max(0.0f, currentIntensity - 0.1f));
                    SDL_Log("Weather intensity: %.1f", renderer.getIntensity());
                }
                else if (event.key.scancode == SDL_SCANCODE_X) {
                    float currentIntensity = renderer.getIntensity();
                    renderer.setWeatherIntensity(std::min(1.0f, currentIntensity + 0.1f));
                    SDL_Log("Weather intensity: %.1f", renderer.getIntensity());
                }
                else if (event.key.scancode == SDL_SCANCODE_C) {
                    uint32_t currentType = renderer.getWeatherType();
                    if (renderer.getIntensity() == 0.0f && currentType == 0) {
                        renderer.setWeatherType(0);
                        renderer.setWeatherIntensity(0.5f);
                    } else if (currentType == 0) {
                        renderer.setWeatherType(1);
                        renderer.setWeatherIntensity(0.5f);
                    } else if (currentType == 1) {
                        renderer.setWeatherType(0);
                        renderer.setWeatherIntensity(0.0f);
                    }

                    std::string weatherStatus = "Clear";
                    if (renderer.getIntensity() > 0.0f) {
                        if (renderer.getWeatherType() == 0) {
                            weatherStatus = "Rain";
                        } else if (renderer.getWeatherType() == 1) {
                            weatherStatus = "Snow";
                        }
                    }
                    SDL_Log("Weather type: %s, Intensity: %.1f", weatherStatus.c_str(), renderer.getIntensity());
                }
                else if (event.key.scancode == SDL_SCANCODE_F) {
                    glm::vec3 playerPos = player.getPosition();
                    renderer.spawnConfetti(playerPos, 8.0f, 100.0f, 0.5f);
                    SDL_Log("Confetti!");
                }
                else if (event.key.scancode == SDL_SCANCODE_V) {
                    renderer.toggleCloudStyle();
                    SDL_Log("Cloud style: %s", renderer.isUsingParaboloidClouds() ? "Paraboloid LUT Hybrid" : "Procedural");
                }
                else if (event.key.scancode == SDL_SCANCODE_LEFTBRACKET) {
                    float density = renderer.getFogDensity();
                    renderer.setFogDensity(std::max(0.0f, density - 0.0025f));
                    SDL_Log("Fog density: %.3f", renderer.getFogDensity());
                }
                else if (event.key.scancode == SDL_SCANCODE_RIGHTBRACKET) {
                    float density = renderer.getFogDensity();
                    renderer.setFogDensity(std::min(0.2f, density + 0.0025f));
                    SDL_Log("Fog density: %.3f", renderer.getFogDensity());
                }
                else if (event.key.scancode == SDL_SCANCODE_BACKSLASH) {
                    renderer.setFogEnabled(!renderer.isFogEnabled());
                    SDL_Log("Fog: %s", renderer.isFogEnabled() ? "ON" : "OFF");
                }
                else if (event.key.scancode == SDL_SCANCODE_COMMA) {
                    float snow = renderer.getSnowAmount();
                    renderer.setSnowAmount(std::max(0.0f, snow - 0.1f));
                    SDL_Log("Snow amount: %.1f", renderer.getSnowAmount());
                }
                else if (event.key.scancode == SDL_SCANCODE_PERIOD) {
                    float snow = renderer.getSnowAmount();
                    renderer.setSnowAmount(std::min(1.0f, snow + 0.1f));
                    SDL_Log("Snow amount: %.1f", renderer.getSnowAmount());
                }
                else if (event.key.scancode == SDL_SCANCODE_SLASH) {
                    float snow = renderer.getSnowAmount();
                    renderer.setSnowAmount(snow < 0.5f ? 1.0f : 0.0f);
                    SDL_Log("Snow amount: %.1f", renderer.getSnowAmount());
                }
                else if (event.key.scancode == SDL_SCANCODE_T) {
                    renderer.toggleTerrainWireframe();
                    SDL_Log("Terrain wireframe: %s", renderer.isTerrainWireframeMode() ? "ON" : "OFF");
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                    renderer.setTimeOfDay(0.25f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
                    renderer.setTimeOfDay(0.5f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                    renderer.setTimeOfDay(0.75f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                    renderer.setTimeOfDay(0.0f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                    renderer.resumeAutoTime();
                    renderer.setTimeScale(1.0f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                    running = false;
                }
                break;
            default:
                break;
        }
    }

    // Handle camera mode switch initialization
    if (input.wasModeSwitchedThisFrame()) {
        if (input.isThirdPersonMode()) {
            // Initialize third-person camera from current free camera position
            // This ensures smooth transition instead of snapping to origin
            camera.initializeThirdPersonFromCurrentPosition(player.getFocusPoint());
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
        renderer.setTimeScale(renderer.getTimeScale() * timeScaleInput);
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
    if (!physics.init()) {
        SDL_Log("Failed to initialize physics system");
        return;
    }

    // Helper to get terrain height at a position
    auto getTerrainY = [this](float x, float z) -> float {
        return renderer.getTerrainHeightAt(x, z);
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
    scenePhysicsBodies[1] = physics.createBox(glm::vec3(x1, getTerrainY(x1, z1) + 0.5f + spawnOffset, z1), cubeHalfExtents, boxMass);

    // Index 2: Rotated wooden crate
    float x2 = -1.5f, z2 = 1.0f;
    scenePhysicsBodies[2] = physics.createBox(glm::vec3(x2, getTerrainY(x2, z2) + 0.5f + spawnOffset, z2), cubeHalfExtents, boxMass);

    // Index 3: Polished metal sphere (mesh radius 0.5)
    float x3 = 0.0f, z3 = -2.0f;
    scenePhysicsBodies[3] = physics.createSphere(glm::vec3(x3, getTerrainY(x3, z3) + 0.5f + spawnOffset, z3), 0.5f, sphereMass);

    // Index 4: Rough metal sphere (mesh radius 0.5)
    float x4 = -3.0f, z4 = -1.0f;
    scenePhysicsBodies[4] = physics.createSphere(glm::vec3(x4, getTerrainY(x4, z4) + 0.5f + spawnOffset, z4), 0.5f, sphereMass);

    // Index 5: Polished metal cube
    float x5 = 3.0f, z5 = -2.0f;
    scenePhysicsBodies[5] = physics.createBox(glm::vec3(x5, getTerrainY(x5, z5) + 0.5f + spawnOffset, z5), cubeHalfExtents, boxMass);

    // Index 6: Brushed metal cube
    float x6 = -3.0f, z6 = -3.0f;
    scenePhysicsBodies[6] = physics.createBox(glm::vec3(x6, getTerrainY(x6, z6) + 0.5f + spawnOffset, z6), cubeHalfExtents, boxMass);

    // Index 7: Emissive sphere - mesh radius 0.5, scaled 0.3 = visual radius 0.15
    float x7 = 2.0f, z7 = 0.0f;
    scenePhysicsBodies[7] = physics.createSphere(glm::vec3(x7, getTerrainY(x7, z7) + 1.3f + spawnOffset, z7), 0.5f * 0.3f, 1.0f);

    // Index 8 & 9: Blue and green lights - NO PHYSICS (fixed light indicators)
    // scenePhysicsBodies[8] and [9] remain INVALID_BODY_ID

    // Index 11: Flag pole - static cylinder (radius 0.05m, height 3m)
    // Create as a static box approximation for simplicity
    float x11 = 5.0f, z11 = 0.0f;
    glm::vec3 poleHalfExtents(0.05f, 1.5f, 0.05f);  // Half of 3m height
    scenePhysicsBodies[11] = physics.createStaticBox(glm::vec3(x11, getTerrainY(x11, z11) + 1.5f, z11), poleHalfExtents);

    // Index 12: Flag cloth - NO PHYSICS (soft body simulation)
    // scenePhysicsBodies[12] remains INVALID_BODY_ID

    // Create character controller for player at terrain height
    float playerX = 0.0f, playerZ = 0.0f;
    float playerTerrainY = getTerrainY(playerX, playerZ);
    physics.createCharacter(glm::vec3(playerX, playerTerrainY + spawnOffset, playerZ), Player::CAPSULE_HEIGHT, Player::CAPSULE_RADIUS);

    SDL_Log("Physics initialized with %d active bodies", physics.getActiveBodyCount());
}

void Application::updatePhysicsToScene() {
    // Update scene object transforms from physics simulation
    auto& sceneObjects = renderer.getSceneManager().getRenderables();

    for (size_t i = 1; i < scenePhysicsBodies.size() && i < sceneObjects.size(); i++) {
        PhysicsBodyID bodyID = scenePhysicsBodies[i];
        if (bodyID == INVALID_BODY_ID) continue;

        // Skip player object (handled separately)
        if (i == renderer.getSceneManager().getPlayerObjectIndex()) continue;

        // Get transform from physics (position and rotation only)
        glm::mat4 physicsTransform = physics.getBodyTransform(bodyID);

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
            renderer.getSceneManager().setOrbLightPosition(orbPosition);
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
    float terrainHeight = renderer.getTerrainHeightAt(flagPoleX, flagPoleZ);
    float poleTopY = terrainHeight + 3.0f;  // Pole center is 1.5m above terrain, pole is 3m tall
    glm::vec3 clothTopLeft(flagPoleX - 0.1f, poleTopY, flagPoleZ);  // Slightly to the left of pole center

    clothSim.create(clothWidth, clothHeight, particleSpacing, clothTopLeft);

    // Pin the left edge of the cloth to the pole
    for (int y = 0; y < clothHeight; ++y) {
        clothSim.pinParticle(0, y);  // Pin left edge
    }

    // Create initial mesh geometry and upload to GPU
    clothSim.createMesh(renderer.getFlagClothMesh());
    renderer.uploadFlagClothMesh();

    SDL_Log("Flag initialized with %dx%d cloth simulation", clothWidth, clothHeight);
}

void Application::updateCameraOcclusion(float deltaTime) {
    // Only in third-person mode
    if (!input.isThirdPersonMode()) return;

    // Raycast from player focus point to camera position
    glm::vec3 playerFocus = player.getFocusPoint();
    glm::vec3 cameraPos = camera.getPosition();

    std::vector<RaycastHit> hits = physics.castRayAllHits(playerFocus, cameraPos);

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
    auto& sceneObjects = renderer.getSceneManager().getRenderables();
    for (size_t i = 0; i < scenePhysicsBodies.size() && i < sceneObjects.size(); i++) {
        PhysicsBodyID bodyID = scenePhysicsBodies[i];
        if (bodyID == INVALID_BODY_ID) continue;

        // Skip player (handled separately)
        if (i == renderer.getSceneManager().getPlayerObjectIndex()) continue;

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
    glm::vec3 playerPos = physics.getCharacterPosition();
    float playerRadius = Player::CAPSULE_RADIUS;
    float playerHeight = Player::CAPSULE_HEIGHT;

    // Add collision spheres for the player capsule (one at bottom, middle, and top)
    clothSim.addSphereCollision(playerPos + glm::vec3(0, playerRadius, 0), playerRadius);
    clothSim.addSphereCollision(playerPos + glm::vec3(0, playerHeight * 0.5f, 0), playerRadius);
    clothSim.addSphereCollision(playerPos + glm::vec3(0, playerHeight - playerRadius, 0), playerRadius);

    // Add collision spheres for dynamic physics objects
    auto& sceneObjects = renderer.getSceneManager().getRenderables();
    for (size_t i = 1; i < scenePhysicsBodies.size() && i < sceneObjects.size(); i++) {
        PhysicsBodyID bodyID = scenePhysicsBodies[i];
        if (bodyID == INVALID_BODY_ID) continue;
        if (i == renderer.getSceneManager().getPlayerObjectIndex()) continue; // Skip player (already handled)
        if (i == 11 || i == 12) continue; // Skip flag pole and cloth itself

        PhysicsBodyInfo info = physics.getBodyInfo(bodyID);

        // Add approximate collision spheres for physics objects
        // For simplicity, use a sphere of radius 0.5 for all objects
        clothSim.addSphereCollision(info.position, 0.5f);
    }

    // Update cloth simulation with wind
    clothSim.update(deltaTime, &renderer.getWindSystem());

    // Update the mesh vertices from cloth particles and re-upload to GPU
    clothSim.updateMesh(renderer.getFlagClothMesh());
    renderer.uploadFlagClothMesh();
}
