# Movement System Implementation Plan

Detailed, step-by-step implementation plan derived from [MOVEMENT_SYSTEM_PLAN.md](MOVEMENT_SYSTEM_PLAN.md). Each step is a self-contained change that compiles and runs. Steps within a phase are ordered by dependency.

---

## Phase 1: Fix the Foundation (Motor + IK Bugs)

No new features. Fix bugs that undermine foot placement and locomotion.

### Step 1.1: Expose ground normal and full ground velocity from CharacterController

**Files:** `src/physics/CharacterController.h`, `src/physics/CharacterController.cpp`

**What to do:**
- Add `getGroundNormal()` method to `CharacterController`. Inside, call `character_->GetGroundNormal()` (Jolt API), convert from `JPH::Vec3` to `glm::vec3` via `toGLM()`, and return it. Return `{0,1,0}` when not on ground.
- Add `getGroundVelocity()` method returning the full 3-component `character_->GetGroundVelocity()` as `glm::vec3` (currently only the Y-component is used at `CharacterController.cpp:82`).

**Why:** Foot IK currently re-raycasts to get the ground normal that Jolt already knows. Exposing it avoids redundant work and gives IK solvers a physics-accurate normal. The full ground velocity is needed for moving platform support.

**Incremental state:** Everything compiles. New methods exist but aren't called yet. Rendering unchanged.

---

### Step 1.2: Project desired velocity onto ground plane when grounded

**Files:** `src/physics/CharacterController.cpp`

**What to do:**
- In `update()` (line ~73-75), when `onGround` is true, project the horizontal `desiredVelocity_` onto the ground plane before setting `newVelocity.SetX/SetZ`:
  ```
  groundNormal = character_->GetGroundNormal()
  projected = desiredVelocity - dot(desiredVelocity, groundNormal) * groundNormal
  newVelocity.SetX(projected.x)
  newVelocity.SetZ(projected.z)
  ```
- This ensures movement follows the slope surface rather than cutting horizontally through it.

**Why:** Without projection, near the 45-degree slope limit the character jitters between `OnGround` and airborne because horizontal velocity fights the slope angle.

**Incremental state:** Character moves smoothly on slopes without ground-state jitter. Slopes up to 45 degrees feel stable.

**How to test:** Walk the character up a steep slope (30-40 degrees). Should move smoothly without vibrating or popping between ground/air states.

---

### Step 1.3: Make jump impulse configurable

**Files:** `src/physics/CharacterController.h`, `src/physics/CharacterController.cpp`

**What to do:**
- Add a `float jumpImpulse_ = 5.0f` member and a `setJumpImpulse(float)` setter.
- Replace the hardcoded `5.0f` at `CharacterController.cpp:85` with `jumpImpulse_`.

**Why:** Hardcoded magic constant. Different gameplay scenarios need different jump heights.

**Incremental state:** Jump behavior unchanged (same default). Configurable via API.

---

### Step 1.4: Pass horizontal ground velocity through for moving platforms

**Files:** `src/physics/CharacterController.cpp`

**What to do:**
- In `update()` when `onGround`, add the ground velocity's horizontal components to the new velocity:
  ```
  newVelocity.SetX(projected.x + groundVelocity.GetX())
  newVelocity.SetZ(projected.z + groundVelocity.GetZ())
  ```
- Currently only `groundVelocity.GetY()` is used (line 82). The X/Z components are discarded.

**Why:** On a moving platform, the character should inherit the platform's horizontal velocity so it doesn't slide off.

**Incremental state:** Character stays on moving platforms. No visual change on static terrain.

---

### Step 1.5: Fix pelvis solve ordering

**Files:** `src/ik/IKSolver.cpp`

**What to do:**
- In `IKSystem::solve()` (line ~460), the current order is:
  1. Compute global transforms
  2. Calculate pelvis offset (reads `animationFootPosition`)
  3. Apply pelvis adjustment
  4. Solve foot placement IK (writes `animationFootPosition`)
