# Comprehensive Movement System Plan

## Critical Analysis of Current Foot/IK Issues

### Problem 1: No Foot Phase Awareness
**Issue**: The IK system doesn't know when feet are supposed to be planted vs. in swing phase.
- Current IK applies ground adaptation continuously, fighting the animation during swing phase
- Results in feet that "hover" or "drag" instead of clean lifts
- Foot locking only activates during idle (movementSpeed < 0.1 m/s), not during stance phase of walk/run

**Evidence**: `FootPlacementIKSolver.cpp:77` applies height offset regardless of foot phase

### Problem 2: Single-Point Ground Query
**Issue**: Each foot uses a single raycast at the ankle position.
- Doesn't account for heel-toe foot geometry
- Foot can appear to float over small terrain features or sink into convex surfaces
- No consideration of foot length or width

**Evidence**: `FootPlacementIKSolver.cpp:46-47` - single rayOrigin with raycast down

### Problem 3: Fixed Ankle Height
**Issue**: Hardcoded 8cm ankle offset (`FootPlacementIKSolver.cpp:60`)
- Won't work correctly for different character scales or foot proportions
- Should be derived from actual skeleton geometry

### Problem 4: No Toe IK
**Issue**: Toe bone is passed to foot placement but never actively solved
- Toe stays rigid relative to foot
- No toe bend on slopes or stairs
- No toe-off during push-off phase

### Problem 5: Animation Speed Clamping Causes Sliding
**Issue**: Speed scale clamped to 0.5x-2.0x (`AnimationStateMachine.cpp:124`)
- At very slow speeds (0.05-0.1 m/s), animation plays at minimum 0.5x, feet slide
- At high speeds (>2x run speed), animation plays at maximum 2.0x, feet slide
- No stride length adjustment

### Problem 6: No Phase-Aligned Transitions
**Issue**: State transitions reset animation time to 0 (`AnimationStateMachine.cpp:66`)
- Walk→run transition: feet jump to different positions
- No synchronization of foot contact timing between animations
- Blend space time sync exists but isn't foot-phase aware

### Problem 7: Limited Animation States
**Issue**: Only 4 states: idle, walk, run, jump
- No turn-in-place (character slides when rotating while stationary)
- No start/stop transitions (sudden velocity changes look robotic)
- No directional locomotion (strafing, backwards)
- No slope-specific animations

### Problem 8: No Foot Roll
**Issue**: Foot treated as rigid body
- No heel-strike → flat → toe-off progression
- Looks mechanical on flat ground, worse on slopes

---

## Improvement Plan

### Milestone 1: Foot Phase Detection
**Goal**: Know when each foot should be planted vs. swinging

**Tasks**:
1. Add animation events for foot contacts
   - `left_foot_down`, `left_foot_up`, `right_foot_down`, `right_foot_up` events
   - Store in AnimationClip, fire during playback
   - For existing Mixamo anims, auto-detect from foot bone Y position curves

2. Create `FootPhaseTracker` class
   - Track phase per foot: `swing`, `contact`, `stance`, `push_off`
   - Expose normalized phase (0-1) within each state
   - Predict contact position during swing phase

3. Modify `FootPlacementIKSolver` to respect phase
   - During swing: minimal IK, let animation play
   - During contact: blend IK in, lock position
   - During stance: maintain locked position
   - During push-off: allow toe pivot

**Testing**: Visual debug display of foot phases, verify feet lift cleanly during swing

---

### Milestone 2: Multi-Point Ground Queries
**Goal**: Better ground sensing for heel and toe

**Tasks**:
1. Add heel/toe raycast positions
   - Derive from foot bone length (use toe bone position relative to foot)
   - Query ground at heel, ball, and toe

2. Compute optimal foot placement
   - Find plane that fits ground contacts
   - Calculate required foot pitch and roll
   - Handle partial contacts (heel on step, toe hanging off)

3. Add foot collision shape
   - Simple capsule or box for foot
   - Use for terrain penetration avoidance

**Testing**: Walk across varying terrain, verify heel and toe both contact appropriately

---

### Milestone 3: Dynamic Ankle Height
**Goal**: Auto-derive foot geometry from skeleton

**Tasks**:
1. Calculate ankle height at setup time
   - Measure distance from foot bone to toe bone projected onto ground plane
   - Account for character scale

2. Calculate foot dimensions
   - Length: foot bone to toe bone
   - Width: estimate from bone scale or use standard humanoid ratio

3. Store in `FootPlacementIK` struct
   - Replace hardcoded 0.08f with derived values

**Testing**: Load characters at different scales, verify feet plant correctly

---

### Milestone 4: Toe IK
**Goal**: Toes bend naturally on slopes

**Tasks**:
1. Add toe bone solving
   - After foot placement, raycast from toe position
   - Calculate toe rotation to match ground angle
   - Clamp to natural toe bend limits (~45° dorsiflexion, ~60° plantarflexion)

2. Toe blending during walk cycle
   - Push-off: allow toe bend as foot leaves ground
   - Contact: toe follows ground slope

**Testing**: Walk up/down slopes, verify toes bend appropriately

---

### Milestone 5: Stride Length Matching
**Goal**: Eliminate foot sliding at all speeds

**Tasks**:
1. Calculate animation stride length
   - Track foot X/Z displacement over one cycle
   - Store per animation in `AnimationClip`

2. Create speed-to-animation mapping
   - Given desired velocity, calculate required animation speed
   - If beyond 0.5x-2.0x, transition to different animation (walk↔run) or adjust stride

