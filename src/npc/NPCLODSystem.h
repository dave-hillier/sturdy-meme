#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// =============================================================================
// NPC Level of Detail System
// =============================================================================
// Based on Assassin's Creed Origins' Meta AI architecture:
// - Virtual: Far away NPCs, only data exists, update every 5-15 seconds
// - Bulk: Medium range, simplified mesh/animation, update every second
// - Real: Close range, full mesh/animation/AI, update every frame
// =============================================================================

// NPC representation state (inspired by AC Origins' Virtual/Bulk/Real system)
enum class NPCLODState : uint8_t {
    Virtual,    // Far away: no visual, just track needs/goals (update every 5-15s)
    Bulk,       // Medium range: low-poly mesh, simplified animation (update every 1s)
    Real        // Close range: full mesh, full AI, full animation (update every frame)
};

// Distance thresholds for LOD transitions (in meters)
struct NPCLODConfig {
    float virtualToRealDistance = 80.0f;   // Distance to transition from virtual to real
    float realToVirtualDistance = 100.0f;  // Distance to transition from real to virtual (hysteresis)
    float bulkToRealDistance = 40.0f;      // Distance to transition from bulk to real
    float realToBulkDistance = 50.0f;      // Distance to transition from real to bulk (hysteresis)

    // Update intervals (in seconds)
    float virtualUpdateInterval = 10.0f;   // How often to update virtual NPCs (5-15s range)
    float bulkUpdateInterval = 1.0f;       // How often to update bulk NPCs

    // Maximum counts per LOD level
    uint32_t maxRealNPCs = 40;             // Maximum fully simulated NPCs
    uint32_t maxBulkNPCs = 120;            // Maximum simplified NPCs
    // Virtual NPCs have no limit - they're just data
};

// LOD state data stored per-NPC
struct NPCLODData {
    NPCLODState state = NPCLODState::Virtual;
    float timeSinceLastUpdate = 0.0f;      // Time since last state update
    float distanceToPlayer = 0.0f;         // Cached distance to player
    uint8_t updatePriority = 0;            // Higher = more important to update
};

// =============================================================================
// NPC Needs System
// =============================================================================
// Needs drive emergent behavior - NPCs seek to fulfill their needs which
// creates natural daily routines and interactions.
// =============================================================================

// Individual need values (0.0 = fully satisfied, 1.0 = urgent)
struct NPCNeeds {
    float hunger = 0.0f;       // Drives: seek food, go to tavern/home
    float tiredness = 0.0f;    // Drives: seek rest, go home, sleep
    float fear = 0.0f;         // Driven by: threats, combat. Drives: flee, hide
    float aggression = 0.0f;   // Driven by: provocation, hostility. Drives: attack, confront
    float social = 0.0f;       // Drives: seek conversation, gather in groups
    float work = 0.0f;         // Drives: go to workplace, perform job tasks

    // Need decay/growth rates per hour of game time
    static constexpr float HUNGER_RATE = 0.15f;      // Gets hungry over ~7 hours
    static constexpr float TIREDNESS_RATE = 0.06f;   // Gets tired over ~16 hours
    static constexpr float FEAR_DECAY = 2.0f;        // Fear decays quickly when safe
    static constexpr float AGGRESSION_DECAY = 1.0f;  // Aggression decays over time
    static constexpr float SOCIAL_RATE = 0.1f;       // Gets lonely over ~10 hours
    static constexpr float WORK_RATE = 0.2f;         // Needs to work over ~5 hours

    // Update needs based on elapsed game time (in hours)
    void update(float gameHours, bool isSafe, bool isWorking, bool isSocializing, bool isEating, bool isResting) {
        // Passive need growth
        hunger += HUNGER_RATE * gameHours;
        tiredness += TIREDNESS_RATE * gameHours;
        social += SOCIAL_RATE * gameHours;

        // Decay fear and aggression when safe
        if (isSafe) {
            fear = glm::max(0.0f, fear - FEAR_DECAY * gameHours);
            aggression = glm::max(0.0f, aggression - AGGRESSION_DECAY * gameHours);
        }

        // Activities satisfy needs
        if (isEating) hunger = glm::max(0.0f, hunger - 0.5f * gameHours);
        if (isResting) tiredness = glm::max(0.0f, tiredness - 0.3f * gameHours);
        if (isSocializing) social = glm::max(0.0f, social - 0.4f * gameHours);
        if (isWorking) {
            work = glm::max(0.0f, work - 0.3f * gameHours);
            tiredness += 0.02f * gameHours;  // Working makes you tired
        }

        // Clamp all values
        hunger = glm::clamp(hunger, 0.0f, 1.0f);
        tiredness = glm::clamp(tiredness, 0.0f, 1.0f);
        fear = glm::clamp(fear, 0.0f, 1.0f);
        aggression = glm::clamp(aggression, 0.0f, 1.0f);
        social = glm::clamp(social, 0.0f, 1.0f);
        work = glm::clamp(work, 0.0f, 1.0f);
    }

