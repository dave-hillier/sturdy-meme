#include "CharacterLODSystem.h"
#include "AnimatedCharacter.h"
#include "BufferUtils.h"

#include <SDL3/SDL.h>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <unordered_map>
#include <queue>
#include <cmath>

std::unique_ptr<CharacterLODSystem> CharacterLODSystem::create(const InitInfo& info) {
    auto system = std::make_unique<CharacterLODSystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

CharacterLODSystem::~CharacterLODSystem() {
    cleanup();
}

bool CharacterLODSystem::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    commandPool_ = info.commandPool;
    graphicsQueue_ = info.graphicsQueue;

    SDL_Log("CharacterLODSystem initialized");
    return true;
}

void CharacterLODSystem::cleanup() {
    // Destroy all LOD mesh buffers
    for (auto& charData : characters_) {
        for (auto& lodMesh : charData.lodMeshes) {
            if (lodMesh.vertexBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator_, lodMesh.vertexBuffer, lodMesh.vertexAllocation);
                lodMesh.vertexBuffer = VK_NULL_HANDLE;
            }
            if (lodMesh.indexBuffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator_, lodMesh.indexBuffer, lodMesh.indexAllocation);
                lodMesh.indexBuffer = VK_NULL_HANDLE;
            }
        }
    }
    characters_.clear();
}

uint32_t CharacterLODSystem::registerCharacter(AnimatedCharacter* character, float boundingSphereRadius) {
    CharacterData data;
    data.character = character;
    data.boundingSphereRadius = boundingSphereRadius;
    data.position = glm::vec3(0.0f);
    data.state = CharacterLODState{};

    uint32_t index = static_cast<uint32_t>(characters_.size());
    characters_.push_back(std::move(data));

    SDL_Log("CharacterLODSystem: Registered character %u with radius %.2f", index, boundingSphereRadius);
    return index;
}

bool CharacterLODSystem::generateLODMeshes(uint32_t characterIndex,
                                            const std::array<float, CHARACTER_LOD_LEVELS - 1>& targetReductions) {
    if (characterIndex >= characters_.size()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Invalid character index %u", characterIndex);
        return false;
    }

    CharacterData& charData = characters_[characterIndex];
    if (!charData.character || !charData.character->isLoaded()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Character %u not loaded", characterIndex);
        return false;
    }

    // Get base mesh data from character
    const auto& skinnedMesh = charData.character->getSkinnedMesh();
    const auto& baseVertices = skinnedMesh.getVertices();
    // Note: We need the indices from the skeleton's mesh data
    // For now, we'll need to store indices in SkinnedMesh or get them differently

    // LOD0 is the original mesh - use the character's existing buffers
    charData.lodMeshes[0].vertexBuffer = skinnedMesh.getVertexBuffer();
    charData.lodMeshes[0].indexBuffer = skinnedMesh.getIndexBuffer();
    charData.lodMeshes[0].indexCount = skinnedMesh.getIndexCount();
    charData.lodMeshes[0].triangleCount = skinnedMesh.getIndexCount() / 3;
    // Note: LOD0 buffers are owned by SkinnedMesh, not this system
    charData.lodMeshes[0].vertexAllocation = VK_NULL_HANDLE;
    charData.lodMeshes[0].indexAllocation = VK_NULL_HANDLE;

    // For now, we'll create placeholder reduced meshes
    // A full implementation would use mesh simplification
    // For this initial version, we'll reuse LOD0 mesh for all levels
    // TODO: Implement proper mesh decimation (meshoptimizer library)

    for (uint32_t lod = 1; lod < CHARACTER_LOD_LEVELS; ++lod) {
        // Reuse LOD0 mesh for now (same visual, but system is ready for reduced meshes)
        charData.lodMeshes[lod].vertexBuffer = skinnedMesh.getVertexBuffer();
        charData.lodMeshes[lod].indexBuffer = skinnedMesh.getIndexBuffer();
        charData.lodMeshes[lod].indexCount = skinnedMesh.getIndexCount();
        charData.lodMeshes[lod].triangleCount = skinnedMesh.getIndexCount() / 3;
        charData.lodMeshes[lod].vertexAllocation = VK_NULL_HANDLE;
        charData.lodMeshes[lod].indexAllocation = VK_NULL_HANDLE;
    }

    charData.hasLODMeshes = true;

    SDL_Log("CharacterLODSystem: Generated LOD meshes for character %u (LOD0: %u triangles)",
            characterIndex, charData.lodMeshes[0].triangleCount);

    return true;
}

