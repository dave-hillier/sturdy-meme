#include "AppComponent.h"
#include "CoreModule.h"
#include "InfrastructureModule.h"
#include "SystemsModule.h"

namespace di {

fruit::Component<
    VulkanContext,
    InitContext,
    RenderingInfrastructure,
    DescriptorInfrastructure,
    PostProcessBundle,
    CoreSystemsBundle,
    InfrastructureBundle
> getAppComponent(const AppConfig& config) {
    return fruit::createComponent()
        // Install configuration providers
        .install(getCoreConfigComponent(config.toCoreConfig()))
        .install(getSystemsConfigComponent(config.toSystemsConfig()))
        // Install module components
        .install(CoreModule::getComponent)
        .install(InfrastructureModule::getComponent)
        .install(SystemsModule::getComponent);
}

fruit::Component<
    VulkanContext,
    InitContext,
    RenderingInfrastructure,
    DescriptorInfrastructure
> getCoreAppComponent(const AppConfig& config) {
    return fruit::createComponent()
        // Install configuration providers
        .install(getCoreConfigComponent(config.toCoreConfig()))
        // Install core module components only
        .install(CoreModule::getComponent)
        .install(InfrastructureModule::getComponent);
}

} // namespace di
