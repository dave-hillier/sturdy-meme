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

    // Try to open the first available gamepad
    int numJoysticks = 0;
    SDL_JoystickID* joysticks = SDL_GetJoysticks(&numJoysticks);
    if (joysticks) {
        for (int i = 0; i < numJoysticks; i++) {
            if (SDL_IsGamepad(joysticks[i])) {
                openGamepad(joysticks[i]);
                break;
            }
        }
        SDL_free(joysticks);
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

    // Initialize physics
    initPhysics();

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

        processEvents();
        handleInput(deltaTime);
        handleGamepadInput(deltaTime);

        // Update physics simulation
        physics.update(deltaTime);

        // Update player position from physics character controller
        glm::vec3 physicsPos = physics.getCharacterPosition();
        player.setPosition(physicsPos);

        // Update scene object transforms from physics
        updatePhysicsToScene();

        // Update camera and player based on mode
        if (thirdPersonMode) {
            camera.setThirdPersonTarget(player.getFocusPoint());
            camera.updateThirdPerson();
            renderer.updatePlayerTransform(player.getModelMatrix());
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
        const char* modeStr = thirdPersonMode ? "3rd Person" : "Free Cam";
        snprintf(title, sizeof(title), "Vulkan Game - FPS: %.0f | Time: %02d:%02d | %s (Tab to toggle)",
                 smoothedFps, hours, minutes, modeStr);
        SDL_SetWindowTitle(window, title);
    }

    renderer.waitIdle();
}

void Application::shutdown() {
    physics.shutdown();
    renderer.shutdown();
    closeGamepad();

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    SDL_Quit();
}

void Application::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
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
                else if (event.key.scancode == SDL_SCANCODE_TAB) {
                    thirdPersonMode = !thirdPersonMode;
                    SDL_Log("Camera mode: %s", thirdPersonMode ? "Third Person" : "Free Camera");
                    if (thirdPersonMode) {
                        // Initialize third-person camera with reasonable angle
                        camera.orbitPitch(15.0f);  // Look down slightly
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!gamepad) {
                    openGamepad(event.gdevice.which);
                }
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (gamepad && SDL_GetGamepadID(gamepad) == event.gdevice.which) {
                    closeGamepad();
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                    renderer.setTimeOfDay(0.25f);  // Sunrise
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
                    renderer.setTimeOfDay(0.5f);   // Noon
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                    renderer.setTimeOfDay(0.75f);  // Sunset
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
                    renderer.setTimeOfDay(0.0f);   // Midnight
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
                    renderer.resumeAutoTime();
                    renderer.setTimeScale(1.0f);
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                    running = false;
                }
                else if (event.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
                    // Left stick click toggles camera mode
                    thirdPersonMode = !thirdPersonMode;
                    SDL_Log("Camera mode: %s", thirdPersonMode ? "Third Person" : "Free Camera");
                    if (thirdPersonMode) {
                        camera.orbitPitch(15.0f);
                    }
                }
                break;
            default:
                break;
        }
    }
}

void Application::handleInput(float deltaTime) {
    const bool* keyState = SDL_GetKeyboardState(nullptr);

    if (thirdPersonMode) {
        handleThirdPersonInput(deltaTime, keyState);
    } else {
        handleFreeCameraInput(deltaTime, keyState);
    }
}

void Application::handleFreeCameraInput(float deltaTime, const bool* keyState) {
    // WASD for movement (standard FPS controls)
    if (keyState[SDL_SCANCODE_W]) {
        camera.moveForward(moveSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_S]) {
        camera.moveForward(-moveSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_A]) {
        camera.moveRight(-moveSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_D]) {
        camera.moveRight(moveSpeed * deltaTime);
    }

    // Arrow keys for camera rotation
    if (keyState[SDL_SCANCODE_UP]) {
        camera.rotatePitch(rotateSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_DOWN]) {
        camera.rotatePitch(-rotateSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_LEFT]) {
        camera.rotateYaw(-rotateSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_RIGHT]) {
        camera.rotateYaw(rotateSpeed * deltaTime);
    }

    // Space/E for up, C/Q for down (fly camera)
    if (keyState[SDL_SCANCODE_SPACE] || keyState[SDL_SCANCODE_E]) {
        camera.moveUp(moveSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_C] || keyState[SDL_SCANCODE_Q]) {
        camera.moveUp(-moveSpeed * deltaTime);
    }
}

