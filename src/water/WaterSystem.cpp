#include "WaterSystem.h"
#include "ShadowSystem.h"
#include "GraphicsPipelineFactory.h"
#include "VulkanResourceFactory.h"
#include "DescriptorManager.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstring>

std::unique_ptr<WaterSystem> WaterSystem::create(const InitInfo& info) {
    std::unique_ptr<WaterSystem> system(new WaterSystem());
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

WaterSystem::~WaterSystem() {
    cleanup();
}

bool WaterSystem::initInternal(const InitInfo& info) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    hdrRenderPass = info.hdrRenderPass;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    extent = info.extent;
    commandPool = info.commandPool;
    graphicsQueue = info.graphicsQueue;
    waterSize = info.waterSize;
    assetPath = info.assetPath;

    // Initialize default water parameters - English estuary/coastal style
    waterUniforms.waterColor = glm::vec4(0.15f, 0.22f, 0.25f, 0.9f);  // Grey-green estuary color
    waterUniforms.waveParams = glm::vec4(0.3f, 15.0f, 0.25f, 0.5f);   // amplitude, wavelength, steepness, speed (English Channel swell)
    waterUniforms.waveParams2 = glm::vec4(0.15f, 5.0f, 0.35f, 0.8f);  // Secondary wave (medium chop)
    waterUniforms.waterExtent = glm::vec4(0.0f, 0.0f, 100.0f, 100.0f);  // position, size
    waterUniforms.waterLevel = 0.0f;
    waterUniforms.foamThreshold = 0.25f;  // Higher threshold for realistic whitecaps
    waterUniforms.fresnelPower = 5.0f;
    waterUniforms.terrainSize = 16384.0f;      // Default terrain size
    waterUniforms.terrainHeightScale = 235.0f; // Default height scale (maxAlt - minAlt = 220 - (-15))
    waterUniforms.shoreBlendDistance = 8.0f;   // 8m shore blend (wider for muddy estuaries)
    waterUniforms.shoreFoamWidth = 15.0f;      // 15m shore foam band (much wider)
    waterUniforms.flowStrength = 1.0f;         // 1m UV offset per flow cycle
    waterUniforms.flowSpeed = 0.5f;            // Flow animation speed
    waterUniforms.flowFoamStrength = 0.5f;     // Flow-based foam intensity
    waterUniforms.fbmNearDistance = 50.0f;     // Max detail within 50m
    waterUniforms.fbmFarDistance = 500.0f;     // Min detail beyond 500m

    // PBR Scattering defaults (English estuary - murkier than ocean)
    // Higher turbidity for sediment-laden coastal waters
    waterUniforms.scatteringCoeffs = glm::vec4(0.6f, 0.15f, 0.05f, 0.3f); // absorption RGB + turbidity (murky)
    waterUniforms.specularRoughness = 0.05f;   // Water is quite smooth
    waterUniforms.absorptionScale = 0.15f;     // Depth-based absorption rate
    waterUniforms.scatteringScale = 1.0f;      // Turbidity multiplier
    waterUniforms.displacementScale = 1.0f;   // Interactive displacement scale (Phase 4)
    waterUniforms.sssIntensity = 1.5f;        // Subsurface scattering intensity (Phase 17)
    waterUniforms.causticsScale = 0.1f;       // Caustics pattern scale (Phase 9)
    waterUniforms.causticsSpeed = 0.8f;       // Caustics animation speed (Phase 9)
    waterUniforms.causticsIntensity = 0.5f;   // Caustics brightness (Phase 9)
    waterUniforms.nearPlane = 0.1f;           // Default camera near plane
    waterUniforms.farPlane = 50000.0f;        // Default camera far plane (matches Camera.cpp)
    waterUniforms.padding1 = 0.0f;
    waterUniforms.padding2 = 0.0f;

    // Phase 12: Material blending defaults
    // Secondary material defaults to same as primary (no blending)
    waterUniforms.waterColor2 = waterUniforms.waterColor;
    waterUniforms.scatteringCoeffs2 = waterUniforms.scatteringCoeffs;
    waterUniforms.absorptionScale2 = waterUniforms.absorptionScale;
    waterUniforms.scatteringScale2 = waterUniforms.scatteringScale;
    waterUniforms.specularRoughness2 = waterUniforms.specularRoughness;
    waterUniforms.sssIntensity2 = waterUniforms.sssIntensity;
    waterUniforms.blendCenter = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    waterUniforms.blendDistance = 50.0f;  // Default 50m blend distance
    waterUniforms.blendMode = 0;          // Distance mode

    if (!createDescriptorSetLayout()) return false;
    if (!createPipeline()) return false;
    if (!createWaterMesh()) return false;
    if (!createUniformBuffers()) return false;
    if (!loadFoamTexture()) return false;
    if (!loadCausticsTexture()) return false;

    // Initialize FFT Ocean simulation
    OceanFFT::OceanParams oceanParams;
    oceanParams.resolution = 256;
    oceanParams.oceanSize = 256.0f;
    oceanParams.windSpeed = 12.0f;                       // ~25 knots moderate wind
    oceanParams.windDirection = glm::vec2(0.8f, 0.6f);
    oceanParams.amplitude = 0.001f;                      // Phillips spectrum A constant
    oceanParams.choppiness = 1.3f;                       // Horizontal displacement scale
    oceanParams.heightScale = 40.0f;                     // Scale to meters (gives ~1-3m waves)

    OceanFFT::InitInfo oceanInfo{};
    oceanInfo.device = device;
    oceanInfo.physicalDevice = physicalDevice;
    oceanInfo.allocator = allocator;
    oceanInfo.commandPool = commandPool;
    oceanInfo.computeQueue = graphicsQueue;  // Use graphics queue for compute
    oceanInfo.shaderPath = shaderPath;
    oceanInfo.framesInFlight = framesInFlight;
    oceanInfo.params = oceanParams;
    oceanInfo.useCascades = true;

    oceanFFT_ = OceanFFT::create(oceanInfo);
    if (!oceanFFT_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "WaterSystem: Failed to create OceanFFT");
        return false;
    }

    // Set FFT ocean mode by default
    setUseFFTOcean(true, oceanParams.oceanSize, oceanParams.oceanSize / 4.0f, oceanParams.oceanSize / 16.0f);

    return true;
}

