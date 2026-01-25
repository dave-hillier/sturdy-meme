# NPC System Architecture

This document describes the NPC system architecture, inspired by Assassin's Creed Origins' "Meta AI" system as presented in various GDC talks over the years.

## Overview

The NPC system is designed to handle large numbers of NPCs efficiently while maintaining emergent, believable behavior. It uses a tiered Level-of-Detail (LOD) approach where NPCs exist in different representation states based on their distance to the player.

## Architecture Diagram

```
                    +------------------+
                    |   NPCManager     |
                    +------------------+
                           |
          +----------------+----------------+
          |                |                |
    +----------+     +----------+     +----------+
    | Virtual  |     |   Bulk   |     |   Real   |
    | NPCs     |     |   NPCs   |     |   NPCs   |
    +----------+     +----------+     +----------+
    | Data only|     | Simple   |     | Full AI  |
    | No render|     | movement |     | Physics  |
    | 5-15s upd|     | 1s update|     | Per-frame|
    +----------+     +----------+     +----------+
          |                |                |
          +----------------+----------------+
                           |
                    +------+------+
                    |             |
              +---------+   +---------+
              |  Needs  |   |Schedule |
              | System  |   | System  |
              +---------+   +---------+
                    |
              +---------+
              |Systemic |
              | Events  |
              +---------+
```

## LOD States (Virtual/Bulk/Real)

Inspired by AC Origins' Meta AI, NPCs exist in one of three representation states:

### Virtual State
- **Distance**: > 100m from player
- **Update Rate**: Every 5-15 seconds
- **Features**:
  - Data only - no visual representation
  - Needs still accumulate over time
  - Schedule is followed (teleport to locations)
  - No physics or collision
  - No behavior tree execution
- **Purpose**: Track thousands of NPCs without performance impact

### Bulk State
- **Distance**: 40-100m from player
- **Update Rate**: Every 1 second
- **Features**:
  - Rendered with simplified mesh (currently full mesh, future: LOD mesh)
  - Simple linear movement toward goals
  - Needs and schedule updates
  - No physics queries
  - No perception or awareness
- **Purpose**: Provide visual crowds without full AI cost

### Real State
- **Distance**: < 40m from player
- **Update Rate**: Every frame
- **Features**:
  - Full mesh with all animations
  - Complete behavior tree AI
  - Physics-based movement and collision
  - Perception system (sight, hearing)
  - Full hostility reactions
  - Can participate in systemic events
- **Purpose**: Fully interactive NPCs near the player

### LOD Transition Hysteresis

To prevent NPCs from flickering between states:

| Transition | Threshold |
|------------|-----------|
| Virtual → Bulk | < 80m |
| Bulk → Virtual | > 100m |
| Bulk → Real | < 40m |
| Real → Bulk | > 50m |

### Budget Constraints

The system enforces maximum counts per LOD level:
- **Max Real NPCs**: 40 (configurable)
- **Max Bulk NPCs**: 120 (configurable)
- **Virtual NPCs**: Unlimited

When budgets are exceeded, NPCs are demoted based on priority (distance + hostility + urgency).

## Needs-Based Behavior

NPCs have six needs that drive emergent behavior:

| Need | Growth Rate | Decay | Drives |
|------|-------------|-------|--------|
| Hunger | +0.15/hour | Eating | Seek food, go home/tavern |
| Tiredness | +0.06/hour | Resting | Go home, sleep |
| Fear | - | -2.0/hour (when safe) | Flee, hide |
| Aggression | - | -1.0/hour | Attack, confront |
| Social | +0.10/hour | Socializing | Seek conversation, gather |
| Work | +0.20/hour | Working | Go to workplace |

### Need Priority

When needs exceed 70% (configurable threshold), they influence behavior:

1. **Fear** (highest priority - survival)
2. **Aggression**
3. **Tiredness** (if > 90% - exhaustion)
4. **Hunger**
5. **Tiredness** (normal)
6. **Work**
7. **Social** (lowest priority)

Urgent needs can override scheduled activities.

## Schedule System

NPCs follow daily schedules based on their archetype:

### Day Periods

| Period | Hours | Activities |
|--------|-------|------------|
| Dawn | 05:00-07:00 | Wake up, prepare |
| Morning | 07:00-12:00 | Work |
| Midday | 12:00-14:00 | Lunch |
| Afternoon | 14:00-18:00 | Work |
| Evening | 18:00-21:00 | Leisure, social |
| Night | 21:00-05:00 | Sleep |

### NPC Archetypes