    // Get the most urgent need
    enum class NeedType { None, Hunger, Tiredness, Fear, Aggression, Social, Work };

    NeedType getMostUrgentNeed(float threshold = 0.7f) const {
        // Fear and aggression take priority (survival instincts)
        if (fear > threshold) return NeedType::Fear;
        if (aggression > threshold) return NeedType::Aggression;

        // Then physical needs
        if (tiredness > 0.9f) return NeedType::Tiredness;  // Exhaustion is urgent
        if (hunger > threshold) return NeedType::Hunger;
        if (tiredness > threshold) return NeedType::Tiredness;

        // Then work and social
        if (work > threshold) return NeedType::Work;
        if (social > threshold) return NeedType::Social;

        return NeedType::None;
    }

    // Get urgency score (for prioritizing which NPC to update)
    float getUrgencyScore() const {
        // Weight fear and aggression higher
        return fear * 2.0f + aggression * 1.5f + tiredness + hunger + social * 0.5f + work * 0.5f;
    }
};

// =============================================================================
// NPC Schedule System
// =============================================================================
// NPCs follow daily schedules that define where they should be and what
// they should be doing at different times of day.
// =============================================================================

// Time of day periods
enum class DayPeriod : uint8_t {
    Dawn,       // 5:00 - 7:00   - Wake up, prepare for day
    Morning,    // 7:00 - 12:00  - Work/activities
    Midday,     // 12:00 - 14:00 - Lunch break
    Afternoon,  // 14:00 - 18:00 - Work/activities
    Evening,    // 18:00 - 21:00 - Leisure, socializing
    Night       // 21:00 - 5:00  - Sleep
};

// Activity types for schedule entries
enum class ScheduleActivity : uint8_t {
    Sleep,          // At home, sleeping
    Eat,            // At home or tavern, eating
    Work,           // At workplace
    Patrol,         // For guards: patrol route
    Socialize,      // Town square, tavern
    Wander,         // Random wandering in area
    Idle,           // Stay in place
    Travel          // Moving between locations
};

// A single schedule entry
struct ScheduleEntry {
    DayPeriod period;
    ScheduleActivity activity;
    glm::vec3 location;         // Where to be during this period
    float locationRadius = 5.0f; // How close is "at location"
};

// NPC archetype determines default schedule
enum class NPCArchetype : uint8_t {
    Villager,       // Home -> Work -> Tavern -> Home
    Guard,          // Patrol during day, rest at night
    Merchant,       // At shop during day
    Farmer,         // Fields during day
    Noble,          // Leisurely schedule
    Beggar,         // Wanders, begs
    Child           // Play during day, home at night
};

// Get day period from hour (0-24)
inline DayPeriod getDayPeriod(float hour) {
    if (hour >= 5.0f && hour < 7.0f) return DayPeriod::Dawn;
    if (hour >= 7.0f && hour < 12.0f) return DayPeriod::Morning;
    if (hour >= 12.0f && hour < 14.0f) return DayPeriod::Midday;
    if (hour >= 14.0f && hour < 18.0f) return DayPeriod::Afternoon;
    if (hour >= 18.0f && hour < 21.0f) return DayPeriod::Evening;
    return DayPeriod::Night;
}

// =============================================================================
// Systemic Events
// =============================================================================
// Events that can occur between NPCs independent of player
// =============================================================================

enum class SystemicEventType : uint8_t {
    None,
    Pickpocket,     // Thief stealing from victim
    Fistfight,      // Two NPCs fighting
    Argument,       // Verbal confrontation
    Mugging,        // Hostile robbing victim
    Rescue,         // Guard helping victim
    Conversation,   // Friendly chat
    Transaction     // Merchant selling goods
};

struct SystemicEvent {
    SystemicEventType type = SystemicEventType::None;
    uint32_t instigatorId = 0;   // NPC who started the event
    uint32_t targetId = 0;       // NPC being targeted
    glm::vec3 location;          // Where the event is happening
    float duration = 0.0f;       // How long the event lasts
    float elapsed = 0.0f;        // Time elapsed
    bool playerCanIntervene = true;  // Can player interact with this event

    bool isActive() const { return type != SystemicEventType::None && elapsed < duration; }
    float getProgress() const { return duration > 0.0f ? elapsed / duration : 0.0f; }
};
