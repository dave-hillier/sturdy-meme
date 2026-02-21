#include "TrainingVisualizer.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

TrainingVisualizer::TrainingVisualizer()
    : TrainingVisualizer(Config{})
{}

TrainingVisualizer::TrainingVisualizer(const Config& config)
    : config_(config)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TrainingVisualizer: SDL_Init failed: %s",
                     SDL_GetError());
        return;
    }

    if (!SDL_CreateWindowAndRenderer("UniCon Training",
                                     config_.windowWidth, config_.windowHeight,
                                     SDL_WINDOW_RESIZABLE,
                                     &window_, &renderer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TrainingVisualizer: window creation failed: %s",
                     SDL_GetError());
        return;
    }

    // Compute grid layout
    size_t visible = config_.maxVisible;
    gridRows_ = (visible + config_.gridCols - 1) / config_.gridCols;
    cellWidth_ = static_cast<float>(config_.windowWidth) / static_cast<float>(config_.gridCols);
    cellHeight_ = static_cast<float>(config_.windowHeight - 40) / static_cast<float>(gridRows_); // 40px for stats bar

    SDL_Log("TrainingVisualizer: %zux%zu grid (%zu envs), cell=%.0fx%.0f",
            config_.gridCols, gridRows_, visible, cellWidth_, cellHeight_);
}

TrainingVisualizer::~TrainingVisualizer() {
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

bool TrainingVisualizer::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            SDL_DestroyRenderer(renderer_);
            SDL_DestroyWindow(window_);
            renderer_ = nullptr;
            window_ = nullptr;
            return false;
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED) {
            config_.windowWidth = event.window.data1;
            config_.windowHeight = event.window.data2;
            cellWidth_ = static_cast<float>(config_.windowWidth) / static_cast<float>(config_.gridCols);
            cellHeight_ = static_cast<float>(config_.windowHeight - 40) / static_cast<float>(gridRows_);
        }
        if (event.type == SDL_EVENT_KEY_DOWN) {
            if (event.key.key == SDLK_EQUALS || event.key.key == SDLK_PLUS) {
                config_.cameraScale *= 1.2f;
            } else if (event.key.key == SDLK_MINUS) {
                config_.cameraScale /= 1.2f;
            }
        }
    }
    return true;
}

void TrainingVisualizer::beginFrame() {
    if (!renderer_) return;
    SDL_SetRenderDrawColor(renderer_, 30, 30, 40, 255);
    SDL_RenderClear(renderer_);
}

void TrainingVisualizer::drawGround() {
    if (!renderer_) return;

    SDL_SetRenderDrawColor(renderer_, 60, 80, 60, 255);

    for (size_t i = 0; i < config_.maxVisible; ++i) {
        size_t col = i % config_.gridCols;
        size_t row = i / config_.gridCols;
        if (row >= gridRows_) break;

        float cellX = static_cast<float>(col) * cellWidth_;
        float cellY = static_cast<float>(row) * cellHeight_;

        // Ground line: Y=0 in world maps to bottom portion of cell
        float groundScreenY = cellY + cellHeight_ * 0.85f;
        SDL_RenderLine(renderer_, cellX + 2, groundScreenY,
                       cellX + cellWidth_ - 2, groundScreenY);

        // Cell border
        SDL_SetRenderDrawColor(renderer_, 50, 50, 60, 255);
        SDL_FRect border = {cellX, cellY, cellWidth_, cellHeight_};
        SDL_RenderRect(renderer_, &border);
        SDL_SetRenderDrawColor(renderer_, 60, 80, 60, 255);
    }
}