- The problem: pelvis offset reads `animationFootPosition` before foot placement writes it, so it uses last frame's data.
- **New order:**
  1. Compute global transforms
  2. Run foot placement IK for **both** feet with `applyIK=false` mode — just compute `animationFootPosition` and ground queries without modifying the skeleton
  3. Calculate pelvis offset from the now-current foot positions
  4. Apply pelvis adjustment
  5. Recompute global transforms (once)
  6. Run foot placement IK for real (full solve)
  7. Recompute global transforms (once)

- Since `FootPlacementIKSolver::solve()` currently does both query + solve in one call, split it: add a `queryGroundForFoot()` static method that just populates `animationFootPosition`, `currentGroundHeight`, and `isGrounded` on the `FootPlacementIK` struct without modifying the skeleton. Then `solve()` uses those pre-populated values. This avoids duplicating the raycast.

**Why:** Bug #18 — pelvis adjustment reads stale foot positions from the previous frame, causing a one-frame lag that's visible as pelvis jitter on uneven terrain.

**Incremental state:** Pelvis tracks terrain correctly on the same frame feet are queried. Visible improvement on stairs and uneven ground.

**How to test:** Walk across boxes/steps of varying height. The pelvis should drop/rise simultaneously with the feet, not one frame late.

---

### Step 1.6: Fix left/right foot identification

**Files:** `src/ik/IKSolver.h`, `src/ik/IKSolver.cpp`

**What to do:**
- Add `bool isLeftFoot = false` field to `NamedFootPlacement` struct (at `IKSolver.h:706`).
- In `addFootPlacement()` (at `IKSolver.cpp:203`), add an `isLeftFoot` parameter and store it.
- Replace all string-matching lookups (`name.find("left")`, `name.find("Left")`, `name.find("L_")`) at lines 466-468 and 504-505 with checks on `nfp.isLeftFoot`.
- Update call sites to pass the correct flag.

**Why:** Bug #19 — substring matching on bone names is fragile. A foot named `"foot_l"` would be classified as the right foot because it doesn't match any of the patterns.

**Incremental state:** Left/right identification is explicit and correct for any naming convention.

---

### Step 1.7: Fix foot lock coordinate space and drift comparison

**Files:** `src/ik/FootPlacementIKSolver.cpp`

**What to do:**
- Add `glm::vec3 lockOriginWorldPosition` to `FootPlacementIK` struct — this stores the foot's world position at the moment the lock was engaged.
- At line 67 (first time locking), store both `lockedWorldPosition = worldFootPos` and `lockOriginWorldPosition = worldFootPos`.
- At line 73, change the drift comparison: instead of comparing `lockedWorldPosition` vs `worldFootPos` (current animation position, which naturally diverges as character walks), compare `lockedWorldPosition` vs `lockOriginWorldPosition`. The lock should only release if the **locked position itself** has drifted from where it was originally placed (e.g., due to accumulated floating-point error or extreme character movement).
- Alternative approach per the plan: compare `worldFootPos` against `lockOriginWorldPosition` instead of against the continuously-updating `lockedWorldPosition`. The intent is that `lockOriginWorldPosition` is the foot's position *at lock time*, and we release the lock if the character has moved so far that the lock would visually break.

**Why:** Bug #10 and #11 — the current 15cm max lock distance releases almost immediately during normal walking because it compares the locked position against the animation position, which naturally advances with the character. The lock should hold as long as the foot hasn't moved too far from where it was planted.

**Incremental state:** Foot locks hold reliably through a full stance phase. No premature release during normal walking.

**How to test:** Walk on flat ground and observe feet during stance. Feet should remain planted (no sliding) throughout the entire stance phase.

---

### Step 1.8: Fix extension ratio weight discontinuity

**Files:** `src/ik/FootPlacementIKSolver.cpp`

