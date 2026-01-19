// FruitComponents.cpp - Fruit DI with system lifecycle ownership
#include "FruitComponents.h"
#include "GlobalBufferManager.h"
#include "ShadowSystem.h"
#include "PostProcessSystem.h"
#include "TerrainSystem.h"

namespace {

fruit::Component<VulkanParams> getVulkanParamsComponent(VulkanParams params) {
    return fruit::createComponent()
        .registerProvider([=]() { return params; });
}

fruit::Component<GlobalBufferManagerPtr> getGlobalBufferManagerComponent() {
    return fruit::createComponent()
        .registerProvider([](VulkanParams params) -> GlobalBufferManagerPtr {
            return GlobalBufferManager::create(
                params.allocator,
                params.physicalDevice,
                params.framesInFlight
            );
        });
}

fruit::Component<ShadowSystemPtr> getShadowSystemComponent() {
    return fruit::createComponent()
        .registerProvider([](VulkanParams params) -> ShadowSystemPtr {
            ShadowSystem::InitInfo info{};
            info.device = params.device;
            info.physicalDevice = params.physicalDevice;
            info.allocator = params.allocator;
            info.mainDescriptorSetLayout = params.mainDescriptorSetLayout;
            info.shaderPath = params.shaderPath;
            info.framesInFlight = params.framesInFlight;
            return ShadowSystem::create(info);
        });
}

fruit::Component<PostProcessSystemPtr> getPostProcessSystemComponent() {
    return fruit::createComponent()
        .registerProvider([](VulkanParams params) -> PostProcessSystemPtr {
            PostProcessSystem::InitInfo info{};
            info.device = params.device;
            info.physicalDevice = params.physicalDevice;
            info.allocator = params.allocator;
            info.shaderPath = params.shaderPath;
            info.framesInFlight = params.framesInFlight;
            info.extent = params.extent;
            return PostProcessSystem::create(info);
        });
}

fruit::Component<TerrainSystemPtr> getTerrainSystemComponent() {
    return fruit::createComponent()
        .registerProvider([](VulkanParams params,
                            ShadowSystemPtr& shadow,
                            GlobalBufferManagerPtr& globalBuffers) -> TerrainSystemPtr {
            TerrainSystem::InitInfo info{};
            info.device = params.device;
            info.physicalDevice = params.physicalDevice;
            info.allocator = params.allocator;
            info.shaderPath = params.shaderPath;
            info.resourcePath = params.resourcePath;
            info.framesInFlight = params.framesInFlight;
            info.shadowResources = &shadow->getResources();
            info.globalBufferManager = globalBuffers.get();
            return TerrainSystem::create(info);
        });
}

} // anonymous namespace

fruit::Component<ShadowSystemPtr, PostProcessSystemPtr, TerrainSystemPtr, GlobalBufferManagerPtr>
getRenderingComponent(VulkanParams params) {
    return fruit::createComponent()
        .install(getVulkanParamsComponent, params)
        .install(getGlobalBufferManagerComponent)
        .install(getShadowSystemComponent)
        .install(getPostProcessSystemComponent)
        .install(getTerrainSystemComponent);
}
