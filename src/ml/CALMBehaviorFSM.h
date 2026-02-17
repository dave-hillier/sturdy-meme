#pragma once

#include "CALMController.h"
#include "CALMHighLevelController.h"
#include "Tensor.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace ml {

// A single state in the CALM behavior FSM.
// Each state drives the CALMController with either:
//   - A fixed behavior tag (sampled from the latent library), OR
//   - An HLC that dynamically produces latent codes
struct CALMFSMState {
    std::string name;

    // Behavior source (one or the other):
    std::string behaviorTag;                        // Fixed behavior from latent library
    std::function<void(Tensor& outLatent)> hlcEval; // Dynamic HLC evaluation (optional)

    // Transition configuration
    int blendSteps = 15;  // Steps to interpolate latent on entry

    // Exit condition: returns true when the state should transition
    std::function<bool()> exitCondition;

    // Next state to transition to (empty = stay in current state)
    std::string nextState;
};

// CALM Behavior FSM — composes complex behavior sequences from latent primitives.
//
// Each state either selects a fixed behavior from the latent library or uses
// an HLC to dynamically produce latent codes. Transitions happen when exit
// conditions are met, with smooth latent interpolation between states.
//
// Example "Stealth Attack" FSM:
//   [crouch_walk] --(dist < 3m)--> [sprint] --(dist < 1m)--> [strike] --(done)--> [idle]
//
class CALMBehaviorFSM {
public:
    CALMBehaviorFSM() = default;

    // Set the CALMController this FSM drives
    void setController(CALMController* controller) { controller_ = controller; }

    // Add a state to the FSM
    void addState(CALMFSMState state);

    // Set the initial state (must be called before update)
    void start(const std::string& stateName);

    // Update the FSM: check exit conditions and transition
    // Call this once per frame, before CALMController::update()
    void update();

    // Force transition to a specific state
    void transitionTo(const std::string& stateName);

    // Get the current state name
    const std::string& currentStateName() const { return currentStateName_; }

    // Get the current state (nullptr if not started)
    const CALMFSMState* currentState() const;

    // Check if FSM is running
    bool isRunning() const { return !currentStateName_.empty() && controller_ != nullptr; }

    // Check if FSM has completed (current state has no next state and exit condition is met)
    bool isComplete() const { return complete_; }

    // Reset to stopped state
    void stop();

    // Get number of states
    size_t stateCount() const { return states_.size(); }

    // Check if a state exists
    bool hasState(const std::string& name) const {
        return stateMap_.count(name) > 0;
    }

private:
    CALMController* controller_ = nullptr;
    std::vector<CALMFSMState> states_;
    std::unordered_map<std::string, size_t> stateMap_;
    std::string currentStateName_;
    bool complete_ = false;

    void enterState(const std::string& stateName);
};

} // namespace ml