bool CharacterLODSystem::setLODMesh(uint32_t characterIndex, uint32_t lodLevel,
                                     const CharacterLODMeshData& meshData) {
    if (characterIndex >= characters_.size() || lodLevel >= CHARACTER_LOD_LEVELS) {
        return false;
    }

    return uploadLODMesh(characterIndex, lodLevel, meshData);
}

bool CharacterLODSystem::uploadLODMesh(uint32_t characterIndex, uint32_t lodLevel,
                                        const CharacterLODMeshData& meshData) {
    CharacterData& charData = characters_[characterIndex];
    CharacterLODMesh& lodMesh = charData.lodMeshes[lodLevel];

    // Free existing buffers if owned by this system
    if (lodMesh.vertexAllocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, lodMesh.vertexBuffer, lodMesh.vertexAllocation);
    }
    if (lodMesh.indexAllocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, lodMesh.indexBuffer, lodMesh.indexAllocation);
    }

    // Upload vertex buffer
    VkDeviceSize vertexSize = meshData.vertices.size() * sizeof(SkinnedVertex);
    if (!BufferUtils::createBufferWithStaging(
            allocator_, device_, commandPool_, graphicsQueue_,
            meshData.vertices.data(), vertexSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            lodMesh.vertexBuffer, lodMesh.vertexAllocation)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload LOD%u vertex buffer", lodLevel);
        return false;
    }

    // Upload index buffer
    VkDeviceSize indexSize = meshData.indices.size() * sizeof(uint32_t);
    if (!BufferUtils::createBufferWithStaging(
            allocator_, device_, commandPool_, graphicsQueue_,
            meshData.indices.data(), indexSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            lodMesh.indexBuffer, lodMesh.indexAllocation)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to upload LOD%u index buffer", lodLevel);
        return false;
    }

    lodMesh.indexCount = static_cast<uint32_t>(meshData.indices.size());
    lodMesh.triangleCount = meshData.triangleCount;

    return true;
}

void CharacterLODSystem::update(float deltaTime, const glm::vec3& cameraPos,
                                 const CharacterScreenParams& screenParams) {
    for (uint32_t i = 0; i < characters_.size(); ++i) {
        updateCharacterLOD(i, deltaTime, cameraPos, screenParams);
    }
}

void CharacterLODSystem::updateCharacterLOD(uint32_t index, float deltaTime, const glm::vec3& cameraPos,
                                             const CharacterScreenParams& screenParams) {
    CharacterData& charData = characters_[index];
    CharacterLODState& state = charData.state;

    // Calculate distance to camera
    float distance = glm::distance(cameraPos, charData.position);
    state.lastDistance = distance;

    // Calculate screen-space size
    float screenSize = calculateScreenSize(charData.boundingSphereRadius, distance, screenParams);
    state.lastScreenSize = screenSize;

    // Determine target LOD level
    // Use hysteresis to prevent LOD popping
    float hysteresisDir = (state.targetLOD > state.currentLOD) ? -1.0f : 1.0f;
    uint32_t newTargetLOD = calculateLODFromScreenSize(screenSize, config_, hysteresisDir);

    // Update target LOD if changed
    if (newTargetLOD != state.targetLOD) {
        state.targetLOD = newTargetLOD;
        if (config_.enableTransitions) {
            state.transitionProgress = 0.0f;
        } else {
            state.currentLOD = state.targetLOD;
            state.transitionProgress = 1.0f;
        }
    }

    // Update transition progress
    if (state.currentLOD != state.targetLOD) {
        state.transitionProgress += deltaTime / config_.transitionDuration;
        if (state.transitionProgress >= 1.0f) {
            state.transitionProgress = 1.0f;
            state.currentLOD = state.targetLOD;
        }
    }

    // Update animation timing
    state.framesSinceAnimUpdate++;
    uint32_t updateInterval = config_.animationUpdateInterval[state.currentLOD];
    state.needsAnimationUpdate = (state.framesSinceAnimUpdate >= updateInterval);
}