**What to do:**
- At line 151, `targetUnreachable` flips at 0.95 extension ratio.
- At line 154, the weight ramp formula is `1.0f - (ratio - 0.9f) * 10.0f`, which starts at 0.9.
- This creates a discontinuity: between 0.9 and 0.95 the ramp reduces weight but `targetUnreachable` is false, then at 0.95 it jumps.
- **Fix:** Align the guard check with the ramp start. Change line 151 from `> 0.95f` to `> 0.9f`. Now `targetUnreachable = true` when `ratio > 0.9`, and the ramp smoothly reduces weight from 1.0 (at 0.9) to 0.0 (at 1.0).

**Why:** Bug #12 — visible pop when IK weight jumps discontinuously at 0.95 extension.

**Incremental state:** Smooth IK weight falloff when leg approaches full extension. No visible pop.

---

### Step 1.9: Fix ground normal transform

**Files:** `src/ik/FootPlacementIKSolver.cpp`

**What to do:**
- At line 204, the ground normal is transformed using `glm::inverse(glm::mat3(characterTransform))`.
- For normals, the correct transform is `glm::transpose(glm::inverse(glm::mat3(characterTransform)))`.
- These are equivalent only when the matrix has no non-uniform scale, which isn't guaranteed.
- **Fix:** Change to `glm::transpose(glm::inverse(glm::mat3(characterTransform)))`.

**Why:** Bug #14 — incorrect normal transformation when character transform has non-uniform scale. While currently unlikely, this is a correctness fix that prevents subtle bugs.

**Incremental state:** Ground alignment correct under all transforms.

---

### Step 1.10: Reduce redundant computeGlobalTransforms calls

**Files:** `src/ik/IKSolver.cpp`

**What to do:**
- Currently `computeGlobalTransforms` is called after every individual solver (lines 478, 487, 495, 512, 528). That's 5-6 calls per frame.
- After step 1.5's reordering, the solve pipeline is:
  1. Compute globals (once)
  2. Query ground for both feet (no skeleton modification)
  3. Pelvis adjustment
  4. Compute globals (once)
  5. Foot IK left + right
  6. Compute globals (once)
  7. Straddle IK
  8. Two-bone chains (arms etc.)
  9. Compute globals (once, only if climbing or look-at follows)
  10. Climbing IK
  11. Look-at IK (last — no recompute needed after)
- Group solvers that modify bones, then recompute once after the group. This reduces calls from 5-6 to 3-4.
- Alternatively, add a `dirty` flag that tracks whether any solver modified bones, and only recompute when the next solver actually needs fresh global transforms.

**Why:** Bug #13 — `computeGlobalTransforms` iterates the entire skeleton hierarchy each call. Redundant calls are wasted CPU.

**Incremental state:** Same visual result, lower CPU cost per IK solve.

---

### Step 1.11: Fix FootPhaseTracker sampleFootHeight bind-pose reset no-op

**Files:** `src/animation/FootPhaseTracker.cpp`

**What to do:**
- At lines 118-120, the bind-pose reset loop body is empty:
  ```cpp
  for (size_t i = 0; i < tempSkeleton.joints.size(); ++i) {
      // Keep original local transform
  }
  ```
- This means foot height is sampled relative to whatever skeleton state existed at analysis time, not relative to bind pose.
- **Fix:** Actually reset each joint to bind pose before sampling:
  ```cpp
  for (size_t i = 0; i < tempSkeleton.joints.size(); ++i) {
      tempSkeleton.joints[i].localTransform = tempSkeleton.joints[i].inverseBindMatrix; // or bindPoseTransform
  }
  ```
- Check how the `Skeleton` struct stores bind pose. If it uses `inverseBindMatrix`, compute the bind-pose local transform from it. If there's a `bindPoseLocal` or similar field, use that directly.

**Why:** Bug #15 — foot height analysis depends on the skeleton's state when `analyzeAnimation` is called, which may not be bind pose. This produces incorrect contact/lift timings.

**Incremental state:** Foot phase detection is consistent regardless of when `analyzeAnimation` is called.

---

### Step 1.12: Unify phase state between FootPhaseTracker and FootPlacementIK

