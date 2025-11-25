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
    void handleGamepadInput(float deltaTime);
    void processEvents();
    void openGamepad(SDL_JoystickID id);
    void closeGamepad();
    std::string getResourcePath();

    SDL_Window* window = nullptr;
    SDL_Gamepad* gamepad = nullptr;
    Renderer renderer;
    Camera camera;

    bool running = false;
    float moveSpeed = 3.0f;
    float rotateSpeed = 60.0f;

    static constexpr float stickDeadzone = 0.15f;
    static constexpr float gamepadLookSpeed = 120.0f;
};
