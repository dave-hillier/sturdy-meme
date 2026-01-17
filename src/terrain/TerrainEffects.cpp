#include "TerrainEffects.h"
#include "TerrainBuffers.h"
#include <cstring>

void TerrainEffects::init(const InitInfo& info) {
    framesInFlight_ = info.framesInFlight;
}

void TerrainEffects::setCausticsParams(float waterLevel, bool enabled) {
    causticsWaterLevel_ = waterLevel;
    causticsEnabled_ = enabled;
}

void TerrainEffects::setLiquidWetness(float wetness) {
    liquidConfig_.globalWetness = wetness;
}

void TerrainEffects::setLiquidConfig(const material::TerrainLiquidUBO& config) {
    liquidConfig_ = config;
}

void TerrainEffects::setMaterialLayerStack(const material::MaterialLayerStack& stack) {
    materialLayerStack_ = stack;
    materialLayerUBO_.packFromStack(materialLayerStack_);
}

void TerrainEffects::updatePerFrame(uint32_t frameIndex, float deltaTime, TerrainBuffers* buffers) {
    if (!buffers) return;

    // Update caustics animation time
    causticsTime_ += deltaTime;
    float* causticsData = static_cast<float*>(buffers->getCausticsMappedPtr(frameIndex));
    if (causticsData && causticsEnabled_) {
        causticsData[0] = causticsWaterLevel_;  // causticsWaterLevel
        causticsData[5] = causticsTime_;        // causticsTime
        causticsData[6] = 1.0f;                 // causticsEnabled
    }

    // Update liquid animation time
    liquidConfig_.updateTime(deltaTime);
    void* liquidData = buffers->getLiquidMappedPtr(frameIndex);
    if (liquidData && liquidConfig_.globalWetness > 0.0f) {
        memcpy(liquidData, &liquidConfig_, sizeof(material::TerrainLiquidUBO));
    }
}

void TerrainEffects::initializeUBOs(TerrainBuffers* buffers) {
    if (!buffers) return;

    // Initialize caustics UBO with disabled state
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        float* causticsData = static_cast<float*>(buffers->getCausticsMappedPtr(i));
        if (causticsData) {
            causticsData[0] = 0.0f;   // causticsWaterLevel
            causticsData[1] = 0.05f;  // causticsScale
            causticsData[2] = 0.3f;   // causticsSpeed
            causticsData[3] = 0.5f;   // causticsIntensity
            causticsData[4] = 20.0f;  // causticsMaxDepth
            causticsData[5] = 0.0f;   // causticsTime
            causticsData[6] = 0.0f;   // causticsEnabled (disabled by default)
            causticsData[7] = 0.0f;   // causticsPadding
        }
    }

    // Initialize liquid UBO with default state (no wetness)
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        void* liquidData = buffers->getLiquidMappedPtr(i);
        if (liquidData) {
            memcpy(liquidData, &liquidConfig_, sizeof(material::TerrainLiquidUBO));
        }
    }

    // Initialize material layer UBO with default state
    for (uint32_t i = 0; i < framesInFlight_; i++) {
        void* layerData = buffers->getMaterialLayerMappedPtr(i);
        if (layerData) {
            memcpy(layerData, &materialLayerUBO_, sizeof(material::MaterialLayerUBO));
        }
    }
}