void Application::handleThirdPersonInput(float deltaTime, const bool* keyState) {
    // WASD moves the player in the direction the camera is facing
    // Get camera yaw for movement direction
    float cameraYaw = camera.getYaw();

    // Calculate movement direction based on camera facing
    float moveX = 0.0f;
    float moveZ = 0.0f;

    if (keyState[SDL_SCANCODE_W]) {
        moveX += cos(glm::radians(cameraYaw));
        moveZ += sin(glm::radians(cameraYaw));
    }
    if (keyState[SDL_SCANCODE_S]) {
        moveX -= cos(glm::radians(cameraYaw));
        moveZ -= sin(glm::radians(cameraYaw));
    }
    if (keyState[SDL_SCANCODE_A]) {
        moveX += cos(glm::radians(cameraYaw - 90.0f));
        moveZ += sin(glm::radians(cameraYaw - 90.0f));
    }
    if (keyState[SDL_SCANCODE_D]) {
        moveX += cos(glm::radians(cameraYaw + 90.0f));
        moveZ += sin(glm::radians(cameraYaw + 90.0f));
    }

    // Calculate desired velocity for physics character controller
    glm::vec3 desiredVelocity(0.0f);
    if (moveX != 0.0f || moveZ != 0.0f) {
        glm::vec3 moveDir = glm::normalize(glm::vec3(moveX, 0.0f, moveZ));
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

    // Space to jump
    wantsJump = keyState[SDL_SCANCODE_SPACE];

    // Update physics character controller
    physics.updateCharacter(deltaTime, desiredVelocity, wantsJump);

    // Arrow keys orbit the camera around the player
    if (keyState[SDL_SCANCODE_UP]) {
        camera.orbitPitch(rotateSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_DOWN]) {
        camera.orbitPitch(-rotateSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_LEFT]) {
        camera.orbitYaw(-rotateSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_RIGHT]) {
        camera.orbitYaw(rotateSpeed * deltaTime);
    }

    // Q/E to zoom in/out
    if (keyState[SDL_SCANCODE_Q]) {
        camera.adjustDistance(-moveSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_E]) {
        camera.adjustDistance(moveSpeed * deltaTime);
    }
}

void Application::handleGamepadInput(float deltaTime) {
    if (!gamepad) return;

    if (thirdPersonMode) {
        handleThirdPersonGamepadInput(deltaTime);
    } else {
        handleFreeCameraGamepadInput(deltaTime);
    }

    // Triggers for time scale (works in both modes)
    float leftTrigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
    float rightTrigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;

    if (rightTrigger > 0.5f) {
        renderer.setTimeScale(renderer.getTimeScale() * (1.0f + deltaTime));
    }
    if (leftTrigger > 0.5f) {
        renderer.setTimeScale(renderer.getTimeScale() * (1.0f - deltaTime * 0.5f));
    }
}

void Application::handleFreeCameraGamepadInput(float deltaTime) {
    // Left stick for movement
    float leftX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float leftY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;

    // Apply deadzone
    if (std::abs(leftX) < stickDeadzone) leftX = 0.0f;
    if (std::abs(leftY) < stickDeadzone) leftY = 0.0f;

    // Left stick controls movement (Y is inverted - up is negative)
    camera.moveForward(-leftY * moveSpeed * deltaTime);
    camera.moveRight(leftX * moveSpeed * deltaTime);

    // Right stick for camera rotation
    float rightX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
    float rightY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;

    // Apply deadzone
    if (std::abs(rightX) < stickDeadzone) rightX = 0.0f;
    if (std::abs(rightY) < stickDeadzone) rightY = 0.0f;

    // Right stick controls camera look (Y inverted for natural feel)
    camera.rotateYaw(rightX * gamepadLookSpeed * deltaTime);
    camera.rotatePitch(-rightY * gamepadLookSpeed * deltaTime);

    // Bumpers for vertical movement
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        camera.moveUp(moveSpeed * deltaTime);
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        camera.moveUp(-moveSpeed * deltaTime);
    }
}

void Application::handleThirdPersonGamepadInput(float deltaTime) {
    // Left stick moves player relative to camera facing
    float leftX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float leftY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;

    // Apply deadzone
    if (std::abs(leftX) < stickDeadzone) leftX = 0.0f;
    if (std::abs(leftY) < stickDeadzone) leftY = 0.0f;

    glm::vec3 desiredVelocity(0.0f);
    if (leftX != 0.0f || leftY != 0.0f) {
        float cameraYaw = camera.getYaw();

        // Calculate movement direction based on camera facing
        float moveX = -leftY * cos(glm::radians(cameraYaw)) + leftX * cos(glm::radians(cameraYaw + 90.0f));
        float moveZ = -leftY * sin(glm::radians(cameraYaw)) + leftX * sin(glm::radians(cameraYaw + 90.0f));

        glm::vec3 moveDir = glm::normalize(glm::vec3(moveX, 0.0f, moveZ));
        desiredVelocity = moveDir * moveSpeed;

        // Rotate player to face movement direction
        float newYaw = glm::degrees(atan2(moveDir.x, moveDir.z));
        float currentYaw = player.getYaw();
        float yawDiff = newYaw - currentYaw;
        while (yawDiff > 180.0f) yawDiff -= 360.0f;
        while (yawDiff < -180.0f) yawDiff += 360.0f;
        player.rotate(yawDiff * 10.0f * deltaTime);
    }

    // A button (South) to jump
    bool gamepadJump = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);

    // Update physics character controller
    physics.updateCharacter(deltaTime, desiredVelocity, gamepadJump);

    // Right stick orbits camera around player
    float rightX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
    float rightY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;

    // Apply deadzone
    if (std::abs(rightX) < stickDeadzone) rightX = 0.0f;
    if (std::abs(rightY) < stickDeadzone) rightY = 0.0f;

    camera.orbitYaw(rightX * gamepadLookSpeed * deltaTime);
    camera.orbitPitch(-rightY * gamepadLookSpeed * deltaTime);

    // Bumpers for camera distance
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        camera.adjustDistance(moveSpeed * deltaTime);
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        camera.adjustDistance(-moveSpeed * deltaTime);
    }
}

