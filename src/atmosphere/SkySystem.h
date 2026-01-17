#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "DescriptorManager.h"
#include "InitContext.h"
#include "interfaces/IRecordable.h"

class AtmosphereLUTSystem;

class SkySystem : public IRecordable {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit SkySystem(ConstructToken) {}

    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        std::string shaderPath;
        uint32_t framesInFlight;
        VkExtent2D extent;
        VkRenderPass hdrRenderPass;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    /**
     * Factory: Create and initialize SkySystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<SkySystem> create(const InitInfo& info);
    static std::unique_ptr<SkySystem> create(const InitContext& ctx, VkRenderPass hdrRenderPass);

    ~SkySystem();

    // Non-copyable, non-movable
    SkySystem(const SkySystem&) = delete;
    SkySystem& operator=(const SkySystem&) = delete;
    SkySystem(SkySystem&&) = delete;
    SkySystem& operator=(SkySystem&&) = delete;

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { extent = newExtent; }

    // Create descriptor sets after uniform buffers and LUTs are ready
    bool createDescriptorSets(const std::vector<VkBuffer>& uniformBuffers,
                              VkDeviceSize uniformBufferSize,
                              AtmosphereLUTSystem& atmosphereLUTSystem);

    // Record sky rendering commands (implements IRecordable)
    void recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) override;


private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createDescriptorSetLayout();
    bool createPipeline();

    VkDevice device = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    VkExtent2D extent = {0, 0};
    VkRenderPass hdrRenderPass = VK_NULL_HANDLE;
    const vk::raii::Device* raiiDevice_ = nullptr;

    std::optional<vk::raii::Pipeline> pipeline_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::vector<VkDescriptorSet> descriptorSets;
};
