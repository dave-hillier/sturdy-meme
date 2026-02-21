#pragma once

#include "../physics/ArticulatedBody.h"
#include "../physics/PhysicsSystem.h"

#include <SDL3/SDL.h>
#include <cstddef>
#include <string>
#include <vector>

// Real-time 2D visualization of training environments.
// Opens an SDL window and draws ragdolls as colored line skeletons,
// plus an overlay with training stats.
class TrainingVisualizer {
public:
    struct Config {
        int windowWidth = 1280;
        int windowHeight = 720;
        size_t gridCols = 4;    // environments per row in grid view
        size_t maxVisible = 8;  // max environments to render
        float cameraScale = 200.0f; // pixels per meter
        float groundY = 0.0f;
    };

    TrainingVisualizer();
    explicit TrainingVisualizer(const Config& config);
    ~TrainingVisualizer();

    TrainingVisualizer(const TrainingVisualizer&) = delete;
    TrainingVisualizer& operator=(const TrainingVisualizer&) = delete;

    bool isOpen() const { return window_ != nullptr; }

    // Process SDL events. Returns false if window was closed.
    bool pollEvents();

    // Begin a new frame (clear screen).
    void beginFrame();

    // Draw a single environment's ragdoll in its grid cell.
    // envIndex: which grid cell (0-based)
    void drawRagdoll(size_t envIndex,
                     const std::vector<ArticulatedBody::PartState>& states);

    // Draw the ground plane across all grid cells.
    void drawGround();

    // Draw training stats overlay.
    void drawStats(size_t iteration, float meanReward, float episodeLen,
                   float policyLoss, float valueLoss, size_t episodes);

    // Present the frame.
    void endFrame();

private:
    // Project 3D world position to 2D screen position within a grid cell.
    // Returns (screenX, screenY) in pixels.
    SDL_FPoint project(const glm::vec3& worldPos, size_t envIndex) const;

    // Draw a thick line (capsule body segment).
    void drawThickLine(float x1, float y1, float x2, float y2,
                       float thickness, Uint8 r, Uint8 g, Uint8 b);

    // Draw a small circle (joint).
    void drawCircle(float cx, float cy, float radius, Uint8 r, Uint8 g, Uint8 b);

    Config config_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;

    float cellWidth_ = 0.0f;
    float cellHeight_ = 0.0f;
    size_t gridRows_ = 0;

    // Parent indices for the 20-part humanoid skeleton
    static constexpr int PARENT_MAP[20] = {
        -1, 0, 1, 2, 3, 4,    // spine + head
        3, 6, 7, 8,           // left arm
        3, 10, 11, 12,        // right arm
        0, 14, 15,            // left leg
        0, 17, 18             // right leg
    };

    // Colors per body region
    struct Color { Uint8 r, g, b; };
    static constexpr Color COLORS[20] = {
        {255,255,100}, {255,255,100}, {255,255,100}, {255,255,100}, // spine
        {200,200,200}, {200,200,200},                                // neck+head
        {100,200,255}, {100,200,255}, {100,200,255}, {100,200,255}, // left arm
        {255,150,100}, {255,150,100}, {255,150,100}, {255,150,100}, // right arm
        {100,255,150}, {100,255,150}, {100,255,150},                 // left leg
        {255,100,200}, {255,100,200}, {255,100,200},                 // right leg
    };
};
