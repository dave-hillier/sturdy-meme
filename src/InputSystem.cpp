#include "InputSystem.h"
#include "GuiSystem.h"
#include <cmath>

bool InputSystem::init() {
    // Scan for connected gamepads
    scanForGamepads();
    return true;
}

void InputSystem::shutdown() {
    closeGamepad();
}

void InputSystem::scanForGamepads() {
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
}

bool InputSystem::processEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad) {
                openGamepad(event.gdevice.which);
            }
            return true;

        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad && SDL_GetGamepadID(gamepad) == event.gdevice.which) {
                closeGamepad();
            }
            return true;

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (event.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
                // Left stick click toggles sprint (both modes)
                gamepadSprintToggle = !gamepadSprintToggle;
                SDL_Log("Sprint: %s", gamepadSprintToggle ? "ON" : "OFF");
                return true;
            }
            if (event.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
                // Right stick click toggles camera mode
                thirdPersonMode = !thirdPersonMode;
                modeSwitchedThisFrame = true;
                SDL_Log("Camera mode: %s", thirdPersonMode ? "Third Person" : "Free Camera");
                return true;
            }
            break;

        case SDL_EVENT_KEY_DOWN:
            if (event.key.scancode == SDL_SCANCODE_TAB) {
                thirdPersonMode = !thirdPersonMode;
                modeSwitchedThisFrame = true;
                SDL_Log("Camera mode: %s", thirdPersonMode ? "Third Person" : "Free Camera");
                return true;
            }
            break;

        default:
            break;
    }

    return false;
}

void InputSystem::update(float deltaTime, float cameraYaw) {
    // Reset input accumulators
    movementDirection = glm::vec3(0.0f);
    jumpRequested = false;
    cameraYawInput = 0.0f;
    cameraPitchInput = 0.0f;
    cameraZoomInput = 0.0f;
    freeCameraForward = 0.0f;
    freeCameraRight = 0.0f;
    freeCameraUp = 0.0f;
    timeScaleInput = 0.0f;
    orientationLockToggleRequested = false;

    // Skip game input if GUI wants it
    if (isGuiBlocking()) {
        return;
    }

    // Get keyboard state
    keyboardState = SDL_GetKeyboardState(nullptr);

    // Process keyboard input
    processKeyboardInput(deltaTime, cameraYaw);

    // Process gamepad input
    processGamepadInput(deltaTime, cameraYaw);
}

bool InputSystem::isKeyPressed(SDL_Scancode scancode) const {
    if (!keyboardState) return false;
    return keyboardState[scancode];
}

bool InputSystem::isGuiBlocking() const {
    if (guiSystem) {
        return guiSystem->wantsInput();
    }
    return false;
}

void InputSystem::processKeyboardInput(float deltaTime, float cameraYaw) {
    if (!keyboardState) return;

    if (thirdPersonMode) {
        processThirdPersonKeyboard(deltaTime, cameraYaw, keyboardState);
    } else {
        processFreeCameraKeyboard(deltaTime, keyboardState);
    }
}

void InputSystem::processFreeCameraKeyboard(float deltaTime, const bool* keyState) {
    // Left Shift for sprint (10x speed)
    sprinting = keyState[SDL_SCANCODE_LSHIFT];
    float effectiveSpeed = sprinting ? moveSpeed * 10.0f : moveSpeed;

    // WASD for movement (standard FPS controls)
    if (keyState[SDL_SCANCODE_W]) {
        freeCameraForward += effectiveSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_S]) {
        freeCameraForward -= effectiveSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_A]) {
        freeCameraRight -= effectiveSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_D]) {
        freeCameraRight += effectiveSpeed * deltaTime;
    }

    // Arrow keys for camera rotation
    if (keyState[SDL_SCANCODE_UP]) {
        cameraPitchInput += rotateSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_DOWN]) {
        cameraPitchInput -= rotateSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_LEFT]) {
        cameraYawInput -= rotateSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_RIGHT]) {
        cameraYawInput += rotateSpeed * deltaTime;
    }

    // Space for up, Left Ctrl/Q for down (fly camera)
    if (keyState[SDL_SCANCODE_SPACE]) {
        freeCameraUp += effectiveSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_LCTRL] || keyState[SDL_SCANCODE_Q]) {
        freeCameraUp -= effectiveSpeed * deltaTime;
    }
}

void InputSystem::processThirdPersonKeyboard(float deltaTime, float cameraYaw, const bool* keyState) {
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

    // Accumulate movement direction
    if (moveX != 0.0f || moveZ != 0.0f) {
        movementDirection += glm::vec3(moveX, 0.0f, moveZ);
    }

    // Space to jump (only on initial press, not while held)
    bool spacePressed = keyState[SDL_SCANCODE_SPACE];
    if (spacePressed && !keyboardJumpHeld) {
        jumpRequested = true;
    }
    keyboardJumpHeld = spacePressed;

    // Arrow keys orbit the camera around the player
    if (keyState[SDL_SCANCODE_UP]) {
        cameraPitchInput += rotateSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_DOWN]) {
        cameraPitchInput -= rotateSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_LEFT]) {
        cameraYawInput -= rotateSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_RIGHT]) {
        cameraYawInput += rotateSpeed * deltaTime;
    }

    // Q/E to zoom in/out
    if (keyState[SDL_SCANCODE_Q]) {
        cameraZoomInput -= moveSpeed * deltaTime;
    }
    if (keyState[SDL_SCANCODE_E]) {
        cameraZoomInput += moveSpeed * deltaTime;
    }

    // Left Shift to sprint (held)
    sprinting = keyState[SDL_SCANCODE_LSHIFT] || gamepadSprintToggle;

    // Caps Lock to toggle orientation lock (only on initial press)
    bool capsPressed = keyState[SDL_SCANCODE_CAPSLOCK];
    if (capsPressed && !keyboardLockHeld) {
        orientationLockToggleRequested = true;
    }
    keyboardLockHeld = capsPressed;

    // Middle mouse button to hold orientation lock
    Uint32 mouseState = SDL_GetMouseState(nullptr, nullptr);
    orientationLockHeld = (mouseState & SDL_BUTTON_MMASK) != 0;
}