void Application::openGamepad(SDL_JoystickID id) {
    gamepad = SDL_OpenGamepad(id);
    if (gamepad) {
        SDL_Log("Gamepad connected: %s", SDL_GetGamepadName(gamepad));
    }
}

void Application::closeGamepad() {
    if (gamepad) {
        SDL_Log("Gamepad disconnected");
        SDL_CloseGamepad(gamepad);
        gamepad = nullptr;
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

    // Create terrain heightmap for the ground disc (radius 50)
    physics.createTerrainDisc(50.0f, 0.0f);

    // Get scene objects from renderer to create physics bodies
    // Scene object layout from SceneBuilder:
    // 0: Ground disc (static terrain - already created above)
    // 1: Wooden crate 1 at (2.0, 0.5, 0.0) - unit cube (half extents 0.5)
    // 2: Rotated wooden crate at (-1.5, 0.5, 1.0)
    // 3: Polished metal sphere at (0.0, 0.5, -2.0) - radius 0.5
    // 4: Rough metal sphere at (-3.0, 0.5, -1.0)
    // 5: Polished metal cube at (3.0, 0.5, -2.0)
    // 6: Brushed metal cube at (-3.0, 0.5, -3.0)
    // 7: Emissive sphere at (2.0, 1.3, 0.0) - small, scaled 0.3 (radius ~0.15)
    // 8: Player capsule (handled by character controller)

    const size_t numSceneObjects = 9;  // Including ground and player
    scenePhysicsBodies.resize(numSceneObjects, INVALID_BODY_ID);

    // Box half-extent for unit cube
    glm::vec3 cubeHalfExtents(0.5f, 0.5f, 0.5f);
    float boxMass = 10.0f;
    float sphereMass = 5.0f;

    // Create physics bodies for dynamic objects
    // Spawn objects slightly above ground (Y offset +0.1) to let them settle
    const float spawnOffset = 0.1f;

    // Index 1: Wooden crate 1
    scenePhysicsBodies[1] = physics.createBox(glm::vec3(2.0f, 0.5f + spawnOffset, 0.0f), cubeHalfExtents, boxMass);

    // Index 2: Rotated wooden crate
    scenePhysicsBodies[2] = physics.createBox(glm::vec3(-1.5f, 0.5f + spawnOffset, 1.0f), cubeHalfExtents, boxMass);

    // Index 3: Polished metal sphere (radius 0.5)
    scenePhysicsBodies[3] = physics.createSphere(glm::vec3(0.0f, 0.5f + spawnOffset, -2.0f), 0.5f, sphereMass);

    // Index 4: Rough metal sphere
    scenePhysicsBodies[4] = physics.createSphere(glm::vec3(-3.0f, 0.5f + spawnOffset, -1.0f), 0.5f, sphereMass);

    // Index 5: Polished metal cube
    scenePhysicsBodies[5] = physics.createBox(glm::vec3(3.0f, 0.5f + spawnOffset, -2.0f), cubeHalfExtents, boxMass);

    // Index 6: Brushed metal cube
    scenePhysicsBodies[6] = physics.createBox(glm::vec3(-3.0f, 0.5f + spawnOffset, -3.0f), cubeHalfExtents, boxMass);

    // Index 7: Small emissive sphere (scaled 0.3, so radius ~0.15)
    scenePhysicsBodies[7] = physics.createSphere(glm::vec3(2.0f, 1.3f + spawnOffset, 0.0f), 0.15f, 1.0f);

    // Create character controller for player slightly above ground
    physics.createCharacter(glm::vec3(0.0f, spawnOffset, 0.0f), Player::CAPSULE_HEIGHT, Player::CAPSULE_RADIUS);

    SDL_Log("Physics initialized with %d active bodies", physics.getActiveBodyCount());
}

void Application::updatePhysicsToScene() {
    // Update scene object transforms from physics simulation
    auto& sceneObjects = renderer.getSceneObjects();

    for (size_t i = 1; i < scenePhysicsBodies.size() && i < sceneObjects.size(); i++) {
        PhysicsBodyID bodyID = scenePhysicsBodies[i];
        if (bodyID == INVALID_BODY_ID) continue;

        // Skip player object (handled separately)
        if (i == renderer.getPlayerObjectIndex()) continue;

        // Get transform from physics
        glm::mat4 physicsTransform = physics.getBodyTransform(bodyID);

        // Update scene object transform
        sceneObjects[i].transform = physicsTransform;
    }
}