void WaterSystem::cleanup() {
    if (device == VK_NULL_HANDLE) return;  // Not initialized

    // Destroy RAII-managed resources
    oceanFFT_.reset();
    foamTexture.reset();
    causticsTexture.reset();
    waterMesh.reset();

    // Destroy uniform buffers (RAII-managed)
    waterUniformBuffers_.clear();
    waterUniformMapped.clear();

    // RAII wrappers handle cleanup automatically - just reset them
    pipeline = ManagedPipeline();
    tessellationPipeline = ManagedPipeline();
    pipelineLayout = ManagedPipelineLayout();
    descriptorSetLayout = ManagedDescriptorSetLayout();
    descriptorSets.clear();
}

bool WaterSystem::createDescriptorSetLayout() {
    // Water shader bindings:
    // 0: Main UBO (scene uniforms)
    // 1: Water uniforms
    // 2: Shadow map array (for shadow sampling)
    // 3: Terrain heightmap (for shore detection)
    // 4: Flow map (for water flow direction and speed)
    // 5: Displacement map (for interactive splashes)
    // 6: Foam noise texture (tileable Worley noise)
    // 7: Temporal foam buffer (Phase 14: persistent foam)
    // 8: Caustics texture (Phase 9: animated underwater light patterns)
    // 9: SSR texture (Phase 10: screen-space reflections)
    // 10: Scene depth texture (Phase 11: dual depth for refraction)
    // 11-13: FFT Ocean cascade 0 (large swells, 256m)
    // 14: Tile array (high-res terrain tiles near camera)
    // 15: Tile info SSBO
    // 16-18: FFT Ocean cascade 1 (medium waves, 64m)
    // 19-21: FFT Ocean cascade 2 (small ripples, 16m)
    // 22: Environment cubemap (Phase 2: SSR fallback)

    // Stage flags for vertex + tessellation evaluation (both can sample ocean textures)
    constexpr VkShaderStageFlags VERTEX_TESS = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    // Stage flags for all stages that need UBO access
    constexpr VkShaderStageFlags ALL_STAGES = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                               VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;

    VkDescriptorSetLayout rawLayout = DescriptorManager::LayoutBuilder(device)
        .addUniformBuffer(ALL_STAGES)                           // 0: Main UBO (used by all stages)
        .addUniformBuffer(VERTEX_TESS | VK_SHADER_STAGE_FRAGMENT_BIT)  // 1: Water uniforms
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 2: Shadow map
        .addCombinedImageSampler(VERTEX_TESS | VK_SHADER_STAGE_FRAGMENT_BIT)  // 3: Terrain heightmap (breaking wave detection)
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 4: Flow map
        .addCombinedImageSampler(VERTEX_TESS)                   // 5: Displacement map (interactive splashes)
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 6: Foam texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 7: Temporal foam
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 8: Caustics texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 9: SSR texture
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 10: Scene depth
        .addCombinedImageSampler(VERTEX_TESS)                   // 11: Ocean displacement (cascade 0)
        .addCombinedImageSampler(VERTEX_TESS)                   // 12: Ocean normal (cascade 0)
        .addCombinedImageSampler(VERTEX_TESS)                   // 13: Ocean foam (cascade 0)
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 14: Tile array
        .addStorageBuffer(VK_SHADER_STAGE_FRAGMENT_BIT)         // 15: Tile info SSBO
        .addCombinedImageSampler(VERTEX_TESS)                   // 16: Ocean displacement (cascade 1)
        .addCombinedImageSampler(VERTEX_TESS)                   // 17: Ocean normal (cascade 1)
        .addCombinedImageSampler(VERTEX_TESS)                   // 18: Ocean foam (cascade 1)
        .addCombinedImageSampler(VERTEX_TESS)                   // 19: Ocean displacement (cascade 2)
        .addCombinedImageSampler(VERTEX_TESS)                   // 20: Ocean normal (cascade 2)
        .addCombinedImageSampler(VERTEX_TESS)                   // 21: Ocean foam (cascade 2)
        .addCombinedImageSampler(VK_SHADER_STAGE_FRAGMENT_BIT)  // 22: Environment cubemap
        .build();

    if (rawLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create water descriptor set layout");
        return false;
    }
    descriptorSetLayout = ManagedDescriptorSetLayout::fromRaw(device, rawLayout);

    // Create pipeline layout with push constants for model matrix + FFT params
    // Push constants are used by vertex shader (non-tessellated) and tessellation evaluation shader (tessellated)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                   VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    VkPipelineLayout rawPipelineLayout = DescriptorManager::createPipelineLayout(device, descriptorSetLayout.get(), {pushConstantRange});
    if (rawPipelineLayout == VK_NULL_HANDLE) {
        SDL_Log("Failed to create water pipeline layout");
        return false;
    }
    pipelineLayout = ManagedPipelineLayout::fromRaw(device, rawPipelineLayout);

    return true;
}

