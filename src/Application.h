#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include "Renderer.h"
#include "Camera.h"
#include "Player.h"
#include "PhysicsSystem.h"
#include "ClothSimulation.h"

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
    void handleFreeCameraInput(float deltaTime, const bool* keyState);
    void handleThirdPersonInput(float deltaTime, const bool* keyState);
    void handleFreeCameraGamepadInput(float deltaTime);
    void handleThirdPersonGamepadInput(float deltaTime);
    void processEvents();
    void openGamepad(SDL_JoystickID id);
    void closeGamepad();
    std::string getResourcePath();
    void initPhysics();
    void updatePhysicsToScene();
    void initFlag();
    void updateFlag(float deltaTime);

    SDL_Window* window = nullptr;
    SDL_Gamepad* gamepad = nullptr;
    Renderer renderer;
    Camera camera;
    Player player;
    PhysicsWorld physics;

    // Physics body IDs for scene objects (mapped to scene object indices)
    std::vector<PhysicsBodyID> scenePhysicsBodies;

    // Flag simulation
    ClothSimulation clothSim;
    size_t flagClothSceneIndex = 0;
    size_t flagPoleSceneIndex = 0;

    bool running = false;
    bool thirdPersonMode = false;  // Toggle between free camera and third-person
    bool wantsJump = false;        // Jump input flag
    bool jumpHeld = false;         // Track if keyboard jump was held last frame
    bool gamepadJumpHeld = false;  // Track if gamepad jump was held last frame
    glm::vec3 accumulatedMoveDir{0.0f};  // Combined movement direction from all inputs
    float moveSpeed = 3.0f;
    float rotateSpeed = 60.0f;

    static constexpr float stickDeadzone = 0.15f;
    static constexpr float gamepadLookSpeed = 120.0f;
};