3. Implement foot plant prediction
   - During swing phase, predict where foot will land
   - Adjust animation timing micro-corrections to hit predicted position

**Testing**: Move at various speeds, verify no visible foot sliding

---

### Milestone 6: Phase-Aligned Transitions
**Goal**: Seamless animation transitions without foot popping

**Tasks**:
1. Detect foot phase at transition
   - When transition starts, record current phase of each foot

2. Find matching phase in target animation
   - Search target animation for closest matching foot positions
   - Start target from matched phase, not time 0

3. Foot position matching
   - During blend, interpolate foot IK targets separately from body
   - Keeps feet planted through transitions

**Testing**: Walk→run at various points, verify feet stay planted

---

### Milestone 7: Turn-in-Place Animation
**Goal**: Natural rotation when stationary

**Tasks**:
1. Download turn animations from Mixamo
   - "Turn Left 90", "Turn Right 90", "Turn Left 180", "Turn Right 180"

2. Add turn state to AnimationStateMachine
   - Trigger on rotation input while idle
   - Blend into turn, then back to idle

3. Foot IK during turn
   - Track planted foot as pivot
   - Moving foot follows animation arc

**Testing**: Rotate in place, verify natural turning motion

---

### Milestone 8: Start/Stop Transitions
**Goal**: Smooth acceleration/deceleration

**Tasks**:
1. Download transition animations from Mixamo
   - "Start Walking", "Stop Walking", "Start Running", "Stop Running"

2. Create velocity-triggered transitions
   - Idle→walk: play start animation
   - Walk→idle: play stop animation

3. Blend with locomotion
   - Start transitions blend from idle pose to moving
   - Stop transitions blend from moving to idle

**Testing**: Start/stop movement, verify natural weight transfer

---

### Milestone 9: Directional Locomotion
**Goal**: Strafing and backwards movement

**Tasks**:
1. Download directional animations from Mixamo
   - "Strafe Left", "Strafe Right", "Walk Backward", "Run Backward"

2. Implement 2D blend space
   - X axis: left/right velocity
   - Y axis: forward/backward velocity
   - Center: idle, edges: directional movements

3. Upper/lower body split
   - Lower body follows movement direction
   - Upper body faces aim/camera direction

**Testing**: Move in all directions, verify feet match movement

---

### Milestone 10: Foot Roll
**Goal**: Natural heel-toe progression

**Tasks**:
1. Add heel/ball/toe markers
   - Virtual bones or calculated positions from foot/toe bones

2. Implement roll phases
   - Heel strike: only heel contacts ground
   - Foot flat: full foot contact
   - Heel off: pivot on ball of foot
   - Toe off: final push, toe pivot

3. Drive roll from animation phase
   - Map animation time to roll phase
   - Apply incremental rotations to foot bone

**Testing**: Slow-motion walk, verify natural heel-toe roll

---

### Milestone 11: Slope Compensation
**Goal**: Proper leaning and stride adjustment on slopes

**Tasks**:
1. Detect surface angle
   - Sample ground normal at character center
   - Calculate slope gradient in movement direction

2. Adjust stride length
   - Uphill: shorter strides, more effort (slower animation or different clip)
   - Downhill: longer strides, controlled braking

3. Body lean
   - Lean into hill proportional to grade
   - Integrate with straddle IK

**Testing**: Walk up/down various slopes, verify natural posture

---

## Animation Assets to Download from Mixamo

### Core Locomotion (Priority 1)
- [x] Idle (have)
- [x] Walk (have)
- [x] Run (have)
- [x] Jump (have)

### Transitions (Priority 2)
- [ ] Walk Start
- [ ] Walk Stop
- [ ] Run Start
- [ ] Run Stop
- [ ] Walk to Run
- [ ] Run to Walk

### Turns (Priority 2)
- [ ] Turn Left 90
- [ ] Turn Right 90
- [ ] Turn Left 180
- [ ] Turn Right 180
- [ ] Turn Left While Walking
- [ ] Turn Right While Walking

### Directional (Priority 3)
- [ ] Strafe Left Walk
- [ ] Strafe Right Walk
- [ ] Walk Backward
- [ ] Strafe Left Run
- [ ] Strafe Right Run
- [ ] Run Backward

### Slope/Stairs (Priority 4)
- [ ] Walk Upstairs
- [ ] Walk Downstairs
- [ ] Walk Up Slope
- [ ] Walk Down Slope

---

## Success Metrics

1. **No visible foot sliding** at any movement speed
2. **Clean foot lifts** during swing phase (no dragging)
3. **Heel-toe contact** visible in slow motion
4. **Smooth transitions** between all states
5. **Natural turning** without sliding rotation
6. **Proper ground conformance** on slopes and steps
7. **Phase-aligned blending** during speed changes

---

## Implementation Order

1. Milestone 1 (Foot Phase) - Foundational, enables everything else
2. Milestone 5 (Stride Matching) - Most noticeable improvement
3. Milestone 7 (Turn-in-Place) - Common complaint with current system
4. Milestone 2 (Multi-Point Ground) - Better terrain interaction
5. Milestone 6 (Phase-Aligned Transitions) - Polish
6. Milestone 8 (Start/Stop) - Polish
7. Milestone 4 (Toe IK) - Detail
8. Milestone 10 (Foot Roll) - Detail
9. Milestone 3 (Dynamic Ankle) - Robustness
10. Milestone 9 (Directional) - Feature expansion
11. Milestone 11 (Slope) - Feature expansion
