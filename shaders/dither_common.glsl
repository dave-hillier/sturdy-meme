/*
 * dither_common.glsl - Common dithering utilities
 *
 * Provides Interleaved Gradient Noise (IGN) for LOD transitions and transparency dithering.
 * IGN produces visually pleasing, organic-looking patterns without the regular grid artifacts
 * of Bayer matrix dithering. Used by tree.frag, tree_impostor.frag, tree_leaf.frag for smooth
 * LOD blending.
 *
 * Features:
 * - Interleaved Gradient Noise (procedural, no texture needed)
 * - Temporal animation to spread dithering across frames (for TAA integration)
 * - Shortened transition zone to minimize visible dithering
 * - Optional alpha-to-coverage output for MSAA hardware blending
 */

#ifndef DITHER_COMMON_GLSL
#define DITHER_COMMON_GLSL

// Interleaved Gradient Noise
// Based on Jorge Jimenez's presentation "Next Generation Post Processing in Call of Duty: Advanced Warfare"
// Produces high-quality blue noise-like distribution without texture lookup
float interleavedGradientNoise(vec2 pixelCoord) {
    return fract(52.9829189 * fract(0.06711056 * pixelCoord.x + 0.00583715 * pixelCoord.y));
}

// Temporal IGN - rotates the noise pattern each frame for TAA integration
// frameTime should be a continuously increasing value (e.g., windDirectionAndSpeed.w)
float temporalInterleavedGradientNoise(vec2 pixelCoord, float frameTime) {
    // Use frame time to create a rotation angle (golden ratio based for good distribution)
    float frame = floor(frameTime * 60.0); // Approximate frame number at 60fps
    float phi = 1.61803398875; // Golden ratio
    float rotation = fract(frame * phi) * 6.28318530718; // 2*PI

    // Rotate the pixel coordinate slightly each frame
    float c = cos(rotation);
    float s = sin(rotation);
    vec2 rotatedCoord = vec2(
        c * pixelCoord.x - s * pixelCoord.y,
        s * pixelCoord.x + c * pixelCoord.y
    );

    // Add frame-based offset for additional temporal variation
    rotatedCoord += vec2(frame * 0.754877669, frame * 0.569840296);

    return interleavedGradientNoise(rotatedCoord);
}

// Get dither threshold for current fragment (non-temporal version)
// Use with: if (blendFactor > getDitherThreshold()) discard;
float getDitherThreshold(ivec2 pixelCoord) {
    return interleavedGradientNoise(vec2(pixelCoord));
}

// Get dither threshold with temporal variation for TAA
float getDitherThresholdTemporal(ivec2 pixelCoord, float frameTime) {
    return temporalInterleavedGradientNoise(vec2(pixelCoord), frameTime);
}

// Convenience overload using gl_FragCoord
float getDitherThreshold() {
    return getDitherThreshold(ivec2(gl_FragCoord.xy));
}

// Sharpen the transition zone to minimize visible dithering
// Maps the input blend factor [0,1] to a steeper [0,1] range
// transitionWidth: 0.1 = very short transition, 0.5 = full range
float sharpenTransition(float blendFactor, float transitionWidth) {
    // Center the transition and scale it
    float center = 0.5;
    float halfWidth = transitionWidth * 0.5;
    float low = center - halfWidth;
    float high = center + halfWidth;
    return clamp((blendFactor - low) / (high - low), 0.0, 1.0);
}

// Returns true if fragment should be discarded for LOD fade-out
// blendFactor: 0 = fully visible, 1 = fully faded out
// Uses shortened transition zone for less visible dithering
bool shouldDiscardForLOD(float blendFactor) {
    if (blendFactor < 0.01) return false;
    if (blendFactor > 0.99) return true;

    // Sharpen the transition to ~30% of the original range
    float sharpenedBlend = sharpenTransition(blendFactor, 0.3);
    return sharpenedBlend > getDitherThreshold();
}

// Temporal version - use when TAA is available
bool shouldDiscardForLODTemporal(float blendFactor, float frameTime) {
    if (blendFactor < 0.01) return false;
    if (blendFactor > 0.99) return true;

    float sharpenedBlend = sharpenTransition(blendFactor, 0.3);
    return sharpenedBlend > getDitherThresholdTemporal(ivec2(gl_FragCoord.xy), frameTime);
}

