# Animation Integration Plan

## Available Animations

All animations use the Mixamo Y Bot skeleton and are located in `assets/characters/fbx/`.

### Currently Active (loaded by state machine)

| File | State | Duration | Root Motion | Notes |
|------|-------|----------|-------------|-------|
| `ss_idle.fbx` | idle | 3.53s | minimal | Combat-ready stance, looping |
| `ss_walk.fbx` | walk | 1.10s | 1.44 m/s | Looping, matches moveSpeed |
| `ss_run.fbx` | run | 0.70s | 3.98 m/s | Looping, matches sprintSpeed |
| `ss_jump.fbx` | jump | 0.83s | N/A | One-shot |

### Available for Integration

#### Combat - Attacks
| File | Purpose | Suggested State |
|------|---------|-----------------|
| `ss_attack1.fbx` | Basic sword swing | attack |
| `ss_attack2.fbx` | Alternative attack | attack_alt |
| `ss_attack3.fbx` | Third attack variant | attack_combo |
| `ss_slash1.fbx` | Horizontal slash | slash |
| `ss_slash2.fbx` | Alternative slash | slash_alt |
| `ss_kick.fbx` | Kick attack | kick |

#### Combat - Defense
| File | Purpose | Suggested State |
|------|---------|-----------------|
| `ss_block.fbx` | Raise shield | block_start |
| `ss_block_idle.fbx` | Hold block stance | block_hold |

#### Combat - Reactions
| File | Purpose | Suggested State |
|------|---------|-----------------|
| `ss_impact1.fbx` | Hit reaction | hit |
| `ss_impact2.fbx` | Alt hit reaction | hit_alt |
| `ss_death1.fbx` | Death animation | death |
| `ss_death2.fbx` | Alt death | death_alt |

#### Movement - Advanced
| File | Purpose | Suggested State |
|------|---------|-----------------|
| `ss_strafe_left.fbx` | Strafe left | strafe_left |
| `ss_strafe_right.fbx` | Strafe right | strafe_right |
| `ss_turn_left.fbx` | Turn in place left | turn_left |
| `ss_turn_right.fbx` | Turn in place right | turn_right |
| `ss_turn_180.fbx` | Quick 180 turn | turn_180 |

#### Special States
| File | Purpose | Suggested State |
|------|---------|-----------------|
| `ss_crouch.fbx` | Enter crouch | crouch_start |
| `ss_crouch_idle.fbx` | Crouched idle | crouch_idle |
| `ss_casting.fbx` | Magic cast | cast |
| `ss_power_up.fbx` | Charge/power up | charge |
| `ss_sheath.fbx` | Sheathe weapon | sheath |

## Implementation Roadmap

### Phase 1: Combat System (Priority)
1. Add attack input handling (mouse click / key)
2. Create `attack` state in AnimationStateMachine
3. Implement attack → idle transition on animation end
4. Add combo system: attack1 → attack2 → attack3 timing windows

### Phase 2: Defense System
1. Add block input (hold right mouse / key)
2. Create `block_start` → `block_hold` → `block_end` states
3. Implement hit reactions when blocking

### Phase 3: Advanced Locomotion
1. Add strafing when moving sideways while locked-on
2. Implement turn-in-place when stationary and rotating
3. Add 180 turn when reversing direction quickly

### Phase 4: Special States
1. Add crouch toggle
2. Implement crouched movement (slower, stealthier)
3. Add casting/spell system

## Technical Notes

### Root Motion
- All locomotion animations have root motion data extracted
- Root motion is stripped from the Hips bone during playback
- Character movement is physics-driven, not animation-driven
- This prevents feet sliding

### State Machine Thresholds
```cpp
WALK_THRESHOLD = 0.1f   // Below this = idle
RUN_THRESHOLD = 2.5f    // Above this = run
// Between thresholds = walk
```

### Movement Speeds
```cpp
moveSpeed = 1.44f      // Walk animation speed
sprintSpeed = 3.98f    // Run animation speed
```

### Adding a New Animation State

1. Load the animation in `SceneBuilder.cpp`:
```cpp
additionalAnimations.push_back(info.resourcePath + "/assets/characters/fbx/ss_attack1.fbx");
```

2. The state machine auto-detects animations by name keywords. For custom states, add to `AnimatedCharacter::setupAnimationStateMachine()`:
```cpp
stateMachine.addState("attack", clip, false);  // false = non-looping
```

3. Add transition logic in `AnimationStateMachine::update()`:
```cpp
if (isAttacking && currentState != "attack") {
    transitionTo("attack", 0.1f);
}
```

### Animation Blending
- Default blend duration: 0.2 seconds
- Fast transitions (jump, attack): 0.1 seconds
- Uses SLERP for rotation, LERP for position/scale
