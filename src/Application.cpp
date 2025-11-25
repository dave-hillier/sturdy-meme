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

        camera.setAspectRatio(static_cast<float>(renderer.getWidth()) / static_cast<float>(renderer.getHeight()));

        renderer.render(camera);

        // Update window title with FPS and time of day
        if (deltaTime > 0.0f) {
            smoothedFps = smoothedFps * 0.95f + (1.0f / deltaTime) * 0.05f;
        }
        float timeOfDay = renderer.getTimeOfDay();
        int hours = static_cast<int>(timeOfDay * 24.0f);
        int minutes = static_cast<int>((timeOfDay * 24.0f - hours) * 60.0f);
        char title[64];
        snprintf(title, sizeof(title), "Vulkan Game - FPS: %.0f | Time: %02d:%02d", smoothedFps, hours, minutes);
        SDL_SetWindowTitle(window, title);
    }

    renderer.waitIdle();
}

void Application::shutdown() {
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
                else if (event.key.scancode == SDL_SCANCODE_5) {
                    renderer.toggleShadowMode();
                    SDL_Log("Shadow mode: %s", renderer.isUsingFrustumFittedShadows() ? "Frustum-fitted" : "Fixed");
                }
                else if (event.key.scancode == SDL_SCANCODE_6) {
                    renderer.toggleCascadeDebug();
                    SDL_Log("Cascade debug visualization: %s", renderer.isShowingCascadeDebug() ? "ON" : "OFF");
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
                break;
            default:
                break;
        }
    }
}

void Application::handleInput(float deltaTime) {
    const bool* keyState = SDL_GetKeyboardState(nullptr);

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

void Application::handleGamepadInput(float deltaTime) {
    if (!gamepad) return;

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

    // Triggers for time scale
    float leftTrigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
    float rightTrigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;

    if (rightTrigger > 0.5f) {
        renderer.setTimeScale(renderer.getTimeScale() * (1.0f + deltaTime));
    }
    if (leftTrigger > 0.5f) {
        renderer.setTimeScale(renderer.getTimeScale() * (1.0f - deltaTime * 0.5f));
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