// Returns true if fragment should be discarded for LOD fade-in
// blendFactor: 0 = fully faded out, 1 = fully visible
bool shouldDiscardForLODFadeIn(float blendFactor) {
    if (blendFactor > 0.99) return false;
    if (blendFactor < 0.01) return true;

    float sharpenedBlend = sharpenTransition(blendFactor, 0.3);
    return sharpenedBlend < getDitherThreshold();
}

// Temporal version for fade-in
bool shouldDiscardForLODFadeInTemporal(float blendFactor, float frameTime) {
    if (blendFactor > 0.99) return false;
    if (blendFactor < 0.01) return true;

    float sharpenedBlend = sharpenTransition(blendFactor, 0.3);
    return sharpenedBlend < getDitherThresholdTemporal(ivec2(gl_FragCoord.xy), frameTime);
}

// Alpha-to-coverage support
// Returns an alpha value suitable for use with hardware alpha-to-coverage
// The alpha is dithered to create smooth cross-fade when MSAA is enabled
float getAlphaToCoverageValue(float blendFactor) {
    // For fade-out: higher blend = lower alpha
    float alpha = 1.0 - sharpenTransition(blendFactor, 0.3);
    // Add small noise to break up MSAA patterns
    alpha += (getDitherThreshold() - 0.5) * 0.1;
    return clamp(alpha, 0.0, 1.0);
}

// For fade-in: higher blend = higher alpha
float getAlphaToCoverageValueFadeIn(float blendFactor) {
    float alpha = sharpenTransition(blendFactor, 0.3);
    alpha += (getDitherThreshold() - 0.5) * 0.1;
    return clamp(alpha, 0.0, 1.0);
}

// ============================================================================
// Staggered LOD Crossfade
// ============================================================================
// When transitioning between full geometry and impostor, we want:
// - Trunk fades first (leads the transition)
// - Leaves and impostor cross-fade together (impostor provides backdrop for leaves)
//
// This prevents visual artifacts where the impostor disappears while leaves
// are still dithering in, which is especially visible against the sky.
//
// Blend factor convention: 0 = full geometry, 1 = full impostor

// Trunk blend factor - leads the transition (fades out/in faster)
// Maps [0, 0.6] to [0, 1], clamped
// Result: trunk is fully faded by bf=0.6, giving leaves time to crossfade with impostor
float computeTrunkBlendFactor(float blendFactor) {
    return clamp(blendFactor / 0.6, 0.0, 1.0);
}

// Leaf blend factor - slightly delayed start
// Maps [0.2, 1.0] to [0, 1], clamped
// Result: leaves start fading after trunk has begun, finish at bf=1.0
float computeLeafBlendFactor(float blendFactor) {
    return clamp((blendFactor - 0.2) / 0.8, 0.0, 1.0);
}

// Impostor blend factor - matches leaf timing for true crossfade
// Same mapping as leaves: [0.2, 1.0] to [0, 1]
// Result: impostor fades in sync with leaves for smooth leaf crossfade
float computeImpostorBlendFactor(float blendFactor) {
    return clamp((blendFactor - 0.2) / 0.8, 0.0, 1.0);
}

// Staggered LOD discard for trunk (fade-out: bf 0→1 means fade to invisible)
bool shouldDiscardForLODTrunk(float blendFactor) {
    float trunkBf = computeTrunkBlendFactor(blendFactor);
    return shouldDiscardForLOD(trunkBf);
}

// Staggered LOD discard for leaves (fade-out)
bool shouldDiscardForLODLeaves(float blendFactor) {
    float leafBf = computeLeafBlendFactor(blendFactor);
    return shouldDiscardForLOD(leafBf);
}

// Staggered LOD discard for impostor (fade-in: bf 0→1 means fade to visible)
bool shouldDiscardForLODImpostor(float blendFactor) {
    float impostorBf = computeImpostorBlendFactor(blendFactor);
    return shouldDiscardForLODFadeIn(impostorBf);
}

#endif // DITHER_COMMON_GLSL