bool WaterSystem::createPipeline() {
    GraphicsPipelineFactory factory(device);

    // Get vertex input from Vertex struct
    auto bindingDesc = Vertex::getBindingDescription();
    auto attrDescs = Vertex::getAttributeDescriptions();

    std::vector<VkVertexInputBindingDescription> bindings = {bindingDesc};
    std::vector<VkVertexInputAttributeDescription> attributes(attrDescs.begin(), attrDescs.end());

    // Water pipeline: alpha blending, depth test but no depth write (for transparency)
    // Depth bias prevents z-fighting flickering at water/terrain intersection
    VkPipeline rawPipeline = VK_NULL_HANDLE;
    bool success = factory
        .setShaders(shaderPath + "/water.vert.spv", shaderPath + "/water.frag.spv")
        .setRenderPass(hdrRenderPass)
        .setPipelineLayout(pipelineLayout.get())
        .setExtent(extent)
        .setDynamicViewport(true)
        .setVertexInput(bindings, attributes)
        .setDepthTest(true)
        .setDepthWrite(false)  // Don't write depth for transparent water
        .setDepthBias(1.0f, 1.5f)  // Bias water slightly away from camera to prevent z-fighting
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Alpha)
        .setCullMode(VK_CULL_MODE_NONE)  // Render both sides of water
        .build(rawPipeline);

    if (!success) {
        SDL_Log("Failed to create water pipeline");
        return false;
    }

    pipeline = ManagedPipeline::fromRaw(device, rawPipeline);

    // Create tessellation pipeline for GPU wave geometry detail
    // This is optional - if it fails, we fall back to the regular pipeline
    factory.reset();
    VkPipeline rawTessPipeline = VK_NULL_HANDLE;
    bool tessSuccess = factory
        .setShaders(shaderPath + "/water_tess.vert.spv", shaderPath + "/water.frag.spv")
        .setTessellationShaders(shaderPath + "/water.tesc.spv", shaderPath + "/water.tese.spv")
        .setRenderPass(hdrRenderPass)
        .setPipelineLayout(pipelineLayout.get())
        .setExtent(extent)
        .setDynamicViewport(true)
        .setVertexInput(bindings, attributes)
        .setDepthTest(true)
        .setDepthWrite(false)
        .setDepthBias(1.0f, 1.5f)
        .setBlendMode(GraphicsPipelineFactory::BlendMode::Alpha)
        .setCullMode(VK_CULL_MODE_NONE)
        .build(rawTessPipeline);

    if (tessSuccess) {
        tessellationPipeline = ManagedPipeline::fromRaw(device, rawTessPipeline);
        SDL_Log("Water tessellation pipeline created successfully");
    } else {
        SDL_Log("Water tessellation pipeline creation failed - tessellation disabled");
        // Continue without tessellation - not a fatal error
    }

    return true;
}