| Archetype | Dawn | Morning | Midday | Afternoon | Evening | Night |
|-----------|------|---------|--------|-----------|---------|-------|
| Villager | Eat | Work | Eat | Work | Social | Sleep |
| Guard | Sleep | Patrol | Patrol | Patrol | Patrol | Sleep |
| Merchant | Sleep | Work | Eat | Work | Social | Sleep |
| Farmer | Eat | Work | Work | Work | Social | Sleep |
| Noble | Sleep | Wander | Eat | Wander | Eat | Sleep |
| Beggar | Sleep | Wander | Wander | Wander | Wander | Sleep |
| Child | Sleep | Wander | Eat | Wander | Wander | Sleep |

### Key Locations

Each NPC has three key locations:
- **homeLocation**: Where they sleep and eat
- **workLocation**: Where they perform their job
- **socialLocation**: Where they socialize (tavern, square)

## Systemic Events

NPC-to-NPC interactions that occur independently of the player:

| Event Type | Trigger | Duration | Player Can Intervene |
|------------|---------|----------|---------------------|
| Fistfight | Hostile + Aggression > 60% | 15s | Yes |
| Conversation | Social > 70% + Idle | 30s | No |
| Pickpocket | (Not implemented) | - | - |
| Mugging | (Not implemented) | - | - |
| Transaction | (Not implemented) | - | - |

### Event Constraints
- Max 5 active events at once
- NPCs can only participate in one event at a time
- Events only spawn between Real NPCs
- Check interval: every 5 seconds

## Hostility System

NPCs have hostility levels that affect their behavior:

| Level | Tint Color | Behavior |
|-------|------------|----------|
| Friendly | Green | Helpful, won't attack |
| Neutral | Yellow | Ignores player |
| Hostile | Red | Attacks on sight |
| Afraid | Blue | Flees from player |

### Hostility Triggers
- Player attack → Hostile
- Player in restricted area → Hostile (guards)
- Damage taken → Hostile or Afraid
- Time decay → Returns to base hostility

## Time System

The game has an accelerated time scale:
- **Default**: 60 game minutes per real second
- **Result**: 1 game hour = 1 real minute
- **Full day-night cycle**: 24 real minutes

Configure with:
```cpp
npcManager.setTimeScale(60.0f);  // Game minutes per real second
npcManager.setTimeOfDay(12.0f);  // Set to noon
```

## Key Files

| File | Purpose |
|------|---------|
| `src/npc/NPCLODSystem.h` | LOD states, needs, schedules, events |
| `src/npc/NPC.h` | Core NPC data structure |
| `src/npc/NPCManager.h/cpp` | NPC lifecycle and update orchestration |
| `src/npc/NPCRenderData.h` | Thread-safe render data structures |
| `src/npc/HostilityState.h` | Hostility levels and triggers |
| `src/npc/NPCPerception.h` | Sight and hearing perception |
| `src/npc/BehaviorTree.h` | Behavior tree implementation |
| `src/npc/NPCBehaviorTrees.h` | Pre-built behavior trees |

## Threading Architecture

The NPC system is designed to support multi-threaded execution where simulation
runs independently of rendering. This enables:

- Future server-side NPC processing
- Background NPC updates while rendering proceeds
- Parallel simulation across multiple cores

### Data Flow

```
+------------------+       +-------------------+       +------------------+
|  Simulation      |       |   NPCRenderData   |       |   Render Thread  |
|  Thread          |  -->  |   (Thread-safe    |  -->  |                  |
|                  |       |    snapshot)      |       |                  |
| - update()       |       | - instances[]     |       | - renderWithData |
| - updateNeeds    |       | - tintColors      |       |                  |
| - updateSchedule |       | - modelMatrices   |       |                  |
+------------------+       +-------------------+       +------------------+
```

### Usage

**Single-threaded (current):**
```cpp
npcManager.update(deltaTime, playerPos, physics);
npcManager.updateAnimations(deltaTime, renderer, frameIndex);
npcManager.render(cmd, frameIndex, renderer);
```

**Multi-threaded (future):**
```cpp
// Simulation thread
npcManager.update(deltaTime, playerPos, physics);
NPCRenderData renderData;
npcManager.generateRenderData(renderData);

// Render thread (with renderData passed safely)
npcManager.updateAnimations(deltaTime, renderer, frameIndex);
npcManager.renderWithData(cmd, frameIndex, renderer, renderData);
```

### Key Types

| Type | Purpose |
|------|---------|
| `NPCRenderInstance` | Per-NPC render state (transform, tint, bone slot) |
| `NPCRenderData` | Snapshot of all NPCs for render thread |
| `NPCRenderConfig` | Rendering configuration (scale, debug flags) |

## Current State vs Target State

### Current Implementation