**Files:** `src/ik/IKSolver.h`, `src/ik/IKSolver.cpp`, `src/animation/FootPhaseTracker.h`

**What to do:**
- Currently both `FootPhaseData` (in tracker) and `FootPlacementIK` (in IK) store phase, progress, lock position, and lock blend independently. There's no enforcement they stay in sync.
- Make `FootPhaseTracker` the single source of truth:
  - After `FootPhaseTracker::update()`, copy phase data to the corresponding `FootPlacementIK` struct:
    ```cpp
    foot.currentPhase = tracker.getLeftFoot().phase;
    foot.phaseProgress = tracker.getLeftFoot().phaseProgress;
    foot.lockBlend = tracker.getLockBlend(isLeft);
    ```
  - Remove the redundant phase/progress fields from `FootPlacementIK` if they're only set externally (or mark them as driven by the tracker).
  - This should happen in the calling code (likely in `Application` or wherever the animation update drives IK).

**Why:** Bug #17 — duplicated state with no sync enforcement. Phase tracker and IK solver can disagree about which phase the foot is in.

**Incremental state:** Single source of truth for foot phase. No desync between tracker and IK.

---

### Step 1.13: Fix motion matching transition hysteresis

**Files:** `src/animation/MotionMatchingController.cpp`

**What to do:**
- At lines 227-242, four interacting constants control transitions:
  - `0.8f` cost ratio for different-clip transition
  - `0.5f` seconds minimum time before different-clip transition
  - `0.2f` seconds minimum time difference for same-clip jump
  - `0.5f` cost ratio for same-clip time jump
  - `1.0f` seconds force-transition timeout
- Replace with a coherent policy struct:
  ```cpp
  struct TransitionPolicy {
      float minDwellTime = 0.3f;          // Minimum time in current clip before allowing transition
      float costImprovementRatio = 0.75f; // New match must be this fraction of current cost
      float forceTransitionTime = 1.0f;   // Force search for new clip after this long
      float sameClipMinTimeDiff = 0.2f;   // Minimum time jump for same-clip transitions
      float sameClipCostRatio = 0.5f;     // Stricter ratio for same-clip jumps
  };
  ```
- Add this struct to `ControllerConfig`. Replace the inline magic numbers with reads from the policy.
- Default values should preserve current behavior.

**Why:** Bug #9 — the current constants interact in non-obvious ways and are hard to tune. A named policy struct makes the behavior explicit and configurable.

**Incremental state:** Same default behavior. Constants are now configurable and documented.

**How to test:** Walk around with the debug overlay showing motion matching stats. Transitions should occur at similar frequency to before. Tune `minDwellTime` up to reduce jittery transitions.

---

### Phase 1 verification

Walk across uneven terrain, step on boxes, walk up slopes. Verify:
- Feet plant without jitter
- No visible pops when foot lock engages/releases
- Pelvis tracks correctly (no one-frame lag)
- Smooth IK falloff at full leg extension
- Character moves smoothly on steep slopes

---

## Phase 2: Eliminate Foot Sliding (Stride Matching + Root Motion)

### Step 2.1: Compute stride length per animation clip

**Files:** `src/animation/MotionDatabase.h`, `src/animation/MotionDatabase.cpp`

**What to do:**
- Add `float strideLength = 0.0f` to `DatabaseClip`.
- During `build()`, for each clip: track the root bone's X/Z displacement over one full cycle (or the full clip for non-looping). Store as `strideLength`.
- For Mixamo in-place clips that have a `locomotionSpeed` override, compute stride length as `locomotionSpeed * duration`.

**Why:** Stride length is needed to compute the playback speed ratio for foot-slide elimination.

**Incremental state:** Stride lengths computed and stored. Not yet used. Visible in debug logging.

---

### Step 2.2: Add playback speed scaling to MotionMatchingController

**Files:** `src/animation/MotionMatchingController.h`, `src/animation/MotionMatchingController.cpp`

**What to do:**
- In `advancePlayback()` (line ~301), time currently advances at `deltaTime * 1.0`:
  ```cpp
  playback_.time += deltaTime;
  ```