bool WaterSystem::createWaterMesh() {
    // Create a subdivided plane for the water surface
    // More subdivisions = smoother wave animation
    // Scale grid resolution based on water size for better wave detail
    // For large ocean planes, we need more vertices but there's a limit
    int gridSize = 64;
    if (waterSize > 1000.0f) gridSize = 256;
    if (waterSize > 20000.0f) gridSize = 512;  // For horizon extension
    const float size = waterSize;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Generate vertices
    for (int z = 0; z <= gridSize; z++) {
        for (int x = 0; x <= gridSize; x++) {
            Vertex v;
            float u = static_cast<float>(x) / gridSize;
            float v_coord = static_cast<float>(z) / gridSize;

            v.position = glm::vec3(
                (u - 0.5f) * size,
                0.0f,
                (v_coord - 0.5f) * size
            );
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.texCoord = glm::vec2(u, v_coord);
            v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            v.color = glm::vec4(1.0f);

            vertices.push_back(v);
        }
    }

    // Generate indices
    for (int z = 0; z < gridSize; z++) {
        for (int x = 0; x < gridSize; x++) {
            uint32_t topLeft = z * (gridSize + 1) + x;
            uint32_t topRight = topLeft + 1;
            uint32_t bottomLeft = (z + 1) * (gridSize + 1) + x;
            uint32_t bottomRight = bottomLeft + 1;

            // First triangle
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Second triangle
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    waterMesh = RAIIAdapter<Mesh>::create(
        [&](auto& m) {
            m.setCustomGeometry(vertices, indices);
            m.upload(allocator, device, commandPool, graphicsQueue);
            return true;
        },
        [this](auto& m) { m.destroy(allocator); }
    );

    if (!waterMesh) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create water mesh");
        return false;
    }

    SDL_Log("Water mesh created with %zu vertices, %zu indices",
            vertices.size(), indices.size());

    return true;
}

bool WaterSystem::createUniformBuffers() {
    waterUniformBuffers_.resize(framesInFlight);
    waterUniformMapped.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; i++) {
        if (!VulkanResourceFactory::createUniformBuffer(allocator, sizeof(WaterUniforms), waterUniformBuffers_[i])) {
            SDL_Log("Failed to create water uniform buffer %u", i);
            return false;
        }
        waterUniformMapped[i] = waterUniformBuffers_[i].map();
    }

    return true;
}

bool WaterSystem::loadFoamTexture() {
    std::string foamPath = assetPath + "/textures/foam_noise.png";

    // Try to load the foam texture, fall back to white if not found
    foamTexture = RAIIAdapter<Texture>::create(
        [&](auto& t) {
            if (!t.load(foamPath, allocator, device, commandPool, graphicsQueue, physicalDevice, false)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Foam texture not found at %s, creating fallback white texture", foamPath.c_str());
                if (!t.createSolidColor(255, 255, 255, 255, allocator, device, commandPool, graphicsQueue)) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create fallback foam texture");
                    return false;
                }
            } else {
                SDL_Log("Loaded foam texture from %s", foamPath.c_str());
            }
            return true;
        },
        [this](auto& t) { t.destroy(allocator, device); }
    );

    return foamTexture.has_value();
}

bool WaterSystem::loadCausticsTexture() {
    std::string causticsPath = assetPath + "/textures/caustics.png";

    // Try to load the caustics texture, fall back to white if not found
    causticsTexture = RAIIAdapter<Texture>::create(
        [&](auto& t) {
            if (!t.load(causticsPath, allocator, device, commandPool, graphicsQueue, physicalDevice, false)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Caustics texture not found at %s, creating fallback white texture", causticsPath.c_str());
                if (!t.createSolidColor(255, 255, 255, 255, allocator, device, commandPool, graphicsQueue)) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create fallback caustics texture");
                    return false;
                }
            } else {
                SDL_Log("Loaded caustics texture from %s", causticsPath.c_str());
            }
            return true;
        },
        [this](auto& t) { t.destroy(allocator, device); }
    );

    return causticsTexture.has_value();
}