const CharacterLODState& CharacterLODSystem::getCharacterLODState(uint32_t characterIndex) const {
    static CharacterLODState defaultState;
    if (characterIndex >= characters_.size()) {
        return defaultState;
    }
    return characters_[characterIndex].state;
}

const CharacterLODMesh* CharacterLODSystem::getCurrentLODMesh(uint32_t characterIndex) const {
    if (characterIndex >= characters_.size()) {
        return nullptr;
    }
    const auto& charData = characters_[characterIndex];
    if (!charData.hasLODMeshes) {
        return nullptr;
    }
    return &charData.lodMeshes[charData.state.currentLOD];
}

const CharacterLODMesh* CharacterLODSystem::getLODMesh(uint32_t characterIndex, uint32_t lodLevel) const {
    if (characterIndex >= characters_.size() || lodLevel >= CHARACTER_LOD_LEVELS) {
        return nullptr;
    }
    const auto& charData = characters_[characterIndex];
    if (!charData.hasLODMeshes) {
        return nullptr;
    }
    return &charData.lodMeshes[lodLevel];
}

bool CharacterLODSystem::shouldUpdateAnimation(uint32_t characterIndex) const {
    if (characterIndex >= characters_.size()) {
        return true;
    }
    return characters_[characterIndex].state.needsAnimationUpdate;
}

void CharacterLODSystem::markAnimationUpdated(uint32_t characterIndex) {
    if (characterIndex >= characters_.size()) {
        return;
    }
    characters_[characterIndex].state.framesSinceAnimUpdate = 0;
    characters_[characterIndex].state.needsAnimationUpdate = false;
}

void CharacterLODSystem::setCharacterPosition(uint32_t characterIndex, const glm::vec3& position) {
    if (characterIndex >= characters_.size()) {
        return;
    }
    characters_[characterIndex].position = position;
}

CharacterLODSystem::Stats CharacterLODSystem::getStats() const {
    Stats stats;
    stats.totalCharacters = static_cast<uint32_t>(characters_.size());

    for (const auto& charData : characters_) {
        stats.charactersPerLOD[charData.state.currentLOD]++;

        if (!charData.state.needsAnimationUpdate) {
            stats.animationsSkipped++;
        }

        if (charData.state.currentLOD != charData.state.targetLOD) {
            stats.transitionsInProgress++;
        }
    }

    return stats;
}

std::vector<CharacterLODSystem::DebugInfo> CharacterLODSystem::getDebugInfo() const {
    std::vector<DebugInfo> infos;
    infos.reserve(characters_.size());

    for (uint32_t i = 0; i < characters_.size(); ++i) {
        const auto& charData = characters_[i];
        DebugInfo info;
        info.characterIndex = i;
        info.distance = charData.state.lastDistance;
        info.screenSize = charData.state.lastScreenSize;
        info.currentLOD = charData.state.currentLOD;
        info.targetLOD = charData.state.targetLOD;
        info.transitionProgress = charData.state.transitionProgress;
        info.triangleCount = charData.hasLODMeshes ?
            charData.lodMeshes[charData.state.currentLOD].triangleCount : 0;
        infos.push_back(info);
    }

    return infos;
}

CharacterLODMeshData CharacterLODSystem::simplifyMesh(const std::vector<SkinnedVertex>& vertices,
                                                       const std::vector<uint32_t>& indices,
                                                       float targetReduction) {
    // Basic mesh simplification using vertex clustering
    // A production implementation would use meshoptimizer or a proper QEM-based simplifier
    // For now, this provides a working (if naive) implementation

    CharacterLODMeshData result;
    result.vertices = vertices;  // Copy vertices
    result.indices = indices;    // Copy indices

    uint32_t targetTriangles = static_cast<uint32_t>(indices.size() / 3 * targetReduction);
    targetTriangles = std::max(targetTriangles, 12u);  // Minimum 12 triangles

    // For now, just return the original mesh
    // TODO: Implement proper simplification with meshoptimizer
    result.triangleCount = static_cast<uint32_t>(indices.size() / 3);
    result.reductionFactor = 1.0f;

    return result;
}