- Compute a speed ratio:
  ```cpp
  float clipSpeed = clip.locomotionSpeed > 0 ? clip.locomotionSpeed : (clip.strideLength / clip.duration);
  float actualSpeed = glm::length(glm::vec2(currentVelocity.x, currentVelocity.z));
  float speedRatio = clipSpeed > 0.01f ? actualSpeed / clipSpeed : 1.0f;
  speedRatio = glm::clamp(speedRatio, 0.5f, 2.0f);  // Clamp to avoid extreme distortion
  playback_.time += deltaTime * speedRatio;
  ```
- Store `currentVelocity` from the trajectory predictor (already available via the `update()` parameters).
- Add `float playbackSpeedScale` to the playback state for debug visualization.

**Why:** Bug #6 — without speed scaling, when actual movement speed diverges from the matched clip's root motion speed, feet slide on the ground.

**Incremental state:** Feet visually match movement speed. Sliding reduced at all speeds.

**How to test:** Walk/run at various speeds. Observe feet during stance phase — they should appear planted, not sliding forward or backward relative to the ground.

---

### Step 2.3: Fix root Y-rotation stripping — feed into character facing

**Files:** `src/animation/MotionMatchingController.cpp`, `src/animation/MotionMatchingController.h`

**What to do:**
- Currently at lines 347-363, all Y-rotation is stripped from the root and discarded.
- Instead of discarding, **extract and expose** the Y-rotation delta:
  - Add `float extractedRootYawDelta_ = 0.0f` member.
  - In `updatePose()`, compute the yaw as currently done, but store it:
    ```cpp
    extractedRootYawDelta_ = yaw;  // Per-frame yaw from animation
    ```
  - Still strip it from the pose (to prevent double-rotation for walk/run).
  - Add `float getExtractedRootYawDelta() const` getter.
- The calling code (likely in `Application`) should feed this yaw delta into the character controller's facing rotation. For walk/run clips the delta is near-zero so behavior is unchanged. For turn-in-place clips (Phase 3), the delta drives the turn.

**Why:** Bug #5 — stripping all root Y-rotation is correct for walk/run but destroys turn-in-place and combat animations. By extracting rather than discarding, we preserve the information for when it's needed.

**Incremental state:** Same visual behavior for walk/run (delta is near-zero). API ready for turn animations in Phase 3.

---

### Step 2.4: Implement per-bone velocity tracking for inertial blending

**Files:** `src/animation/AnimationBlend.h`, `src/animation/AnimationBlend.cpp` (or wherever `InertialBlender` lives)

**What to do:**
- In `MotionMatchingController`, track `previousPose_` and `prevPrevPose_` (pose from two frames ago).
- Before calling `startSkeletalBlend()`, compute per-bone velocity via finite difference:
  ```cpp
  for (size_t i = 0; i < pose.size(); ++i) {
      boneVelocities[i].translationVel = (previousPose_[i].translation - prevPrevPose_[i].translation) / prevDeltaTime;
      boneVelocities[i].rotationVel = computeAngularVelocity(prevPrevPose_[i].rotation, previousPose_[i].rotation, prevDeltaTime);
  }
  ```
- Extend `InertialBlender::startSkeletalBlend()` to accept bone velocities (currently noted at `MotionMatchingController.cpp:286` as missing).
- Use velocities in the spring-damper system for proper inertialization (Bovet & Clavet GDC 2016 approach).

**Why:** Bug #7 — without per-bone velocity, the inertial blender is a fancy crossfade. True inertialization needs velocity to produce the characteristic overshoot-and-settle.

**Incremental state:** Motion matching transitions look noticeably smoother with natural momentum. Most visible at walk-to-run and direction changes.

**How to test:** Trigger rapid transitions (change direction while running). The blend should show a brief overshoot before settling, not a stiff linear interpolation.

---

### Phase 2 verification