void InputSystem::processGamepadInput(float deltaTime, float cameraYaw) {
    if (!gamepad) return;

    if (thirdPersonMode) {
        processThirdPersonGamepad(deltaTime, cameraYaw);
    } else {
        processFreeCameraGamepad(deltaTime);
    }

    // Triggers for time scale (works in both modes)
    float leftTrigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
    float rightTrigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;

    if (rightTrigger > 0.5f) {
        timeScaleInput = 1.0f + deltaTime;  // Speed up
    }
    if (leftTrigger > 0.5f) {
        timeScaleInput = 1.0f - deltaTime * 0.5f;  // Slow down
    }
}

void InputSystem::processFreeCameraGamepad(float deltaTime) {
    // Apply sprint from toggle (left stick click)
    sprinting = gamepadSprintToggle;
    float effectiveSpeed = sprinting ? moveSpeed * 10.0f : moveSpeed;

    // Left stick for movement
    float leftX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float leftY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;

    // Apply deadzone
    if (std::abs(leftX) < stickDeadzone) leftX = 0.0f;
    if (std::abs(leftY) < stickDeadzone) leftY = 0.0f;

    // Left stick controls movement (Y is inverted - up is negative)
    freeCameraForward += -leftY * effectiveSpeed * deltaTime;
    freeCameraRight += leftX * effectiveSpeed * deltaTime;

    // Right stick for camera rotation
    float rightX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
    float rightY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;

    // Apply deadzone
    if (std::abs(rightX) < stickDeadzone) rightX = 0.0f;
    if (std::abs(rightY) < stickDeadzone) rightY = 0.0f;

    // Right stick controls camera look (Y inverted for natural feel)
    cameraYawInput += rightX * gamepadLookSpeed * deltaTime;
    cameraPitchInput += -rightY * gamepadLookSpeed * deltaTime;

    // Bumpers for vertical movement
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        freeCameraUp += effectiveSpeed * deltaTime;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        freeCameraUp -= effectiveSpeed * deltaTime;
    }
}

void InputSystem::processThirdPersonGamepad(float deltaTime, float cameraYaw) {
    // Left stick moves player relative to camera facing
    float leftX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float leftY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;

    // Apply deadzone
    if (std::abs(leftX) < stickDeadzone) leftX = 0.0f;
    if (std::abs(leftY) < stickDeadzone) leftY = 0.0f;

    // Accumulate movement direction from gamepad
    if (leftX != 0.0f || leftY != 0.0f) {
        // Calculate movement direction based on camera facing
        float moveX = -leftY * cos(glm::radians(cameraYaw)) + leftX * cos(glm::radians(cameraYaw + 90.0f));
        float moveZ = -leftY * sin(glm::radians(cameraYaw)) + leftX * sin(glm::radians(cameraYaw + 90.0f));

        movementDirection += glm::vec3(moveX, 0.0f, moveZ);
    }

    // A button (South) to jump (only on initial press)
    bool aButtonPressed = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    if (aButtonPressed && !gamepadJumpHeld) {
        jumpRequested = true;
    }
    gamepadJumpHeld = aButtonPressed;

    // Right stick orbits camera around player
    float rightX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
    float rightY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;

    // Apply deadzone
    if (std::abs(rightX) < stickDeadzone) rightX = 0.0f;
    if (std::abs(rightY) < stickDeadzone) rightY = 0.0f;

    cameraYawInput += rightX * gamepadLookSpeed * deltaTime;
    cameraPitchInput += -rightY * gamepadLookSpeed * deltaTime;

    // Bumpers for camera distance
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
        cameraZoomInput += moveSpeed * deltaTime;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
        cameraZoomInput -= moveSpeed * deltaTime;
    }

    // Left trigger to hold orientation lock
    float leftTrigger = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
    if (leftTrigger > 0.5f) {
        orientationLockHeld = true;
    }

    // B button (East) to toggle orientation lock (only on initial press)
    bool bButtonPressed = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST);
    if (bButtonPressed && !gamepadLockToggleHeld) {
        orientationLockToggleRequested = true;
    }
    gamepadLockToggleHeld = bButtonPressed;
}

void InputSystem::openGamepad(SDL_JoystickID id) {
    gamepad = SDL_OpenGamepad(id);
    if (gamepad) {
        SDL_Log("Gamepad connected: %s", SDL_GetGamepadName(gamepad));
    }
}

void InputSystem::closeGamepad() {
    if (gamepad) {
        SDL_Log("Gamepad disconnected");
        SDL_CloseGamepad(gamepad);
        gamepad = nullptr;
    }
}
