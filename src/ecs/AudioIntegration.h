#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include "Components.h"

// Audio ECS Integration (Phase 8)
// Factory functions and utilities for spatial audio system
namespace AudioECS {

// ============================================================================
// Audio Source Factory Functions
// ============================================================================

// Create a basic audio source entity
inline entt::entity createAudioSource(
    entt::registry& registry,
    const glm::vec3& position,
    AudioClipHandle clip = InvalidAudioClip,
    const std::string& name = "AudioSource")
{
    auto entity = registry.create();

    registry.emplace<Transform>(entity, Transform{position, 0.0f});

    AudioSource source;
    source.clip = clip;
    registry.emplace<AudioSource>(entity, source);
    registry.emplace<IsAudioSource>(entity);

    EntityInfo info;
    info.name = name;
    info.icon = "A";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a 3D positional sound
inline entt::entity create3DSound(
    entt::registry& registry,
    const glm::vec3& position,
    AudioClipHandle clip,
    float minDist = 1.0f,
    float maxDist = 50.0f,
    const std::string& name = "3DSound")
{
    auto entity = createAudioSource(registry, position, clip, name);

    auto& source = registry.get<AudioSource>(entity);
    source.spatialize = true;
    source.minDistance = minDist;
    source.maxDistance = maxDist;
    source.rolloff = AudioSource::Rolloff::Logarithmic;

    return entity;
}

// Create a looping ambient sound
inline entt::entity createAmbientSound(
    entt::registry& registry,
    const glm::vec3& position,
    AudioClipHandle clip,
    float radius = 20.0f,
    const std::string& name = "AmbientSound")
{
    auto entity = create3DSound(registry, position, clip, radius * 0.5f, radius, name);

    auto& source = registry.get<AudioSource>(entity);
    source.looping = true;
    source.playOnAwake = true;

    AudioMixerGroup mixer;
    mixer.group = AudioMixerGroup::Group::Ambient;
    registry.emplace<AudioMixerGroup>(entity, mixer);

    return entity;
}

// Create a one-shot sound effect (auto-destroys after playing)
inline entt::entity createOneShotSound(
    entt::registry& registry,
    const glm::vec3& position,
    AudioClipHandle clip,
    float volume = 1.0f,
    float pitch = 1.0f)
{
    auto entity = registry.create();

    registry.emplace<Transform>(entity, Transform{position, 0.0f});

    OneShotAudio oneShot;
    oneShot.clip = clip;
    oneShot.volume = volume;
    oneShot.pitch = pitch;
    registry.emplace<OneShotAudio>(entity, oneShot);

    return entity;
}

// Create a directional sound (cone attenuation)
inline entt::entity createDirectionalSound(
    entt::registry& registry,
    const glm::vec3& position,
    float yaw,
    AudioClipHandle clip,
    float innerAngle = 60.0f,
    float outerAngle = 120.0f,
    const std::string& name = "DirectionalSound")
{
    auto entity = createAudioSource(registry, position, clip, name);
    registry.get<Transform>(entity).yaw = yaw;

    auto& source = registry.get<AudioSource>(entity);
    source.coneInnerAngle = innerAngle;
    source.coneOuterAngle = outerAngle;
    source.coneOuterVolume = 0.2f;

    return entity;
}

// ============================================================================
// Audio Listener
// ============================================================================

// Create an audio listener entity
inline entt::entity createAudioListener(
    entt::registry& registry,
    const glm::vec3& position,
    const std::string& name = "AudioListener")
{
    auto entity = registry.create();

    registry.emplace<Transform>(entity, Transform{position, 0.0f});
    registry.emplace<AudioListener>(entity);
    registry.emplace<ActiveAudioListener>(entity);

    EntityInfo info;
    info.name = name;
    info.icon = "L";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Set which entity is the active listener
inline void setActiveListener(entt::registry& registry, entt::entity listener) {
    // Remove active tag from all listeners
    auto view = registry.view<ActiveAudioListener>();
    for (auto entity : view) {
        registry.remove<ActiveAudioListener>(entity);
        if (registry.all_of<AudioListener>(entity)) {
            registry.get<AudioListener>(entity).active = false;
        }
    }

    // Set new active listener
    if (registry.valid(listener) && registry.all_of<AudioListener>(listener)) {
        registry.emplace_or_replace<ActiveAudioListener>(listener);
        registry.get<AudioListener>(listener).active = true;
    }
}

// Get the active listener entity
inline entt::entity getActiveListener(entt::registry& registry) {
    auto view = registry.view<ActiveAudioListener>();
    for (auto entity : view) {
        return entity;
    }
    return entt::null;
}

// ============================================================================
// Audio Zones
// ============================================================================

// Create an ambient sound zone
inline entt::entity createAmbientZone(
    entt::registry& registry,
    const glm::vec3& center,
    const glm::vec3& extents,
    AudioClipHandle clip,
    float volume = 1.0f,
    const std::string& name = "AmbientZone")
{
    auto entity = registry.create();

    registry.emplace<Transform>(entity, Transform{center, 0.0f});

    AmbientSoundZone zone;
    zone.clip = clip;
    zone.extents = extents;
    zone.volume = volume;
    zone.looping = true;
    registry.emplace<AmbientSoundZone>(entity, zone);

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "Z";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a reverb zone
inline entt::entity createReverbZone(
    entt::registry& registry,
    const glm::vec3& center,
    const glm::vec3& extents,
    ReverbZone::Preset preset = ReverbZone::Preset::Room,
    const std::string& name = "ReverbZone")
{
    auto entity = registry.create();

    registry.emplace<Transform>(entity, Transform{center, 0.0f});

    ReverbZone reverb;
    reverb.extents = extents;
    reverb.preset = preset;
    registry.emplace<ReverbZone>(entity, reverb);

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "R";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// ============================================================================
// Music System
// ============================================================================

// Create a music track controller entity
inline entt::entity createMusicTrack(
    entt::registry& registry,
    AudioClipHandle clip,
    const std::string& name = "MusicTrack")
{
    auto entity = registry.create();

    MusicTrack music;
    music.clip = clip;
    music.looping = true;
    registry.emplace<MusicTrack>(entity, music);

    AudioMixerGroup mixer;
    mixer.group = AudioMixerGroup::Group::Music;
    registry.emplace<AudioMixerGroup>(entity, mixer);

    EntityInfo info;
    info.name = name;
    info.icon = "M";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Start playing a music track with fade-in
inline void playMusic(entt::registry& registry, entt::entity musicEntity, float fadeIn = 2.0f) {
    if (!registry.all_of<MusicTrack>(musicEntity)) return;

    auto& music = registry.get<MusicTrack>(musicEntity);
    music.fadeInDuration = fadeIn;
    music.playing = true;
    music.state = (fadeIn > 0.0f) ? MusicTrack::State::FadingIn : MusicTrack::State::Playing;
}

// Stop playing with fade-out
inline void stopMusic(entt::registry& registry, entt::entity musicEntity, float fadeOut = 2.0f) {
    if (!registry.all_of<MusicTrack>(musicEntity)) return;

    auto& music = registry.get<MusicTrack>(musicEntity);
    music.fadeOutDuration = fadeOut;
    music.state = (fadeOut > 0.0f) ? MusicTrack::State::FadingOut : MusicTrack::State::Stopped;
    if (fadeOut <= 0.0f) {
        music.playing = false;
    }
}

// Crossfade to a new track
inline void crossfadeMusic(
    entt::registry& registry,
    entt::entity musicEntity,
    AudioClipHandle newClip,
    float duration = 3.0f)
{
    if (!registry.all_of<MusicTrack>(musicEntity)) return;

    auto& music = registry.get<MusicTrack>(musicEntity);
    music.nextClip = newClip;
    music.fadeOutDuration = duration;
    music.fadeInDuration = duration;
    music.crossfadeProgress = 0.0f;
    music.state = MusicTrack::State::Crossfading;
}

// ============================================================================
// Spatial Audio Calculations
// ============================================================================

// Calculate distance attenuation
inline float calculateAttenuation(
    float distance,
    float minDist,
    float maxDist,
    AudioSource::Rolloff rolloff,
    float rolloffFactor = 1.0f)
{
    if (distance <= minDist) return 1.0f;
    if (distance >= maxDist) return 0.0f;

    float normalizedDist = (distance - minDist) / (maxDist - minDist);

    switch (rolloff) {
        case AudioSource::Rolloff::Linear:
            return 1.0f - normalizedDist;

        case AudioSource::Rolloff::Logarithmic:
            // Inverse square law approximation
            return minDist / (minDist + rolloffFactor * (distance - minDist));

        case AudioSource::Rolloff::Custom:
            return std::pow(1.0f - normalizedDist, rolloffFactor);

        default:
            return 1.0f;
    }
}

// Calculate cone attenuation for directional sounds
inline float calculateConeAttenuation(
    const glm::vec3& sourcePos,
    const glm::vec3& sourceForward,
    const glm::vec3& listenerPos,
    float innerAngle,
    float outerAngle,
    float outerVolume)
{
    if (innerAngle >= 360.0f) return 1.0f;

    glm::vec3 toListener = glm::normalize(listenerPos - sourcePos);
    float angle = glm::degrees(std::acos(glm::clamp(glm::dot(sourceForward, toListener), -1.0f, 1.0f)));

    float halfInner = innerAngle * 0.5f;
    float halfOuter = outerAngle * 0.5f;

    if (angle <= halfInner) return 1.0f;
    if (angle >= halfOuter) return outerVolume;

    // Interpolate between inner and outer
    float t = (angle - halfInner) / (halfOuter - halfInner);
    return glm::mix(1.0f, outerVolume, t);
}

// Calculate doppler pitch shift
inline float calculateDopplerPitch(
    const glm::vec3& sourcePos,
    const glm::vec3& sourceVel,
    const glm::vec3& listenerPos,
    const glm::vec3& listenerVel,
    float speedOfSound = 343.0f,
    float dopplerFactor = 1.0f)
{
    glm::vec3 direction = listenerPos - sourcePos;
    float distance = glm::length(direction);
    if (distance < 0.001f) return 1.0f;

    direction /= distance;

    float sourceSpeed = glm::dot(sourceVel, direction);
    float listenerSpeed = glm::dot(listenerVel, direction);

    // Clamp to avoid infinite/negative pitch
    sourceSpeed = glm::clamp(sourceSpeed * dopplerFactor, -speedOfSound * 0.9f, speedOfSound * 0.9f);
    listenerSpeed = glm::clamp(listenerSpeed * dopplerFactor, -speedOfSound * 0.9f, speedOfSound * 0.9f);

    return (speedOfSound - listenerSpeed) / (speedOfSound - sourceSpeed);
}

// ============================================================================
// Update Systems
// ============================================================================

// Update one-shot sounds (remove completed ones)
inline void updateOneShotSounds(entt::registry& registry, float deltaTime) {
    std::vector<entt::entity> toRemove;

    auto view = registry.view<OneShotAudio>();
    for (auto entity : view) {
        auto& oneShot = view.get<OneShotAudio>(entity);

        if (!oneShot.started) {
            oneShot.elapsedDelay += deltaTime;
            if (oneShot.elapsedDelay >= oneShot.delay) {
                oneShot.started = true;
                // Audio backend would start playing here
            }
        }
        // Audio backend would check if finished and mark for removal
    }

    for (auto entity : toRemove) {
        registry.destroy(entity);
    }
}

// Update music track states
inline void updateMusicTracks(entt::registry& registry, float deltaTime) {
    auto view = registry.view<MusicTrack>();

    for (auto entity : view) {
        auto& music = view.get<MusicTrack>(entity);

        switch (music.state) {
            case MusicTrack::State::FadingIn:
                music.crossfadeProgress += deltaTime / music.fadeInDuration;
                if (music.crossfadeProgress >= 1.0f) {
                    music.crossfadeProgress = 1.0f;
                    music.state = MusicTrack::State::Playing;
                }
                break;

            case MusicTrack::State::FadingOut:
                music.crossfadeProgress -= deltaTime / music.fadeOutDuration;
                if (music.crossfadeProgress <= 0.0f) {
                    music.crossfadeProgress = 0.0f;
                    music.state = MusicTrack::State::Stopped;
                    music.playing = false;
                }
                break;

            case MusicTrack::State::Crossfading:
                music.crossfadeProgress += deltaTime / music.fadeOutDuration;
                if (music.crossfadeProgress >= 1.0f) {
                    music.clip = music.nextClip;
                    music.nextClip = InvalidAudioClip;
                    music.crossfadeProgress = 1.0f;
                    music.state = MusicTrack::State::Playing;
                }
                break;

            default:
                break;
        }
    }
}

// Update ambient zones based on listener position
inline void updateAmbientZones(entt::registry& registry, const glm::vec3& listenerPos) {
    auto view = registry.view<AmbientSoundZone, Transform>();

    for (auto entity : view) {
        auto& zone = view.get<AmbientSoundZone>(entity);
        auto& transform = view.get<Transform>(entity);

        glm::vec3 localPos = listenerPos - transform.position;
        glm::vec3 absLocal = glm::abs(localPos);

        // Check if inside zone
        bool inside = absLocal.x <= zone.extents.x &&
                      absLocal.y <= zone.extents.y &&
                      absLocal.z <= zone.extents.z;

        // Calculate distance from zone edge
        float distFromEdge = 0.0f;
        if (!inside) {
            glm::vec3 clamped = glm::clamp(localPos, -zone.extents, zone.extents);
            distFromEdge = glm::distance(localPos, clamped);
        }

        // Calculate volume based on distance
        float targetVolume = 0.0f;
        if (inside) {
            targetVolume = zone.volume;
        } else if (distFromEdge < zone.fadeDistance) {
            targetVolume = zone.volume * (1.0f - distFromEdge / zone.fadeDistance);
        }

        zone.currentlyInside = inside;
        zone.currentVolume = targetVolume;
    }
}

// Update reverb zone blending
inline void updateReverbZones(entt::registry& registry, const glm::vec3& listenerPos) {
    auto view = registry.view<ReverbZone, Transform>();

    for (auto entity : view) {
        auto& reverb = view.get<ReverbZone>(entity);
        auto& transform = view.get<Transform>(entity);

        glm::vec3 localPos = listenerPos - transform.position;
        glm::vec3 absLocal = glm::abs(localPos);

        bool inside = absLocal.x <= reverb.extents.x &&
                      absLocal.y <= reverb.extents.y &&
                      absLocal.z <= reverb.extents.z;

        float distFromEdge = 0.0f;
        if (!inside) {
            glm::vec3 clamped = glm::clamp(localPos, -reverb.extents, reverb.extents);
            distFromEdge = glm::distance(localPos, clamped);
        }

        if (inside) {
            reverb.blendWeight = 1.0f;
        } else if (distFromEdge < reverb.fadeDistance) {
            reverb.blendWeight = 1.0f - distFromEdge / reverb.fadeDistance;
        } else {
            reverb.blendWeight = 0.0f;
        }
    }
}

// ============================================================================
// Query Functions
// ============================================================================

// Get all audio sources
inline std::vector<entt::entity> getAudioSources(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<IsAudioSource>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get playing audio sources
inline std::vector<entt::entity> getPlayingAudioSources(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<AudioSource>();
    for (auto entity : view) {
        if (view.get<AudioSource>(entity).playing) {
            result.push_back(entity);
        }
    }
    return result;
}

// Get audio sources within range of a position
inline std::vector<entt::entity> getAudioSourcesInRange(
    entt::registry& registry,
    const glm::vec3& position,
    float range)
{
    std::vector<entt::entity> result;
    auto view = registry.view<AudioSource, Transform>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        float dist = glm::distance(position, transform.position);
        if (dist <= range) {
            result.push_back(entity);
        }
    }

    return result;
}

// Get all ambient zones
inline std::vector<entt::entity> getAmbientZones(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<AmbientSoundZone>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all reverb zones
inline std::vector<entt::entity> getReverbZones(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<ReverbZone>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// ============================================================================
// Playback Control
// ============================================================================

// Play an audio source
inline void play(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<AudioSource>(entity)) {
        auto& source = registry.get<AudioSource>(entity);
        source.playing = true;
        source.paused = false;
    }
}

// Pause an audio source
inline void pause(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<AudioSource>(entity)) {
        auto& source = registry.get<AudioSource>(entity);
        source.paused = true;
    }
}

// Stop an audio source
inline void stop(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<AudioSource>(entity)) {
        auto& source = registry.get<AudioSource>(entity);
        source.playing = false;
        source.paused = false;
        source.playbackPosition = 0.0f;
    }
}

// Stop all audio sources
inline void stopAll(entt::registry& registry) {
    auto view = registry.view<AudioSource>();
    for (auto entity : view) {
        auto& source = view.get<AudioSource>(entity);
        source.playing = false;
        source.paused = false;
        source.playbackPosition = 0.0f;
    }
}

// ============================================================================
// Statistics
// ============================================================================

struct AudioStats {
    uint32_t totalSources;
    uint32_t playingSources;
    uint32_t pausedSources;
    uint32_t ambientZones;
    uint32_t reverbZones;
    uint32_t musicTracks;
    uint32_t playingMusicTracks;
};

inline AudioStats getAudioStats(entt::registry& registry) {
    AudioStats stats{};

    auto sourceView = registry.view<AudioSource>();
    stats.totalSources = static_cast<uint32_t>(sourceView.size());

    for (auto entity : sourceView) {
        auto& source = sourceView.get<AudioSource>(entity);
        if (source.playing && !source.paused) {
            stats.playingSources++;
        } else if (source.paused) {
            stats.pausedSources++;
        }
    }

    stats.ambientZones = static_cast<uint32_t>(registry.view<AmbientSoundZone>().size());
    stats.reverbZones = static_cast<uint32_t>(registry.view<ReverbZone>().size());

    auto musicView = registry.view<MusicTrack>();
    stats.musicTracks = static_cast<uint32_t>(musicView.size());
    for (auto entity : musicView) {
        if (musicView.get<MusicTrack>(entity).playing) {
            stats.playingMusicTracks++;
        }
    }

    return stats;
}

}  // namespace AudioECS