Walk/run at every speed from 0 to max. Observe feet in stance phase — zero visible sliding. Motion matching transitions produce smooth overshoot-and-settle blends rather than linear crossfades.

---

## Phase 3: Full Locomotion (Turn, Strafe, Start/Stop)

**Prerequisite:** New animation assets are needed for this phase. The implementation work assumes clips are available.

### Step 3.1: Add turn clips to motion matching database

**Files:** Asset pipeline + `Application` or wherever clips are registered

**What to do:**
- Source turn-left-90, turn-right-90, and turn-180 animation clips (Mixamo or similar).
- Add them to the motion matching database tagged as `"turn"`:
  ```cpp
  controller.addClip(&turnLeft90, "turn_left_90", false, {"turn"}, 0.0f, 0.0f);
  controller.addClip(&turnRight90, "turn_right_90", false, {"turn"}, 0.0f, 0.0f);
  controller.addClip(&turn180, "turn_180", false, {"turn"}, 0.0f, 0.0f);
  ```
- These are non-looping clips with significant root Y-rotation. Step 2.3's extracted yaw delta feeds the character controller's facing, so the turn plays as animation while the character actually rotates.

**Why:** Turn-in-place is the most visually jarring missing feature. Without turn clips, the character snaps rotation instantly.

**Incremental state:** Character plays turn animation when changing facing direction while stationary. The motion matcher's trajectory prediction naturally selects these clips when the query shows a large facing delta with low positional movement.

---

### Step 3.2: Integrate extracted root yaw into character facing

**Files:** The calling code that drives both motion matching and character controller (likely `Application.cpp` or a player controller class)

**What to do:**
- After `motionMatchingController.update()`, read the extracted yaw delta:
  ```cpp
  float yawDelta = motionMatchingController.getExtractedRootYawDelta();
  if (std::abs(yawDelta) > 0.001f) {
      playerFacing = glm::rotate(playerFacing, yawDelta, glm::vec3(0, 1, 0));
  }
  ```
- During turn animations, foot locking should lock the pivot foot (the planted foot). The phase tracker already handles this — during stance the foot is locked, during swing it follows animation.

**Why:** Completes the root-motion-driven turn pipeline from step 2.3.

**Incremental state:** Turn-in-place animations drive actual facing rotation. Pivot foot stays planted.

**How to test:** Stand still and rotate the camera/input 90 degrees. Character should play a turn animation, pivoting on the planted foot.

---

### Step 3.3: Add strafe and backward locomotion clips

**Files:** Asset pipeline + clip registration

**What to do:**
- Add strafe-walk-left, strafe-walk-right, walk-backward clips tagged as `"strafe"`.
- Optionally add strafe-run clips for run-speed strafing.
- The existing `setStrafeMode()` and strafe tag filtering in `performSearch()` (lines 168-199) already handles selecting strafe clips when movement is predominantly sideways. No motion matching code changes needed — the system already detects sideways movement in local space and adds the `"strafe"` tag requirement.

**Why:** Strafing is needed for directional locomotion (combat, aiming).

**Incremental state:** When strafe mode is active and player moves sideways, character plays strafe animations instead of turning.

---

### Step 3.4: Add start/stop transition clips

**Files:** Asset pipeline + clip registration

**What to do:**
- Add walk-start, walk-stop, run-start, run-stop clips tagged as `"transition"`.
- These are non-looping clips. Motion matching naturally selects them when:
  - Start: trajectory shows acceleration from idle (low current speed, high future speed)
  - Stop: trajectory shows deceleration to idle (high current speed, low future speed)
- No special state machine logic needed. The motion matching cost function inherently finds the best pose match.

**Why:** Without start/stop clips, the character pops from idle to walk/run instantly.

**Incremental state:** Visible weight transfer when starting and stopping. Natural deceleration animations.

---

### Step 3.5: Fix trajectory rotation for slopes (2D → 3D)

**Files:** `src/animation/MotionMatchingController.cpp`

