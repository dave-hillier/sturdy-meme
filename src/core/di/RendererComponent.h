#pragma once

#include <fruit/fruit.h>
#include <optional>
#include <string>

#include <glm/glm.hpp>

#include "CoreResources.h"
#include "DescriptorManager.h"
#include "Renderer.h"
#include "atmosphere/AtmosphereSystemGroup.h"
#include "atmosphere/SnowSystemGroup.h"
#include "postprocess/PostProcessSystem.h"
#include "subdivision/GeometrySystemGroup.h"
#include "vegetation/ScatterSystemFactory.h"
#include "vegetation/VegetationSystemGroup.h"
#include "water/WaterSystemGroup.h"

class AssetRegistry;
class DebugLineSystem;
class GlobalBufferManager;
class HiZSystem;
class Profiler;
class SceneManager;
class ShadowSystem;
class SkinnedMeshRenderer;
class TerrainSystem;
class VulkanContext;

namespace core::di {

struct RendererSubsystemBundle {
    std::optional<PostProcessSystem::Bundle> postProcess;
    std::unique_ptr<SkinnedMeshRenderer> skinnedMesh;
    std::unique_ptr<GlobalBufferManager> globalBuffers;
    std::unique_ptr<ShadowSystem> shadow;
    std::unique_ptr<TerrainSystem> terrain;
    std::unique_ptr<SceneManager> scene;
    CoreResources core;
    std::optional<SnowSystemGroup::Bundle> snow;
    std::optional<VegetationSystemGroup::Bundle> vegetation;
    std::optional<AtmosphereSystemGroup::Bundle> atmosphere;
    std::optional<GeometrySystemGroup::Bundle> geometry;
    std::unique_ptr<HiZSystem> hiZ;
    std::unique_ptr<Profiler> profiler;
    std::optional<WaterSystemGroup::Bundle> water;
    std::unique_ptr<DebugLineSystem> debugLine;
};

fruit::Component<RendererSubsystemBundle> getRendererComponent(
    VulkanContext& vulkanContext,
    const InitContext& initContext,
    const Renderer::Config& rendererConfig,
    const std::string& resourcePath,
    uint32_t framesInFlight,
    DescriptorManager::Pool* descriptorPool,
    VkDescriptorSetLayout mainDescriptorSetLayout,
    const ScatterSystemFactory::RockConfig& rockConfig,
    AssetRegistry* assetRegistry,
    const glm::vec2& sceneOrigin);

} // namespace core::di
