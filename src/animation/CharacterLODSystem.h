#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <array>

#include "CharacterLOD.h"
#include "SkinnedMesh.h"

class AnimatedCharacter;

// Manages LOD for skinned characters
// Supports:
// - Multiple mesh LOD levels per character
// - Animation update frequency reduction at distance
// - Screen-space or distance-based LOD selection
// - Smooth LOD transitions with dithering
class CharacterLODSystem {
public:
    // Passkey for controlled construction
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit CharacterLODSystem(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
    };

    static std::unique_ptr<CharacterLODSystem> create(const InitInfo& info);
    ~CharacterLODSystem();

    // Non-copyable, non-movable
    CharacterLODSystem(const CharacterLODSystem&) = delete;
    CharacterLODSystem& operator=(const CharacterLODSystem&) = delete;
    CharacterLODSystem(CharacterLODSystem&&) = delete;
    CharacterLODSystem& operator=(CharacterLODSystem&&) = delete;

    // Register a character with the LOD system
    // Returns character index for future reference
    uint32_t registerCharacter(AnimatedCharacter* character, float boundingSphereRadius);

    // Generate LOD meshes for a character from its base mesh
    // Uses mesh simplification to create lower detail levels
    // targetReduction: array of target triangle counts as fraction of original (e.g., {0.5, 0.25, 0.1})
    bool generateLODMeshes(uint32_t characterIndex,
                           const std::array<float, CHARACTER_LOD_LEVELS - 1>& targetReductions);

    // Manually set LOD mesh data (for externally generated LODs)
    bool setLODMesh(uint32_t characterIndex, uint32_t lodLevel, const CharacterLODMeshData& meshData);

    // Update LOD states based on camera position
    void update(float deltaTime, const glm::vec3& cameraPos,
                const CharacterScreenParams& screenParams = CharacterScreenParams());

    // Get LOD state for a character
    const CharacterLODState& getCharacterLODState(uint32_t characterIndex) const;

    // Get current LOD mesh for a character (for rendering)
    const CharacterLODMesh* getCurrentLODMesh(uint32_t characterIndex) const;

    // Get mesh for specific LOD level (for transition rendering)
    const CharacterLODMesh* getLODMesh(uint32_t characterIndex, uint32_t lodLevel) const;

    // Check if animation should be updated this frame for a character
    bool shouldUpdateAnimation(uint32_t characterIndex) const;

    // Mark that animation was updated (resets frame counter)
    void markAnimationUpdated(uint32_t characterIndex);

    // Update character world position (needed for distance calculation)
    void setCharacterPosition(uint32_t characterIndex, const glm::vec3& position);

    // Get number of registered characters
    uint32_t getCharacterCount() const { return static_cast<uint32_t>(characters_.size()); }

    // Configuration access
    CharacterLODConfig& getConfig() { return config_; }
    const CharacterLODConfig& getConfig() const { return config_; }

    // Statistics for debugging
    struct Stats {
        uint32_t totalCharacters = 0;
        std::array<uint32_t, CHARACTER_LOD_LEVELS> charactersPerLOD = {};
        uint32_t animationsSkipped = 0;
        uint32_t transitionsInProgress = 0;
    };
    Stats getStats() const;

    // Debug info
    struct DebugInfo {
        uint32_t characterIndex = 0;
        float distance = 0.0f;
        float screenSize = 0.0f;
        uint32_t currentLOD = 0;
        uint32_t targetLOD = 0;
        float transitionProgress = 0.0f;
        uint32_t triangleCount = 0;
    };
    std::vector<DebugInfo> getDebugInfo() const;

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    // Upload mesh data to GPU
    bool uploadLODMesh(uint32_t characterIndex, uint32_t lodLevel, const CharacterLODMeshData& meshData);

    // Mesh simplification using edge collapse
    CharacterLODMeshData simplifyMesh(const std::vector<SkinnedVertex>& vertices,
                                       const std::vector<uint32_t>& indices,
                                       float targetReduction);

    // Update single character's LOD state
    void updateCharacterLOD(uint32_t index, float deltaTime, const glm::vec3& cameraPos,
                             const CharacterScreenParams& screenParams);

    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;

    CharacterLODConfig config_;

    // Per-character data
    struct CharacterData {
        AnimatedCharacter* character = nullptr;
        float boundingSphereRadius = 1.0f;
        glm::vec3 position{0.0f};
        CharacterLODState state;
        std::array<CharacterLODMesh, CHARACTER_LOD_LEVELS> lodMeshes;
        bool hasLODMeshes = false;  // True if LOD meshes are generated
    };
    std::vector<CharacterData> characters_;
};
