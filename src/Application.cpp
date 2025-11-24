#include "Application.h"
#include <chrono>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

bool Application::init(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
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

    running = true;
    return true;
}

void Application::run() {
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        processEvents();
        handleInput(deltaTime);

        camera.setAspectRatio(static_cast<float>(renderer.getWidth()) / static_cast<float>(renderer.getHeight()));

        renderer.render(camera);
    }

    renderer.waitIdle();
}

void Application::shutdown() {
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
        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
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

    // Arrow keys for movement
    if (keyState[SDL_SCANCODE_UP]) {
        camera.moveForward(moveSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_DOWN]) {
        camera.moveForward(-moveSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_LEFT]) {
        camera.moveRight(-moveSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_RIGHT]) {
        camera.moveRight(moveSpeed * deltaTime);
    }

    // WASD for rotation
    if (keyState[SDL_SCANCODE_W]) {
        camera.rotatePitch(rotateSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_S]) {
        camera.rotatePitch(-rotateSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_A]) {
        camera.rotateYaw(-rotateSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_D]) {
        camera.rotateYaw(rotateSpeed * deltaTime);
    }

    // Page Up/Down for vertical movement
    if (keyState[SDL_SCANCODE_PAGEUP]) {
        camera.moveUp(moveSpeed * deltaTime);
    }
    if (keyState[SDL_SCANCODE_PAGEDOWN]) {
        camera.moveUp(-moveSpeed * deltaTime);
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