| Feature | Status | Notes |
|---------|--------|-------|
| Virtual/Bulk/Real LOD | Done | Fully functional |
| Needs system | Done | 6 needs with growth/decay |
| Schedule system | Done | 7 archetypes, 6 periods |
| Systemic events | Partial | Fistfights, conversations only |
| Behavior trees | Done | Per-hostility-level trees |
| Skinned animation | Done | Shared skeleton, per-NPC animation |
| Hostility tinting | Done | Color-coded by hostility |
| Physics integration | Done | Real NPCs only |
| Perception (sight/hearing) | Done | Real NPCs only |
| Thread-safe render data | Done | NPCRenderData for decoupled rendering |
| Player PBR matching | Done | NPCs use player's material properties |

### Target State (Future Enhancements)

#### High Priority

1. **Bulk Mesh LOD**
   - Currently: Bulk NPCs use full mesh
   - Target: Low-poly impostor or billboard for Bulk NPCs
   - Impact: 3-5x more visible NPCs

2. **Spatial Partitioning**
   - Currently: Linear scan for proximity queries
   - Target: Octree or grid-based spatial hash
   - Impact: O(n) → O(log n) for nearby NPC queries

3. **More Systemic Events**
   - Pickpocket events (thief → victim)
   - Mugging events (hostile → neutral)
   - Rescue events (guard → victim)
   - Transaction events (merchant ↔ customer)

4. **GOAP Integration**
   - Currently: Fixed behavior trees per hostility level
   - Target: Goal-Oriented Action Planning for emergent goals
   - Benefit: NPCs dynamically choose actions to satisfy needs

#### Medium Priority

5. **Crowd Flocking**
   - Currently: NPCs move independently
   - Target: Reynolds flocking for natural crowd movement
   - Benefit: More realistic crowd behavior

6. **Event Reactions**
   - NPCs react to nearby events (flee from fights, gather to watch)
   - Crowd panic propagation

7. **Memory System**
   - NPCs remember player actions
   - Persistent reputation affects initial hostility

8. **Occupation-Specific Behaviors**
   - Guards: patrol routes, investigate disturbances
   - Merchants: tend shop, call out wares
   - Farmers: work fields, carry produce

#### Lower Priority

9. **Conversation System**
   - Procedural dialogue barks
   - Context-aware comments

10. **Group Behaviors**
    - Families that stay together
    - Guard squads with coordinated response

11. **Weather/Time Reactions**
    - Seek shelter in rain
    - Light torches at night

## Performance Considerations

### Update Costs (per frame)

| LOD State | Operations | Relative Cost |
|-----------|------------|---------------|
| Virtual | Needs accumulation only | 1x |
| Bulk | Needs + simple movement | 5x |
| Real | Full AI + physics + perception | 50x |

### Recommended Budgets

| Scene Type | Virtual | Bulk | Real |
|------------|---------|------|------|
| Village | 100-200 | 40-60 | 10-20 |
| City | 500-1000 | 80-120 | 30-40 |
| Wilderness | 20-50 | 10-20 | 5-10 |

### Memory Usage

Per NPC approximate memory:
- NPC struct: ~400 bytes
- Behavior tree: ~200 bytes
- Total per NPC: ~600 bytes
- 1000 NPCs: ~600 KB

## Testing

### Manual Testing Checklist

1. **LOD Transitions**
   - Spawn NPCs at varying distances
   - Walk toward/away from NPCs
   - Verify debug summary shows correct V/B/R counts
   - Check no visual popping during transitions

2. **Needs and Schedules**
   - Set time scale high (e.g., 600) to speed up day cycle
   - Observe NPCs moving between locations
   - Verify schedules match archetype definitions

3. **Systemic Events**
   - Spawn hostile NPCs near neutral NPCs
   - Wait for fistfight events
   - Verify events appear in `getActiveEvents()`

4. **Performance**
   - Spawn 100+ NPCs
   - Verify Real count stays within budget
   - Check frame rate remains stable

### Debug Commands

```cpp
// Get LOD state counts
SDL_Log("%s", npcManager.getDebugSummary().c_str());

// Output example:
// NPCs: 45/50 alive, 3 hostile
//   LOD: V=30 B=12 R=8 | Time: 14:30 | Events: 1

// Set time for testing
npcManager.setTimeOfDay(21.0f);  // Night time

// Fast-forward time
npcManager.setTimeScale(600.0f);  // 10 hours per minute
```

## References

- [GDC 2018: AI Postmortem - The Living World of AC Origins](https://www.gdcvault.com/)
- [GDC 2017: Bringing Life to Open World Games - AC Unity Crowds](https://www.gdcvault.com/)
- [GDC 2019: GOAP in AC Odyssey](https://www.gdcvault.com/)
