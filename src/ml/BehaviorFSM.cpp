#include "BehaviorFSM.h"
#include <SDL3/SDL_log.h>

namespace ml {

void BehaviorFSM::addState(FSMState state) {
    std::string name = state.name;
    stateMap_[name] = states_.size();
    states_.push_back(std::move(state));
}

void BehaviorFSM::start(const std::string& stateName) {
    if (!controller_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "BehaviorFSM: cannot start without a controller");
        return;
    }

    complete_ = false;
    enterState(stateName);
}

void BehaviorFSM::update() {
    if (!isRunning() || complete_) return;

    const FSMState* state = currentState();
    if (!state) return;

    // If the state has a task controller, evaluate it each frame to update the latent
    if (state->hlcEval) {
        Tensor latent;
        state->hlcEval(latent);
        controller_->setLatent(latent);
    }

    // Check exit condition
    if (state->exitCondition && state->exitCondition()) {
        if (!state->nextState.empty()) {
            SDL_Log("BehaviorFSM: '%s' -> '%s'",
                    currentStateName_.c_str(), state->nextState.c_str());
            enterState(state->nextState);
        } else {
            // No next state — FSM is complete
            SDL_Log("BehaviorFSM: '%s' completed (terminal state)",
                    currentStateName_.c_str());
            complete_ = true;
        }
    }
}

void BehaviorFSM::transitionTo(const std::string& stateName) {
    if (!controller_) return;
    complete_ = false;
    enterState(stateName);
}

const FSMState* BehaviorFSM::currentState() const {
    auto it = stateMap_.find(currentStateName_);
    if (it == stateMap_.end()) return nullptr;
    return &states_[it->second];
}

void BehaviorFSM::stop() {
    currentStateName_.clear();
    complete_ = false;
}

void BehaviorFSM::enterState(const std::string& stateName) {
    auto it = stateMap_.find(stateName);
    if (it == stateMap_.end()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "BehaviorFSM: state '%s' not found", stateName.c_str());
        return;
    }

    currentStateName_ = stateName;
    const FSMState& state = states_[it->second];

    // Apply the state's behavior to the controller
    if (!state.behaviorTag.empty()) {
        // Use a fixed behavior from the latent library
        controller_->transitionToBehavior(state.behaviorTag, state.blendSteps);
    } else if (state.hlcEval) {
        // Task-driven state — evaluate immediately to set initial latent
        Tensor latent;
        state.hlcEval(latent);
        controller_->transitionToLatent(latent, state.blendSteps);
    }

    SDL_Log("BehaviorFSM: entered state '%s'", stateName.c_str());
}

} // namespace ml
