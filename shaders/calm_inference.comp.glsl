// CALM batch MLP inference compute shader.
// Evaluates the LLC policy for multiple NPCs in parallel on the GPU.
//
// Architecture:
//   Per NPC:  styleMLP(latent) -> concat(styleEmbed, obs) -> mainMLP -> muHead -> actions
//
// Data layout:
//   - Weights are stored as flat SSBOs, with layer offsets precomputed on CPU
//   - Input: batched latent codes + observations (one row per NPC)
//   - Output: batched action vectors (one row per NPC)
//
// Each workgroup processes one NPC. Shared memory is used for the
// intermediate activations (ping-pong buffers).

#version 450

// Specialization constants for network dimensions
layout(constant_id = 0) const uint NUM_NPCS = 1;
layout(constant_id = 1) const uint LATENT_DIM = 64;
layout(constant_id = 2) const uint OBS_DIM = 102;
layout(constant_id = 3) const uint ACTION_DIM = 37;
layout(constant_id = 4) const uint MAX_HIDDEN = 1024;  // Largest hidden layer size

layout(local_size_x = 1) in;  // One NPC per invocation

// Push constants for layer configuration
layout(push_constant) uniform PushConstants {
    uint numLayers;          // Total number of MLP layers
    uint styleLayerCount;    // Number of style MLP layers
    uint mainLayerCount;     // Number of main MLP layers (including muHead)
    uint styleDim;           // Style embedding dimension (output of style MLP)
} pc;

// Weight storage: all layer weights packed sequentially
// Layout per layer: [inFeatures * outFeatures floats (weights)] [outFeatures floats (bias)]
// Layer metadata stored in layerMeta SSBO
layout(set = 0, binding = 0) readonly buffer WeightBuffer {
    float weights[];
};

// Layer metadata: [weightOffset, biasOffset, inFeatures, outFeatures, activation]
// activation: 0=none, 1=ReLU, 2=Tanh
layout(set = 0, binding = 1) readonly buffer LayerMeta {
    uint layerMeta[];  // 5 uints per layer
};

// Input: latent codes, one per NPC [NUM_NPCS x LATENT_DIM]
layout(set = 0, binding = 2) readonly buffer LatentBuffer {
    float latents[];
};

// Input: observations, one per NPC [NUM_NPCS x OBS_DIM]
layout(set = 0, binding = 3) readonly buffer ObsBuffer {
    float observations[];
};

// Output: actions, one per NPC [NUM_NPCS x ACTION_DIM]
layout(set = 0, binding = 4) writeonly buffer ActionBuffer {
    float actions[];
};

// Shared memory for intermediate activations (ping-pong)
shared float bufA[MAX_HIDDEN];
shared float bufB[MAX_HIDDEN];
shared float styleEmbed[MAX_HIDDEN];

// Read layer metadata
void getLayerMeta(uint layerIdx, out uint weightOff, out uint biasOff,
                  out uint inFeat, out uint outFeat, out uint activation) {
    uint base = layerIdx * 5;
    weightOff  = layerMeta[base + 0];
    biasOff    = layerMeta[base + 1];
    inFeat     = layerMeta[base + 2];
    outFeat    = layerMeta[base + 3];
    activation = layerMeta[base + 4];
}

// Matrix-vector multiply: dest[j] = sum(weights[j*inFeat + k] * src[k]) + bias[j]
// for j in [0, outFeat)
void matVecMul(uint weightOff, uint biasOff, uint inFeat, uint outFeat,
               inout float src[MAX_HIDDEN], inout float dest[MAX_HIDDEN]) {
    for (uint j = 0; j < outFeat; ++j) {
        float sum = weights[biasOff + j];  // bias
        uint rowStart = weightOff + j * inFeat;
        for (uint k = 0; k < inFeat; ++k) {
            sum += weights[rowStart + k] * src[k];
        }
        dest[j] = sum;
    }
}

// Apply activation in-place
void applyActivation(uint activation, uint count, inout float buf[MAX_HIDDEN]) {
    if (activation == 1) {  // ReLU
        for (uint i = 0; i < count; ++i) {
            buf[i] = max(buf[i], 0.0);
        }
    } else if (activation == 2) {  // Tanh
        for (uint i = 0; i < count; ++i) {
            buf[i] = tanh(buf[i]);
        }
    }
}

void main() {
    uint npcIdx = gl_GlobalInvocationID.x;
    if (npcIdx >= NUM_NPCS) return;

    // --- Step 1: Load latent into bufA ---
    uint latentBase = npcIdx * LATENT_DIM;
    for (uint i = 0; i < LATENT_DIM; ++i) {
        bufA[i] = latents[latentBase + i];
    }

    // --- Step 2: Run style MLP (latent -> styleEmbed) ---
    bool useA = true;
    for (uint layer = 0; layer < pc.styleLayerCount; ++layer) {
        uint wOff, bOff, inF, outF, act;
        getLayerMeta(layer, wOff, bOff, inF, outF, act);

        if (useA) {
            matVecMul(wOff, bOff, inF, outF, bufA, bufB);
            applyActivation(act, outF, bufB);
        } else {
            matVecMul(wOff, bOff, inF, outF, bufB, bufA);
            applyActivation(act, outF, bufA);
        }
        useA = !useA;
    }

    // Copy style embedding to styleEmbed buffer
    {
        uint lastStyleLayer = pc.styleLayerCount - 1;
        uint dummy1, dummy2, dummy3, styleDim, dummy4;
        getLayerMeta(lastStyleLayer, dummy1, dummy2, dummy3, styleDim, dummy4);
        float[] src = useA ? bufA : bufB;
        for (uint i = 0; i < styleDim; ++i) {
            styleEmbed[i] = src[i];
        }
    }

    // --- Step 3: Concatenate styleEmbed + observation into bufA ---
    uint obsBase = npcIdx * OBS_DIM;
    for (uint i = 0; i < pc.styleDim; ++i) {
        bufA[i] = styleEmbed[i];
    }
    for (uint i = 0; i < OBS_DIM; ++i) {
        bufA[pc.styleDim + i] = observations[obsBase + i];
    }

    // --- Step 4: Run main MLP + muHead ---
    useA = true;
    for (uint layer = 0; layer < pc.mainLayerCount; ++layer) {
        uint globalLayer = pc.styleLayerCount + layer;
        uint wOff, bOff, inF, outF, act;
        getLayerMeta(globalLayer, wOff, bOff, inF, outF, act);

        if (useA) {
            matVecMul(wOff, bOff, inF, outF, bufA, bufB);
            applyActivation(act, outF, bufB);
        } else {
            matVecMul(wOff, bOff, inF, outF, bufB, bufA);
            applyActivation(act, outF, bufA);
        }
        useA = !useA;
    }

    // --- Step 5: Write output actions ---
    uint actionBase = npcIdx * ACTION_DIM;
    float[] result = useA ? bufA : bufB;
    for (uint i = 0; i < ACTION_DIM; ++i) {
        actions[actionBase + i] = result[i];
    }
}
