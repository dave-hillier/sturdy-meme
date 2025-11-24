#pragma once

#include <SDL3/SDL.h>
#include <string>
#include "Renderer.h"
#include "Camera.h"

class Application {
public:
    Application() = default;
    ~Application() = default;

    bool init(const std::string& title, int width, int height);
    void run();
    void shutdown();

private:
    void handleInput(float deltaTime);
    void processEvents();
    std::string getResourcePath();

    SDL_Window* window = nullptr;
    Renderer renderer;
    Camera camera;

    bool running = false;
    float moveSpeed = 3.0f;
    float rotateSpeed = 60.0f;
};
