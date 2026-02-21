#pragma once

// ============================================================================
// HDRDrawableFactory.h - Registers all drawable adapters with the HDR pass
// ============================================================================
//
// Centralizes the knowledge of which concrete systems participate in HDR
// rendering, keeping Renderer.cpp free from those concrete includes.

class HDRPassRecorder;
class RendererSystems;

namespace HDRDrawableFactory {

/// Register all drawable adapters with the HDR pass recorder.
void registerAll(HDRPassRecorder& recorder, RendererSystems& systems);

} // namespace HDRDrawableFactory