bool WaterSystem::createDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                                        VkDeviceSize uniformBufferSize,
                                        ShadowSystem& shadowSystem,
                                        VkImageView terrainHeightMapView,
                                        VkSampler terrainHeightMapSampler,
                                        VkImageView flowMapView,
                                        VkSampler flowMapSampler,
                                        VkImageView displacementMapView,
                                        VkSampler displacementMapSampler,
                                        VkImageView temporalFoamView,
                                        VkSampler temporalFoamSampler,
                                        VkImageView ssrView,
                                        VkSampler ssrSampler,
                                        VkImageView sceneDepthView,
                                        VkSampler sceneDepthSampler,
                                        VkImageView tileArrayView,
                                        VkSampler tileSampler,
                                        const std::array<VkBuffer, 3>& tileInfoBuffers,
                                        VkImageView envCubemapView,
                                        VkSampler envCubemapSampler) {
    // Store tile info buffers for per-frame updates (triple-buffered)
    tileInfoBuffers_ = tileInfoBuffers;

    // Allocate descriptor sets using managed pool
    descriptorSets = descriptorPool->allocate(descriptorSetLayout.get(), framesInFlight);
    if (descriptorSets.size() != framesInFlight) {
        SDL_Log("Failed to allocate water descriptor sets");
        return false;
    }

    // Get shadow resources
    VkImageView shadowView = shadowSystem.getShadowImageView();
    VkSampler shadowSampler = shadowSystem.getShadowSampler();

    // Update each descriptor set - use non-fluent pattern to avoid copy semantics bug
    // Note: tile info buffer (binding 15) is updated per-frame in recordDraw
    for (size_t i = 0; i < framesInFlight; i++) {
        DescriptorManager::SetWriter writer(device, descriptorSets[i]);
        writer.writeBuffer(0, uniformBuffers[i], 0, uniformBufferSize);
        writer.writeBuffer(1, waterUniformBuffers_[i].get(), 0, sizeof(WaterUniforms));
        writer.writeImage(2, shadowView, shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        writer.writeImage(3, terrainHeightMapView, terrainHeightMapSampler);
        writer.writeImage(4, flowMapView, flowMapSampler);
        writer.writeImage(5, displacementMapView, displacementMapSampler);
        writer.writeImage(6, (*foamTexture)->getImageView(), (*foamTexture)->getSampler());
        writer.writeImage(7, temporalFoamView, temporalFoamSampler);
        writer.writeImage(8, (*causticsTexture)->getImageView(), (*causticsTexture)->getSampler());
        writer.writeImage(9, ssrView, ssrSampler, VK_IMAGE_LAYOUT_GENERAL);
        writer.writeImage(10, sceneDepthView, sceneDepthSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

        // FFT Ocean cascade 0 (bindings 11-13)
        if (oceanFFT_ && oceanFFT_->getDisplacementView(0) != VK_NULL_HANDLE) {
            VkSampler oceanSampler = oceanFFT_->getSampler();
            writer.writeImage(11, oceanFFT_->getDisplacementView(0), oceanSampler);
            writer.writeImage(12, oceanFFT_->getNormalView(0), oceanSampler);
            writer.writeImage(13, oceanFFT_->getFoamView(0), oceanSampler);
        } else {
            // Fallback to placeholders if OceanFFT not ready
            writer.writeImage(11, displacementMapView, displacementMapSampler);
            writer.writeImage(12, displacementMapView, displacementMapSampler);
            writer.writeImage(13, displacementMapView, displacementMapSampler);
        }

        // Tile cache bindings (14 and 15) - for high-res terrain sampling
        if (tileArrayView != VK_NULL_HANDLE && tileSampler != VK_NULL_HANDLE) {
            writer.writeImage(14, tileArrayView, tileSampler);
        }
        // Write initial tile info buffer (frame 0) - will be updated per-frame
        if (tileInfoBuffers_[0] != VK_NULL_HANDLE) {
            writer.writeBuffer(15, tileInfoBuffers_[0], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }

        // FFT Ocean cascade 1 and 2 (bindings 16-21)
        if (oceanFFT_ && oceanFFT_->getCascadeCount() >= 2) {
            VkSampler oceanSampler = oceanFFT_->getSampler();
            writer.writeImage(16, oceanFFT_->getDisplacementView(1), oceanSampler);
            writer.writeImage(17, oceanFFT_->getNormalView(1), oceanSampler);
            writer.writeImage(18, oceanFFT_->getFoamView(1), oceanSampler);
        } else {
            writer.writeImage(16, displacementMapView, displacementMapSampler);
            writer.writeImage(17, displacementMapView, displacementMapSampler);
            writer.writeImage(18, displacementMapView, displacementMapSampler);
        }

        if (oceanFFT_ && oceanFFT_->getCascadeCount() >= 3) {
            VkSampler oceanSampler = oceanFFT_->getSampler();
            writer.writeImage(19, oceanFFT_->getDisplacementView(2), oceanSampler);
            writer.writeImage(20, oceanFFT_->getNormalView(2), oceanSampler);
            writer.writeImage(21, oceanFFT_->getFoamView(2), oceanSampler);
        } else {
            writer.writeImage(19, displacementMapView, displacementMapSampler);
            writer.writeImage(20, displacementMapView, displacementMapSampler);
            writer.writeImage(21, displacementMapView, displacementMapSampler);
        }

        // Environment cubemap (binding 22) - Phase 2 SSR fallback
        if (envCubemapView != VK_NULL_HANDLE && envCubemapSampler != VK_NULL_HANDLE) {
            writer.writeImage(22, envCubemapView, envCubemapSampler);
        } else {
            // Use displacement map as placeholder (will fall back to procedural sky in shader)
            writer.writeImage(22, displacementMapView, displacementMapSampler);
        }

        writer.update();
    }

    SDL_Log("Water descriptor sets created with terrain heightmap, flow map, displacement map, foam texture, temporal foam, caustics, SSR, scene depth, tile cache, FFT cascades, and environment cubemap");
    return true;
}

void WaterSystem::updateUniforms(uint32_t frameIndex) {
    // Copy water uniforms to mapped buffer
    std::memcpy(waterUniformMapped[frameIndex], &waterUniforms, sizeof(WaterUniforms));
}

void WaterSystem::updateOceanFFT(VkCommandBuffer cmd, uint32_t frameIndex, float time) {
    if (oceanFFT_ && pushConstants.useFFTOcean) {
        oceanFFT_->update(cmd, frameIndex, time);
    }
}

void WaterSystem::setWaterExtent(const glm::vec2& position, const glm::vec2& size) {
    waterUniforms.waterExtent = glm::vec4(position.x, position.y, size.x, size.y);

    // Update model matrix to position the water plane
    waterModelMatrix = glm::mat4(1.0f);
    waterModelMatrix = glm::translate(waterModelMatrix,
                                       glm::vec3(position.x, waterUniforms.waterLevel, position.y));
}

void WaterSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Update tile info buffer binding to the correct frame's buffer (triple-buffered to avoid CPU-GPU sync)
    if (tileInfoBuffers_[frameIndex % 3] != VK_NULL_HANDLE) {
        DescriptorManager::SetWriter(device, descriptorSets[frameIndex])
            .writeBuffer(15, tileInfoBuffers_[frameIndex % 3], 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .update();
    }

    // Use tessellation pipeline if enabled and available
    bool useTess = useTessellation_ && isTessellationSupported();
    VkPipeline activePipeline = useTess ? tessellationPipeline.get() : pipeline.get();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);

    // Set dynamic viewport and scissor to handle window resize
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout.get(), 0, 1, &descriptorSets[frameIndex], 0, nullptr);

    // Set up push constants
    pushConstants.model = glm::mat4(1.0f);
    pushConstants.model = glm::translate(pushConstants.model, glm::vec3(
        waterUniforms.waterExtent.x,
        waterUniforms.waterLevel,
        waterUniforms.waterExtent.y
    ));
    // useFFTOcean and oceanSize values are set via setUseFFTOcean()
    // Push constants are used by vertex shader (non-tess) or both vertex + tess eval shaders
    vkCmdPushConstants(cmd, pipelineLayout.get(), VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(PushConstants), &pushConstants);

    // Bind water mesh and draw
    VkBuffer vertexBuffers[] = {(*waterMesh)->getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, (*waterMesh)->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, (*waterMesh)->getIndexCount(), 1, 0, 0, 0);
}

void WaterSystem::recordMeshDraw(VkCommandBuffer cmd) {
    // Draw just the mesh (pipeline and descriptors bound externally)
    VkBuffer vertexBuffers[] = {(*waterMesh)->getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, (*waterMesh)->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, (*waterMesh)->getIndexCount(), 1, 0, 0, 0);
}

void WaterSystem::updateTide(float tideHeight) {
    // tideHeight is normalized -1 to +1 from CelestialCalculator::calculateTide()
    // Scale by tidalRange and add to base water level
    waterUniforms.waterLevel = baseWaterLevel + (tideHeight * tidalRange);
}

void WaterSystem::setWaterType(WaterType type) {
    // Water type presets based on real-world optical properties
    // Absorption coefficients: how quickly each wavelength is absorbed (higher = faster absorption)
    // Real water absorbs red fastest, then green, then blue
    // Turbidity: amount of suspended particles causing scattering

    switch (type) {
        case WaterType::Ocean:
            // Deep ocean: very clear, strong blue tint
            waterUniforms.scatteringCoeffs = glm::vec4(0.45f, 0.09f, 0.02f, 0.05f);
            waterUniforms.waterColor = glm::vec4(0.01f, 0.03f, 0.08f, 0.95f);
            waterUniforms.absorptionScale = 0.12f;
            waterUniforms.scatteringScale = 0.8f;
            break;

        case WaterType::CoastalOcean:
            // Coastal waters: more sediment, blue-green
            waterUniforms.scatteringCoeffs = glm::vec4(0.35f, 0.12f, 0.05f, 0.15f);
            waterUniforms.waterColor = glm::vec4(0.02f, 0.06f, 0.10f, 0.92f);
            waterUniforms.absorptionScale = 0.18f;
            waterUniforms.scatteringScale = 1.2f;
            break;

        case WaterType::River:
            // River: green-brown tint, moderate turbidity
            waterUniforms.scatteringCoeffs = glm::vec4(0.25f, 0.18f, 0.12f, 0.25f);
            waterUniforms.waterColor = glm::vec4(0.04f, 0.08f, 0.06f, 0.90f);
            waterUniforms.absorptionScale = 0.25f;
            waterUniforms.scatteringScale = 1.5f;
            break;

        case WaterType::MuddyRiver:
            // Muddy river: brown, high turbidity
            waterUniforms.scatteringCoeffs = glm::vec4(0.15f, 0.20f, 0.25f, 0.6f);
            waterUniforms.waterColor = glm::vec4(0.12f, 0.10f, 0.06f, 0.85f);
            waterUniforms.absorptionScale = 0.4f;
            waterUniforms.scatteringScale = 2.5f;
            break;

        case WaterType::ClearStream:
            // Mountain stream: extremely clear
            waterUniforms.scatteringCoeffs = glm::vec4(0.50f, 0.08f, 0.01f, 0.02f);
            waterUniforms.waterColor = glm::vec4(0.01f, 0.04f, 0.08f, 0.98f);
            waterUniforms.absorptionScale = 0.08f;
            waterUniforms.scatteringScale = 0.5f;
            break;

        case WaterType::Lake:
            // Lake: dark blue-green, moderate clarity
            waterUniforms.scatteringCoeffs = glm::vec4(0.35f, 0.15f, 0.08f, 0.12f);
            waterUniforms.waterColor = glm::vec4(0.02f, 0.05f, 0.08f, 0.93f);
            waterUniforms.absorptionScale = 0.20f;
            waterUniforms.scatteringScale = 1.0f;
            break;

        case WaterType::Swamp:
            // Swamp: dark green-brown, very turbid
            waterUniforms.scatteringCoeffs = glm::vec4(0.10f, 0.15f, 0.20f, 0.8f);
            waterUniforms.waterColor = glm::vec4(0.08f, 0.10f, 0.04f, 0.80f);
            waterUniforms.absorptionScale = 0.5f;
            waterUniforms.scatteringScale = 3.0f;
            break;

        case WaterType::Tropical:
            // Tropical: bright turquoise, very clear
            waterUniforms.scatteringCoeffs = glm::vec4(0.55f, 0.06f, 0.03f, 0.03f);
            waterUniforms.waterColor = glm::vec4(0.0f, 0.08f, 0.12f, 0.97f);
            waterUniforms.absorptionScale = 0.06f;
            waterUniforms.scatteringScale = 0.4f;
            break;
    }

    SDL_Log("Water type set with absorption (%.2f, %.2f, %.2f), turbidity %.2f",
            waterUniforms.scatteringCoeffs.r, waterUniforms.scatteringCoeffs.g,
            waterUniforms.scatteringCoeffs.b, waterUniforms.scatteringCoeffs.a);
}

// Phase 12: Material blending implementation

WaterSystem::WaterMaterial WaterSystem::getMaterialPreset(WaterType type) const {
    WaterMaterial material{};

    switch (type) {
        case WaterType::Ocean:
            material.waterColor = glm::vec4(0.01f, 0.03f, 0.08f, 0.95f);
            material.scatteringCoeffs = glm::vec4(0.45f, 0.09f, 0.02f, 0.05f);
            material.absorptionScale = 0.12f;
            material.scatteringScale = 0.8f;
            material.specularRoughness = 0.04f;
            material.sssIntensity = 1.2f;
            break;

        case WaterType::CoastalOcean:
            material.waterColor = glm::vec4(0.02f, 0.06f, 0.10f, 0.92f);
            material.scatteringCoeffs = glm::vec4(0.35f, 0.12f, 0.05f, 0.15f);
            material.absorptionScale = 0.18f;
            material.scatteringScale = 1.2f;
            material.specularRoughness = 0.05f;
            material.sssIntensity = 1.4f;
            break;

        case WaterType::River:
            material.waterColor = glm::vec4(0.04f, 0.08f, 0.06f, 0.90f);
            material.scatteringCoeffs = glm::vec4(0.25f, 0.18f, 0.12f, 0.25f);
            material.absorptionScale = 0.25f;
            material.scatteringScale = 1.5f;
            material.specularRoughness = 0.06f;
            material.sssIntensity = 1.0f;
            break;

        case WaterType::MuddyRiver:
            material.waterColor = glm::vec4(0.12f, 0.10f, 0.06f, 0.85f);
            material.scatteringCoeffs = glm::vec4(0.15f, 0.20f, 0.25f, 0.6f);
            material.absorptionScale = 0.4f;
            material.scatteringScale = 2.5f;
            material.specularRoughness = 0.08f;
            material.sssIntensity = 0.5f;
            break;

        case WaterType::ClearStream:
            material.waterColor = glm::vec4(0.01f, 0.04f, 0.08f, 0.98f);
            material.scatteringCoeffs = glm::vec4(0.50f, 0.08f, 0.01f, 0.02f);
            material.absorptionScale = 0.08f;
            material.scatteringScale = 0.5f;
            material.specularRoughness = 0.03f;
            material.sssIntensity = 2.0f;
            break;

        case WaterType::Lake:
            material.waterColor = glm::vec4(0.02f, 0.05f, 0.08f, 0.93f);
            material.scatteringCoeffs = glm::vec4(0.35f, 0.15f, 0.08f, 0.12f);
            material.absorptionScale = 0.20f;
            material.scatteringScale = 1.0f;
            material.specularRoughness = 0.04f;
            material.sssIntensity = 1.3f;
            break;

        case WaterType::Swamp:
            material.waterColor = glm::vec4(0.08f, 0.10f, 0.04f, 0.80f);
            material.scatteringCoeffs = glm::vec4(0.10f, 0.15f, 0.20f, 0.8f);
            material.absorptionScale = 0.5f;
            material.scatteringScale = 3.0f;
            material.specularRoughness = 0.10f;
            material.sssIntensity = 0.3f;
            break;

        case WaterType::Tropical:
            material.waterColor = glm::vec4(0.0f, 0.08f, 0.12f, 0.97f);
            material.scatteringCoeffs = glm::vec4(0.55f, 0.06f, 0.03f, 0.03f);
            material.absorptionScale = 0.06f;
            material.scatteringScale = 0.4f;
            material.specularRoughness = 0.03f;
            material.sssIntensity = 2.5f;
            break;
    }

    return material;
}

void WaterSystem::setPrimaryMaterial(const WaterMaterial& material) {
    waterUniforms.waterColor = material.waterColor;
    waterUniforms.scatteringCoeffs = material.scatteringCoeffs;
    waterUniforms.absorptionScale = material.absorptionScale;
    waterUniforms.scatteringScale = material.scatteringScale;
    waterUniforms.specularRoughness = material.specularRoughness;
    waterUniforms.sssIntensity = material.sssIntensity;
}

void WaterSystem::setSecondaryMaterial(const WaterMaterial& material) {
    waterUniforms.waterColor2 = material.waterColor;
    waterUniforms.scatteringCoeffs2 = material.scatteringCoeffs;
    waterUniforms.absorptionScale2 = material.absorptionScale;
    waterUniforms.scatteringScale2 = material.scatteringScale;
    waterUniforms.specularRoughness2 = material.specularRoughness;
    waterUniforms.sssIntensity2 = material.sssIntensity;
}

void WaterSystem::setPrimaryMaterial(WaterType type) {
    setPrimaryMaterial(getMaterialPreset(type));
    SDL_Log("Primary water material set to type %d", static_cast<int>(type));
}

void WaterSystem::setSecondaryMaterial(WaterType type) {
    setSecondaryMaterial(getMaterialPreset(type));
    SDL_Log("Secondary water material set to type %d", static_cast<int>(type));
}

void WaterSystem::setupMaterialTransition(WaterType from, WaterType to, const glm::vec2& center,
                                           float distance, BlendMode mode) {
    setPrimaryMaterial(from);
    setSecondaryMaterial(to);
    setBlendCenter(center);
    setBlendDistance(distance);
    setBlendMode(mode);

    SDL_Log("Material transition set up: type %d -> %d at (%.1f, %.1f), distance %.1fm, mode %d",
            static_cast<int>(from), static_cast<int>(to),
            center.x, center.y, distance, static_cast<int>(mode));
}