**What to do:**
- In `performSearch()` (lines 142-159), the trajectory rotation only rotates X/Z coordinates around Y, leaving Y unchanged:
  ```cpp
  s.position.x = pos.x * cosA + pos.z * sinA;
  s.position.z = -pos.x * sinA + pos.z * cosA;
  // s.position.y unchanged
  ```
- If the trajectory predictor produces non-zero Y positions (on slopes), the matching breaks because the database trajectories are flat.
- **Fix:** Project trajectory samples onto the ground plane before rotation, or include Y in the trajectory cost with a lower weight. The simplest fix: zero out Y before matching:
  ```cpp
  s.position.y = 0.0f;  // Matching is 2D; height handled by IK
  ```
- Also update velocity Y to zero for matching purposes (keep the original for other uses).
- Long term: include vertical velocity in trajectory samples for uphill/downhill awareness, with a separate weight.

**Why:** Bug #8 — on slopes, trajectory positions have non-zero Y that confuses the 2D matcher.

**Incremental state:** Motion matching works correctly on slopes. Correct clip selection going uphill/downhill.

**How to test:** Walk up and down hills. Motion matching should select appropriate clips without jitter or incorrect selections.

---

### Step 3.6: Upper/lower body split for strafe aiming

**Files:** Animation layer system (existing `AnimationLayer` and `BoneMask`)

**What to do:**
- When in strafe mode, the upper body should face the aim/camera direction while the lower body follows movement direction.
- Use the existing `BoneMask` system to create a split:
  - Lower body mask: pelvis, legs, feet (driven by motion matching)
  - Upper body mask: spine, arms, head (driven by aim direction)
- Apply the aim rotation as an additive layer on top of the motion-matched pose, masked to upper body only.

**Why:** In strafing combat, the character needs to face the target while legs follow movement.

**Incremental state:** When strafing, upper body faces camera/aim direction while legs animate sideways movement.

---

### Phase 3 verification

- Stand still, rotate 90/180 degrees — natural turn animation, pivot foot stays planted
- Strafe left/right at walk and run speed — feet match movement direction
- Start walking from idle, stop — visible weight transfer animation
- Upper body faces aim while lower body strafes

---

## Phase 4: Terrain Detail (Multi-Point Ground, Toe IK, Foot Roll, Slopes)

### Step 4.1: Multi-point ground queries (heel + ball + toe)

**Files:** `src/ik/FootPlacementIKSolver.cpp`, `src/ik/IKSolver.h`

**What to do:**
- Add `int32_t heelBoneIndex = -1` and `int32_t ballBoneIndex = -1` to `FootPlacementIK` (the `toeBoneIndex` already exists).
- Replace the single ankle raycast with three raycasts: heel, ball-of-foot, and toe.
- Derive probe positions from skeleton geometry (foot bone → toe bone chain).
- Fit a plane to the contacts using least-squares:
  ```
  normal = normalize(cross(ball - heel, toe - heel))
  ```
- Use this plane for foot orientation instead of the single ground normal.
- Handle partial contacts: if only heel hits (toe hanging off edge), use a weighted combination.

**Why:** A single raycast can't detect stairs, step edges, or rocks under the foot arch. Multi-point queries give the foot a surface to conform to.

**Incremental state:** Feet orient to match the actual surface under them, including on stair edges.

---

### Step 4.2: Toe IK

**Files:** New solver or extension of `FootPlacementIKSolver`

**What to do:**
- After foot placement, raycast from toe position downward.
- Calculate the angle difference between the foot plane and the toe ground contact.
- Rotate the toe bone to match the ground angle.
- Clamp to natural limits: ~45 degrees dorsiflexion (toes up), ~60 degrees plantarflexion (toes down).
- Blend with foot phase:
  - Push-off phase: allow full toe bend (natural toe-off)
  - Swing phase: return toe to animation pose
  - Stance phase: conform to ground

**Why:** On stairs and slopes, rigid feet look robotic. Toe flexion makes contacts look natural.

**Incremental state:** Toes bend on step edges and slopes. Push-off shows natural toe flex.

---

