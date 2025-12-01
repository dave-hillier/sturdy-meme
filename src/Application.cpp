#include "Application.h"
#include <chrono>
#include <cmath>
#include <cstdio>

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

    // Initialize physics system
    if (!physics.init()) {
        SDL_Log("Failed to initialize physics system");
        return false;
    }

    // Initialize terrain physics using heightfield from terrain system
    const auto& terrain = renderer.getTerrainSystem();
    const auto& terrainConfig = terrain.getConfig();
    renderer.getSceneManager().initTerrainPhysics(
        physics,
        terrain.getHeightMapData(),
        terrain.getHeightMapResolution(),
        terrainConfig.size,
        terrainConfig.heightScale
    );

    // Initialize scene physics (dynamic objects)
    renderer.getSceneManager().initPhysics(physics);

    // Create character controller for player
    physics.createCharacter(glm::vec3(0.0f, 0.1f, 0.0f), Player::CAPSULE_HEIGHT, Player::CAPSULE_RADIUS);

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
        gui.render(renderer, lastDeltaTime, currentFps);

        // Update input system
        input.update(deltaTime, camera.getYaw());

        // Apply input to camera
        applyInputToCamera();

        // Process movement input for third-person mode
        glm::vec3 desiredVelocity(0.0f);
        if (input.isThirdPersonMode()) {
            glm::vec3 moveDir = input.getMovementDirection();
            if (glm::length(moveDir) > 0.001f) {
                moveDir = glm::normalize(moveDir);
                desiredVelocity = moveDir * moveSpeed;

                // Rotate player to face movement direction
                float newYaw = glm::degrees(atan2(moveDir.x, moveDir.z));
                float currentYaw = player.getYaw();
                float yawDiff = newYaw - currentYaw;
                // Normalize yaw difference
                while (yawDiff > 180.0f) yawDiff -= 360.0f;
                while (yawDiff < -180.0f) yawDiff += 360.0f;
                player.rotate(yawDiff * 10.0f * deltaTime);  // Smooth rotation
            }
        }

        // Always update physics character controller (handles gravity, jumping, and movement)
        physics.updateCharacter(deltaTime, desiredVelocity, input.wantsJump());

        // Update physics simulation
        physics.update(deltaTime);

        // Update player position from physics character controller
        glm::vec3 physicsPos = physics.getCharacterPosition();
        player.setPosition(physicsPos);

        // Update scene object transforms from physics
        renderer.getSceneManager().update(physics);

        // Update player position for grass interaction (always, regardless of camera mode)
        renderer.setPlayerPosition(player.getPosition(), Player::CAPSULE_RADIUS);

        // Update flag cloth simulation
        updateFlag(deltaTime);

        // Update camera and player based on mode
        if (input.isThirdPersonMode()) {
            camera.setThirdPersonTarget(player.getFocusPoint());
            camera.updateThirdPerson();
            renderer.getSceneManager().updatePlayerTransform(player.getModelMatrix());
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
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                }
                else if (event.key.scancode == SDL_SCANCODE_F1) {
                    gui.toggleVisibility();
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
    if (input.wasModeSwitchedThisFrame() && input.isThirdPersonMode()) {
        camera.orbitPitch(15.0f);
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

    // Create terrain ground plane (radius 50)
    physics.createTerrainDisc(50.0f, 0.0f);

    // Scene object layout from SceneBuilder (after multi-lights update):
    // 0: Ground disc (static terrain - already created above)
    // 1: Wooden crate 1 at (2.0, 0.5, 0.0) - unit cube
    // 2: Rotated wooden crate at (-1.5, 0.5, 1.0)
    // 3: Polished metal sphere at (0.0, 0.5, -2.0) - radius 0.5
    // 4: Rough metal sphere at (-3.0, 0.5, -1.0) - radius 0.5
    // 5: Polished metal cube at (3.0, 0.5, -2.0)
    // 6: Brushed metal cube at (-3.0, 0.5, -3.0)
    // 7: Emissive sphere at (2.0, 1.3, 0.0) - scaled 0.3, visual radius 0.15
    // 8: Blue light at (-3.0, 2.0, 2.0) - fixed, no physics
    // 9: Green light at (4.0, 1.5, -2.0) - fixed, no physics
    // 10: Player capsule (handled by character controller)
    // 11: Flag pole at (5.0, 1.5, 0.0) - static physics
    // 12: Flag cloth - no physics (soft body simulation handles it)

    const size_t numSceneObjects = 13;
    scenePhysicsBodies.resize(numSceneObjects, INVALID_BODY_ID);

    // Box half-extent for unit cube
    glm::vec3 cubeHalfExtents(0.5f, 0.5f, 0.5f);
    float boxMass = 10.0f;
    float sphereMass = 5.0f;

    // Spawn objects slightly above ground to let them settle
    const float spawnOffset = 0.1f;

    // Index 1: Wooden crate 1
    scenePhysicsBodies[1] = physics.createBox(glm::vec3(2.0f, 0.5f + spawnOffset, 0.0f), cubeHalfExtents, boxMass);

    // Index 2: Rotated wooden crate
    scenePhysicsBodies[2] = physics.createBox(glm::vec3(-1.5f, 0.5f + spawnOffset, 1.0f), cubeHalfExtents, boxMass);

    // Index 3: Polished metal sphere (mesh radius 0.5)
    scenePhysicsBodies[3] = physics.createSphere(glm::vec3(0.0f, 0.5f + spawnOffset, -2.0f), 0.5f, sphereMass);

    // Index 4: Rough metal sphere (mesh radius 0.5)
    scenePhysicsBodies[4] = physics.createSphere(glm::vec3(-3.0f, 0.5f + spawnOffset, -1.0f), 0.5f, sphereMass);

    // Index 5: Polished metal cube
    scenePhysicsBodies[5] = physics.createBox(glm::vec3(3.0f, 0.5f + spawnOffset, -2.0f), cubeHalfExtents, boxMass);

    // Index 6: Brushed metal cube
    scenePhysicsBodies[6] = physics.createBox(glm::vec3(-3.0f, 0.5f + spawnOffset, -3.0f), cubeHalfExtents, boxMass);

    // Index 7: Emissive sphere - mesh radius 0.5, scaled 0.3 = visual radius 0.15
    scenePhysicsBodies[7] = physics.createSphere(glm::vec3(2.0f, 1.3f + spawnOffset, 0.0f), 0.5f * 0.3f, 1.0f);

    // Index 8 & 9: Blue and green lights - NO PHYSICS (fixed light indicators)
    // scenePhysicsBodies[8] and [9] remain INVALID_BODY_ID

    // Index 11: Flag pole - static cylinder (radius 0.05m, height 3m)
    // Create as a static box approximation for simplicity
    glm::vec3 poleHalfExtents(0.05f, 1.5f, 0.05f);  // Half of 3m height
    scenePhysicsBodies[11] = physics.createStaticBox(glm::vec3(5.0f, 1.5f, 0.0f), poleHalfExtents);

    // Index 12: Flag cloth - NO PHYSICS (soft body simulation)
    // scenePhysicsBodies[12] remains INVALID_BODY_ID

    // Create character controller for player slightly above ground
    physics.createCharacter(glm::vec3(0.0f, spawnOffset, 0.0f), Player::CAPSULE_HEIGHT, Player::CAPSULE_RADIUS);

    SDL_Log("Physics initialized with %d active bodies", physics.getActiveBodyCount());
}

void Application::updatePhysicsToScene() {
    // Update scene object transforms from physics simulation
    auto& sceneObjects = renderer.getSceneManager().getSceneObjects();

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

    // Position the cloth at the top of the pole (pole is at 5, 1.5, 0 with height 3m)
    // Top of pole is at y = 1.5 + 1.5 = 3.0
    glm::vec3 clothTopLeft(5.0f - 0.1f, 3.0f, 0.0f);  // Slightly to the left of pole center

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
    auto& sceneObjects = renderer.getSceneManager().getSceneObjects();
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
