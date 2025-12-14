#include "CoreResources.h"
#include "PostProcessSystem.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"

HDRResources HDRResources::collect(const PostProcessSystem& postProcess) {
    HDRResources hdr;
    hdr.renderPass = postProcess.getHDRRenderPass();
    hdr.colorView = postProcess.getHDRColorView();
    hdr.depthView = postProcess.getHDRDepthView();
    hdr.framebuffer = postProcess.getHDRFramebuffer();
    hdr.extent = postProcess.getExtent();
    return hdr;
}

ShadowResources ShadowResources::collect(const ShadowSystem& shadow, uint32_t framesInFlight) {
    ShadowResources res;
    res.renderPass = shadow.getShadowRenderPass();
    res.cascadeView = shadow.getShadowImageView();
    res.sampler = shadow.getShadowSampler();
    res.mapSize = shadow.getShadowMapSize();
    res.pointShadowSampler = shadow.getPointShadowSampler();
    res.spotShadowSampler = shadow.getSpotShadowSampler();

    for (uint32_t i = 0; i < framesInFlight && i < 2; ++i) {
        res.pointShadowViews[i] = shadow.getPointShadowArrayView(i);
        res.spotShadowViews[i] = shadow.getSpotShadowArrayView(i);
    }

    return res;
}

TerrainResources TerrainResources::collect(const TerrainSystem& terrain) {
    TerrainResources res;
    res.heightMapView = terrain.getHeightMapView();
    res.heightMapSampler = terrain.getHeightMapSampler();
    res.getHeightAt = [&terrain](float x, float z) { return terrain.getHeightAt(x, z); };

    const auto& config = terrain.getConfig();
    res.size = config.size;
    res.heightScale = config.heightScale;

    return res;
}

CoreResources CoreResources::collect(
    const PostProcessSystem& postProcess,
    const ShadowSystem& shadow,
    const TerrainSystem& terrain,
    uint32_t framesInFlight
) {
    CoreResources core;
    core.hdr = HDRResources::collect(postProcess);
    core.shadow = ShadowResources::collect(shadow, framesInFlight);
    core.terrain = TerrainResources::collect(terrain);
    return core;
}
