#include "CharacterTemplate.h"
#include "AnimatedCharacter.h"
#include <SDL3/SDL.h>

CharacterTemplateFactory::CharacterTemplateFactory(const InitInfo& info)
    : allocator_(info.allocator)
    , device_(info.device)
    , commandPool_(info.commandPool)
    , queue_(info.queue)
{
}

std::unique_ptr<CharacterTemplate> CharacterTemplateFactory::createFromCharacter(
    std::unique_ptr<AnimatedCharacter> character)
{
    if (!character || !character->isLoaded()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "CharacterTemplateFactory: Cannot create template from invalid character");
        return nullptr;
    }

    auto tmpl = std::make_unique<CharacterTemplate>();

    // Transfer skeleton data
    tmpl->skeleton = character->getSkeleton();

    // Build bone LOD masks
    character->buildBoneLODMasks();
    tmpl->boneCategories = character->getBoneCategories();
    for (uint32_t i = 0; i < CHARACTER_LOD_LEVELS; ++i) {
        tmpl->boneLODMasks[i] = character->getBoneLODMask(i);
    }

    // Copy animation clips (these are lightweight data structures)
    tmpl->animations = character->getAnimations();

    // Note: For full template sharing, we would need to:
    // 1. Share the SkinnedMesh GPU buffers
    // 2. Share the Mesh GPU buffers
    // This requires architectural changes to separate ownership from the template.
    //
    // For now, this creates the template infrastructure but each NPC still
    // owns its own AnimatedCharacter. Future optimization would:
    // 1. Upload mesh once per template
    // 2. Store per-NPC bone matrices in a shared SSBO
    // 3. Use instanced rendering to draw all NPCs with same template

    SDL_Log("CharacterTemplateFactory: Created template with %zu bones, %zu animations",
            tmpl->skeleton.joints.size(), tmpl->animations.size());

    return tmpl;
}

std::unique_ptr<CharacterTemplate> CharacterTemplateFactory::loadFromFile(
    const std::string& path,
    const std::vector<std::string>& additionalAnimations)
{
    // Create AnimatedCharacter from file
    AnimatedCharacter::InitInfo charInfo{};
    charInfo.path = path;
    charInfo.allocator = allocator_;
    charInfo.device = device_;
    charInfo.commandPool = commandPool_;
    charInfo.queue = queue_;

    auto character = AnimatedCharacter::create(charInfo);
    if (!character) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "CharacterTemplateFactory: Failed to load character from %s", path.c_str());
        return nullptr;
    }

    // Load additional animations
    if (!additionalAnimations.empty()) {
        character->loadAdditionalAnimations(additionalAnimations);
    }

    auto tmpl = createFromCharacter(std::move(character));
    if (tmpl) {
        tmpl->sourcePath = path;
    }

    return tmpl;
}
