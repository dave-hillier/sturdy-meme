#pragma once

/**
 * Interface for post-processing state controls.
 * PostProcessSystem implements this directly.
 * Used by GUI to control HDR, bloom, god rays, local tone mapping, exposure.
 */
class IPostProcessState {
public:
    virtual ~IPostProcessState() = default;

    // HDR tonemapping
    virtual void setHDREnabled(bool enabled) = 0;
    virtual bool isHDREnabled() const = 0;

    // HDR pass (whether to render to HDR target)
    virtual void setHDRPassEnabled(bool enabled) = 0;
    virtual bool isHDRPassEnabled() const = 0;

    // Bloom
    virtual void setBloomEnabled(bool enabled) = 0;
    virtual bool isBloomEnabled() const = 0;

    // God rays
    virtual void setGodRaysEnabled(bool enabled) = 0;
    virtual bool isGodRaysEnabled() const = 0;
    virtual void setGodRayQuality(int quality) = 0;
    virtual int getGodRayQuality() const = 0;

    // Froxel volumetric fog filter quality
    virtual void setFroxelFilterQuality(bool highQuality) = 0;
    virtual bool isFroxelFilterHighQuality() const = 0;

    // Froxel debug visualization mode
    // 0 = Normal, 1 = Depth slices, 2 = Density, 3 = Transmittance, 4 = Grid cells
    virtual void setFroxelDebugMode(int mode) = 0;
    virtual int getFroxelDebugMode() const = 0;

    // Local tone mapping (bilateral grid)
    virtual void setLocalToneMapEnabled(bool enabled) = 0;
    virtual bool isLocalToneMapEnabled() const = 0;
    virtual void setLocalToneMapContrast(float contrast) = 0;
    virtual float getLocalToneMapContrast() const = 0;
    virtual void setLocalToneMapDetail(float detail) = 0;
    virtual float getLocalToneMapDetail() const = 0;
    virtual void setBilateralBlend(float blend) = 0;
    virtual float getBilateralBlend() const = 0;

    // Exposure
    virtual void setAutoExposureEnabled(bool enabled) = 0;
    virtual bool isAutoExposureEnabled() const = 0;
    virtual void setManualExposure(float ev) = 0;
    virtual float getManualExposure() const = 0;
    virtual float getCurrentExposure() const = 0;
};
