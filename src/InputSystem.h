#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

// Forward declarations
class Camera;
class GuiSystem;

// Input system for handling keyboard, mouse, and gamepad input
// Centralizes all input state management and provides a clean interface
// for querying input state without coupling to SDL directly.
class InputSystem {
public:
    InputSystem() = default;
    ~InputSystem() = default;

    // Initialize the input system
    bool init();

    // Shutdown and release resources
    void shutdown();

    // Process SDL events for input (call this with each event from the main loop)
    // Returns true if the event was handled as an input event
    bool processEvent(const SDL_Event& event);

    // Update continuous input state (call once per frame before reading input)
    // deltaTime is used for input that accumulates over time
    // cameraYaw is needed to calculate movement direction relative to camera facing
    void update(float deltaTime, float cameraYaw);

    // Query if GUI wants input (blocks game input)
    void setGuiSystem(GuiSystem* gui) { guiSystem = gui; }

    // Camera mode
    bool isThirdPersonMode() const { return thirdPersonMode; }
    void setThirdPersonMode(bool enabled) { thirdPersonMode = enabled; modeSwitchedThisFrame = true; }
    void toggleCameraMode() { thirdPersonMode = !thirdPersonMode; modeSwitchedThisFrame = true; }

    // Returns true if camera mode was just switched this frame (resets after checking)
    bool wasModeSwitchedThisFrame() { bool result = modeSwitchedThisFrame; modeSwitchedThisFrame = false; return result; }

    // Movement input (accumulated from keyboard + gamepad)
    glm::vec3 getMovementDirection() const { return movementDirection; }

    // Jump input (true only on initial press)
    bool wantsJump() const { return jumpRequested; }

    // Sprint input (Shift key held or left stick toggle on gamepad)
    bool isSprinting() const { return sprinting; }

    // Camera control input
    float getCameraYawInput() const { return cameraYawInput; }
    float getCameraPitchInput() const { return cameraPitchInput; }
    float getCameraZoomInput() const { return cameraZoomInput; }

    // Free camera movement (for fly camera mode)
    float getFreeCameraForward() const { return freeCameraForward; }
    float getFreeCameraRight() const { return freeCameraRight; }
    float getFreeCameraUp() const { return freeCameraUp; }

    // Time scale input from gamepad triggers
    float getTimeScaleInput() const { return timeScaleInput; }

    // Check if gamepad is connected
    bool hasGamepad() const { return gamepad != nullptr; }

    // Access to raw keyboard state for special cases
    bool isKeyPressed(SDL_Scancode scancode) const;

    // Orientation lock input (for strafe mode)
    bool wantsOrientationLockToggle() const { return orientationLockToggleRequested; }
    bool isOrientationLockHeld() const { return orientationLockHeld; }

    // Settings
    void setMoveSpeed(float speed) { moveSpeed = speed; }
    void setRotateSpeed(float speed) { rotateSpeed = speed; }
    float getMoveSpeed() const { return moveSpeed; }
    float getRotateSpeed() const { return rotateSpeed; }

private:
    // Gamepad management
    void openGamepad(SDL_JoystickID id);
    void closeGamepad();
    void scanForGamepads();

    // Input processing helpers
    void processKeyboardInput(float deltaTime, float cameraYaw);
    void processGamepadInput(float deltaTime, float cameraYaw);
    void processFreeCameraKeyboard(float deltaTime, const bool* keyState);
    void processThirdPersonKeyboard(float deltaTime, float cameraYaw, const bool* keyState);
    void processFreeCameraGamepad(float deltaTime);
    void processThirdPersonGamepad(float deltaTime, float cameraYaw);

    // Check if GUI is consuming input
    bool isGuiBlocking() const;

    // SDL gamepad handle
    SDL_Gamepad* gamepad = nullptr;

    // GUI system reference for input blocking
    GuiSystem* guiSystem = nullptr;

    // Cached keyboard state pointer
    const bool* keyboardState = nullptr;

    // Camera mode
    bool thirdPersonMode = false;
    bool modeSwitchedThisFrame = false;

    // Accumulated movement direction (normalized when read)
    glm::vec3 movementDirection{0.0f};

    // Jump input state
    bool jumpRequested = false;
    bool keyboardJumpHeld = false;
    bool gamepadJumpHeld = false;

    // Sprint input state
    bool sprinting = false;
    bool gamepadSprintToggle = false;  // Toggled by left stick press

    // Camera control input (for orbit/free look)
    float cameraYawInput = 0.0f;
    float cameraPitchInput = 0.0f;
    float cameraZoomInput = 0.0f;

    // Free camera movement
    float freeCameraForward = 0.0f;
    float freeCameraRight = 0.0f;
    float freeCameraUp = 0.0f;

    // Time scale input
    float timeScaleInput = 0.0f;

    // Orientation lock input state
    bool orientationLockToggleRequested = false;
    bool orientationLockHeld = false;
    bool keyboardLockHeld = false;
    bool gamepadLockToggleHeld = false;

    // Input settings
    float moveSpeed = 3.0f;
    float rotateSpeed = 60.0f;

    // Gamepad constants
    static constexpr float stickDeadzone = 0.15f;
    static constexpr float gamepadLookSpeed = 120.0f;
};
