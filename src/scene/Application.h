#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <optional>
#include "Renderer.h"
#include "Camera.h"
#include "Player.h"
#include "PhysicsSystem.h"
#include "PhysicsTerrainTileManager.h"
#include "ClothSimulation.h"
#include "GuiSystem.h"
#include "InputSystem.h"
#include "BreadcrumbTracker.h"

class Application {
public:
    Application() = default;
    ~Application() = default;

    bool init(const std::string& title, int width, int height);
    void run();
    void shutdown();

private:
    void processEvents();
    void applyInputToCamera();
    std::string getResourcePath();
    void initPhysics();
    void updatePhysicsToScene();
    void initFlag();
    void updateFlag(float deltaTime);
    void updateCameraOcclusion(float deltaTime);

    SDL_Window* window = nullptr;
    Renderer renderer;
    Camera camera;
    Player player;
    std::optional<PhysicsWorld> physics_;
    PhysicsTerrainTileManager physicsTerrainManager_;

    // Helper to access physics (assumes physics is initialized)
    PhysicsWorld& physics() { return *physics_; }

    // Input system
    InputSystem input;

    // Physics body IDs for scene objects (mapped to scene object indices)
    std::vector<PhysicsBodyID> scenePhysicsBodies;

    // Breadcrumb tracker for fast respawn (Ghost of Tsushima optimization)
    // Tracks safe player positions so respawns load most content from cache
    BreadcrumbTracker breadcrumbTracker;

    // Flag simulation
    ClothSimulation clothSim;
    size_t flagClothSceneIndex = 0;
    size_t flagPoleSceneIndex = 0;

    // GUI system
    GuiSystem gui;
    float currentFps = 60.0f;
    float lastDeltaTime = 0.016f;

    // Camera occlusion tracking
    std::unordered_set<PhysicsBodyID> occludingBodies;
    static constexpr float occlusionFadeSpeed = 8.0f;
    static constexpr float occludedOpacity = 0.3f;

    bool running = false;
    // Walk speed matches animation root motion: 158.42 cm / 1.10s * 0.01 scale = 1.44 m/s
    float moveSpeed = 1.44f;
    // Run speed matches animation root motion: 278.32 cm / 0.70s * 0.01 scale = 3.98 m/s
    float sprintSpeed = 3.98f;
};
