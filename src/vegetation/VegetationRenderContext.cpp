#include "VegetationRenderContext.h"
#include "RendererSystems.h"
#include "FrameData.h"
#include "DisplacementSystem.h"
#include "WindSystem.h"
#include "ShadowSystem.h"
#include "CloudShadowSystem.h"

VegetationRenderContext VegetationRenderContext::fromSystems(
    RendererSystems& systems,
    const FrameData& frame
) {
    VegetationRenderContext ctx;

    // Frame identification
    ctx.frameIndex = frame.frameIndex;
    ctx.time = frame.time;
    ctx.deltaTime = frame.deltaTime;

    // Camera state
    ctx.cameraPosition = frame.cameraPosition;
    ctx.viewMatrix = frame.view;
    ctx.projectionMatrix = frame.projection;
    ctx.viewProjectionMatrix = frame.viewProj;

    // Terrain info
    ctx.terrainSize = frame.terrainSize;
    ctx.terrainHeightScale = frame.heightScale;

    // Wind UBO
    VkDescriptorBufferInfo windInfo = systems.wind().getBufferInfo(frame.frameIndex);
    ctx.windUBO = windInfo.buffer;
    ctx.windUBOOffset = windInfo.offset;

    // Displacement
    ctx.displacementView = systems.displacement().getImageView();
    ctx.displacementSampler = systems.displacement().getSampler();
    ctx.displacementRegion = systems.displacement().getRegionVec4();

    // Shadow map
    ctx.shadowMapView = systems.shadow().getShadowImageView();
    ctx.shadowMapSampler = systems.shadow().getShadowSampler();

    // Cloud shadow
    ctx.cloudShadowView = systems.cloudShadow().getShadowMapView();
    ctx.cloudShadowSampler = systems.cloudShadow().getShadowMapSampler();

    // Environment settings
    ctx.environment = &systems.wind().getEnvironmentSettings();

    return ctx;
}