void TrainingVisualizer::drawRagdoll(size_t envIndex,
                                      const std::vector<ArticulatedBody::PartState>& states) {
    if (!renderer_ || envIndex >= config_.maxVisible || states.empty()) return;

    // Draw skeleton lines between parent-child body parts
    for (size_t i = 0; i < states.size() && i < 20; ++i) {
        int parent = PARENT_MAP[i];
        if (parent < 0) continue;

        SDL_FPoint childPt = project(states[i].position, envIndex);
        SDL_FPoint parentPt = project(states[parent].position, envIndex);

        const auto& c = COLORS[i];
        drawThickLine(parentPt.x, parentPt.y, childPt.x, childPt.y, 3.0f, c.r, c.g, c.b);
    }

    // Draw joints as small circles
    for (size_t i = 0; i < states.size() && i < 20; ++i) {
        SDL_FPoint pt = project(states[i].position, envIndex);
        const auto& c = COLORS[i];
        drawCircle(pt.x, pt.y, 4.0f, c.r, c.g, c.b);
    }

    // Draw root as larger circle
    if (!states.empty()) {
        SDL_FPoint root = project(states[0].position, envIndex);
        drawCircle(root.x, root.y, 6.0f, 255, 255, 255);
    }
}

void TrainingVisualizer::drawStats(size_t iteration, float meanReward, float episodeLen,
                                    float policyLoss, float valueLoss, size_t episodes) {
    if (!renderer_) return;

    // Stats bar at bottom
    float barY = static_cast<float>(config_.windowHeight - 38);
    SDL_SetRenderDrawColor(renderer_, 20, 20, 30, 255);
    SDL_FRect bar = {0, barY, static_cast<float>(config_.windowWidth), 38};
    SDL_RenderFillRect(renderer_, &bar);

    // Reward bar visualization
    float barWidth = std::clamp(meanReward * 200.0f, 0.0f, 300.0f);
    Uint8 barR = meanReward < 0.3f ? 255 : (meanReward < 0.7f ? 255 : 100);
    Uint8 barG = meanReward < 0.3f ? 80 : (meanReward < 0.7f ? 200 : 255);
    SDL_SetRenderDrawColor(renderer_, barR, barG, 80, 255);
    SDL_FRect rewardBar = {10, barY + 5, barWidth, 12};
    SDL_RenderFillRect(renderer_, &rewardBar);

    // Iteration marker dots
    SDL_SetRenderDrawColor(renderer_, 180, 180, 200, 255);
    for (size_t d = 0; d < std::min(iteration, size_t(50)); ++d) {
        float dx = 320.0f + static_cast<float>(d) * 6.0f;
        SDL_FRect dot = {dx, barY + 8, 4, 4};
        SDL_RenderFillRect(renderer_, &dot);
    }
}

void TrainingVisualizer::endFrame() {
    if (!renderer_) return;
    SDL_RenderPresent(renderer_);
}

SDL_FPoint TrainingVisualizer::project(const glm::vec3& worldPos, size_t envIndex) const {
    size_t col = envIndex % config_.gridCols;
    size_t row = envIndex / config_.gridCols;

    // Cell center
    float cellCenterX = (static_cast<float>(col) + 0.5f) * cellWidth_;
    float cellBottomY = static_cast<float>(row) * cellHeight_ + cellHeight_ * 0.85f;

    // Side view: X maps to screen X, Y maps to screen Y (inverted)
    float screenX = cellCenterX + worldPos.x * config_.cameraScale;
    float screenY = cellBottomY - worldPos.y * config_.cameraScale;

    return {screenX, screenY};
}

void TrainingVisualizer::drawThickLine(float x1, float y1, float x2, float y2,
                                        float thickness, Uint8 r, Uint8 g, Uint8 b) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);

    // Draw multiple parallel lines for thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    float nx = -dy / len;
    float ny = dx / len;

    int halfT = static_cast<int>(thickness * 0.5f);
    for (int offset = -halfT; offset <= halfT; ++offset) {
        float ox = nx * static_cast<float>(offset);
        float oy = ny * static_cast<float>(offset);
        SDL_RenderLine(renderer_, x1 + ox, y1 + oy, x2 + ox, y2 + oy);
    }
}

void TrainingVisualizer::drawCircle(float cx, float cy, float radius, Uint8 r, Uint8 g, Uint8 b) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);

    // Bresenham-style circle with filled horizontal lines
    int ir = static_cast<int>(radius);
    for (int dy = -ir; dy <= ir; ++dy) {
        int dx = static_cast<int>(std::sqrt(static_cast<float>(ir * ir - dy * dy)));
        SDL_RenderLine(renderer_, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}