### Step 4.3: Foot roll phases

**Files:** `src/animation/FootPhaseTracker.h`, extension of foot IK

**What to do:**
- Add virtual markers for heel, ball, and toe (derived from skeleton in bind pose).
- Implement roll sub-phases driven by `FootPhaseTracker`:
  - **Heel strike:** First contact is heel. Foot rotates around heel until flat.
  - **Foot flat:** Full foot on ground. Standard stance behavior.
  - **Heel off:** Heel lifts, foot pivots around ball.
  - **Toe off:** Final push, foot pivots around toe tip.
- Apply incremental rotations to the foot bone at each sub-phase.
- Sub-phase transitions are driven by the existing phase progress values.

**Why:** Natural walking has distinct roll phases. Without them, the foot appears to stamp flat at contact.

**Incremental state:** Visible heel-strike and toe-off during walking. More natural gait appearance.

---

### Step 4.4: Slope compensation

**Files:** `src/ik/FootPlacementIKSolver.cpp`, integration with `StraddleIKSolver`

**What to do:**
- Sample ground normal at character center position.
- **Forward/backward pelvis shift:** On uphill slopes, shift pelvis slightly forward. On downhill, shift back. Currently pelvis only adjusts Y (`IKSolver.cpp:309`). Add X/Z offset proportional to ground slope angle.
- **Body lean:** Apply lean rotation proportional to slope grade. Integrate with existing straddle IK which already handles lateral tilt.
- **Speed adjustment:** Slightly reduce playback speed for uphill (shorter stride feel), increase for downhill (longer stride). Apply as a multiplier to the speed ratio from step 2.2.

**Why:** Bug #20 — pelvis currently only adjusts Y, which looks unnatural on slopes where the body should lean into the hill.

**Incremental state:** Character leans into slopes, pelvis shifts forward uphill, stride adjusts for grade.

**How to test:** Walk up/down a 30-degree slope. Body should lean forward going uphill, slightly back going downhill. Stride length should appear slightly shorter uphill.

---

### Phase 4 verification

- Walk up/down stairs — toes bend on step edges
- Walk across rocky terrain — heel and toe both contact, foot conforms to surface
- Walk up a 30-degree slope — body lean and stride adjustment visible
- Foot roll visible during normal walking (heel strike → flat → toe off)

---

## Dependency Graph

```
Phase 1 (all steps independent of each other, except 1.5 before 1.10):
  1.1 → 1.2 → 1.4  (ground normal → slope projection → platform velocity)
  1.3                (independent)
  1.5 → 1.10         (pelvis reorder → reduce recompute)
  1.6                (independent)
  1.7                (independent)
  1.8                (independent)
  1.9                (independent)
  1.11               (independent)
  1.12               (independent)
  1.13               (independent)

Phase 2 (sequential):
  2.1 → 2.2          (stride length → speed scaling)
  2.3                (independent of 2.1/2.2)
  2.4                (independent)

Phase 3 (mostly depends on Phase 2):
  3.1 → 3.2          (turn clips → yaw integration)
  3.3                (needs strafe clips, code already exists)
  3.4                (needs transition clips, code already exists)
  3.5                (independent)
  3.6                (needs 3.3)

Phase 4 (depends on Phase 1 foundations):
  4.1 → 4.2          (multi-point → toe IK)
  4.1 → 4.3          (multi-point → foot roll)
  4.4                (independent)
```

---

## Animation Assets Checklist

### Have
- [x] Idle
- [x] Walk
- [x] Run
- [x] Jump

### Needed for Phase 3
- [ ] Turn Left 90
- [ ] Turn Right 90
- [ ] Turn 180
- [ ] Strafe Left Walk
- [ ] Strafe Right Walk
- [ ] Walk Backward
- [ ] Walk Start
- [ ] Walk Stop
- [ ] Run Start
- [ ] Run Stop
- [ ] Strafe Left Run (optional)
- [ ] Strafe Right Run (optional)

### Phase 4
No new assets — driven by IK and procedural adjustment.
