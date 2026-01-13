#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>
#include <string>
#include <SDL3/SDL_log.h>
#include "../ShaderLoader.h"

/**
 * ComputePipelineBuilder - Fluent builder for Vulkan compute pipelines
 *
 * Reduces duplication in compute pipeline creation by:
 * - Handling shader loading and cleanup automatically
 * - Providing a fluent API for configuration
 * - Supporting specialization constants
 * - Working with both RAII and raw Vulkan handles
 *
 * Basic usage (shader path):
 *   auto pipeline = ComputePipelineBuilder(raiiDevice)
 *       .setShader(shaderPath + "/my_compute.comp.spv")
 *       .setPipelineLayout(**pipelineLayout)
 *       .build();
 *
 * With specialization constants:
 *   uint32_t workgroupSize = 64;
 *   auto pipeline = ComputePipelineBuilder(raiiDevice)
 *       .setShader(shaderPath + "/my_compute.comp.spv")
 *       .setPipelineLayout(**pipelineLayout)
 *       .addSpecConstant(0, workgroupSize)
 *       .build();
 *
 * Building into member variable:
 *   ComputePipelineBuilder(raiiDevice)
 *       .setShader(shaderPath + "/my_compute.comp.spv")
 *       .setPipelineLayout(**pipelineLayout)
 *       .buildInto(myPipeline_);
 */
class ComputePipelineBuilder {
public:
    // Construct with RAII device (preferred)
    explicit ComputePipelineBuilder(const vk::raii::Device& device)
        : raiiDevice_(&device), rawDevice_(**device) {}

    // Construct with raw device (for gradual migration)
    explicit ComputePipelineBuilder(VkDevice device)
        : raiiDevice_(nullptr), rawDevice_(device) {}

    // Reset builder for reuse
    ComputePipelineBuilder& reset() {
        shaderPath_.clear();
        shaderModule_ = nullptr;
        pipelineLayout_ = nullptr;
        pipelineCache_ = nullptr;
        entryPoint_ = "main";
        specMapEntries_.clear();
        specData_.clear();
        return *this;
    }

    // Set shader from file path (recommended - handles loading and cleanup)
    ComputePipelineBuilder& setShader(const std::string& path) {
        shaderPath_ = path;
        shaderModule_ = nullptr;  // Clear any pre-set module
        return *this;
    }

    // Set pre-loaded shader module (caller is responsible for cleanup)
    ComputePipelineBuilder& setShaderModule(vk::ShaderModule module) {
        shaderModule_ = module;
        shaderPath_.clear();  // Clear path since we're using pre-loaded module
        return *this;
    }

    // Set pipeline layout (required)
    ComputePipelineBuilder& setPipelineLayout(vk::PipelineLayout layout) {
        pipelineLayout_ = layout;
        return *this;
    }

    // Set pipeline cache (optional, for faster creation)
    ComputePipelineBuilder& setPipelineCache(vk::PipelineCache cache) {
        pipelineCache_ = cache;
        return *this;
    }

    // Set entry point name (default: "main")
    ComputePipelineBuilder& setEntryPoint(const char* entryPoint) {
        entryPoint_ = entryPoint;
        return *this;
    }

    // Add a specialization constant by value
    template<typename T>
    ComputePipelineBuilder& addSpecConstant(uint32_t constantId, const T& value) {
        uint32_t offset = static_cast<uint32_t>(specData_.size());
        specData_.resize(specData_.size() + sizeof(T));
        std::memcpy(specData_.data() + offset, &value, sizeof(T));

        specMapEntries_.push_back(vk::SpecializationMapEntry{}
            .setConstantID(constantId)
            .setOffset(offset)
            .setSize(sizeof(T)));

        return *this;
    }

    // Build the pipeline, returning optional<raii::Pipeline>
    // Requires raiiDevice_ to be set (use RAII constructor)
    std::optional<vk::raii::Pipeline> build() {
        if (!raiiDevice_) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ComputePipelineBuilder: RAII device required for build(). Use buildRaw() for VkDevice.");
            return std::nullopt;
        }

        if (!pipelineLayout_) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ComputePipelineBuilder: Pipeline layout not set");
            return std::nullopt;
        }

        // Load shader if path is set
        std::optional<vk::ShaderModule> loadedModule;
        vk::ShaderModule moduleToUse = shaderModule_;

        if (!shaderPath_.empty()) {
            auto result = ShaderLoader::loadShaderModule(rawDevice_, shaderPath_);
            if (!result) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "ComputePipelineBuilder: Failed to load shader: %s", shaderPath_.c_str());
                return std::nullopt;
            }
            loadedModule = *result;
            moduleToUse = *loadedModule;
        }

        if (!moduleToUse) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ComputePipelineBuilder: No shader module set");
            return std::nullopt;
        }

        // Build specialization info if we have spec constants
        vk::SpecializationInfo specInfo;
        if (!specMapEntries_.empty()) {
            specInfo
                .setMapEntries(specMapEntries_)
                .setDataSize(specData_.size())
                .setPData(specData_.data());
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(moduleToUse)
            .setPName(entryPoint_);

        if (!specMapEntries_.empty()) {
            stageInfo.setPSpecializationInfo(&specInfo);
        }

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(pipelineLayout_);

        std::optional<vk::raii::Pipeline> result;
        try {
            result = raiiDevice_->createComputePipeline(pipelineCache_, pipelineInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ComputePipelineBuilder: Failed to create pipeline: %s", e.what());
        }

        // Cleanup loaded shader module
        if (loadedModule) {
            vk::Device(rawDevice_).destroyShaderModule(*loadedModule);
        }

        return result;
    }

    // Build into an optional member variable
    bool buildInto(std::optional<vk::raii::Pipeline>& outPipeline) {
        auto result = build();
        if (result) {
            outPipeline = std::move(result);
            return true;
        }
        return false;
    }

    // Build returning raw VkPipeline (for use with raw VkDevice)
    // Caller owns the returned pipeline handle
    bool buildRaw(VkPipeline& outPipeline) {
        if (!pipelineLayout_) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ComputePipelineBuilder: Pipeline layout not set");
            return false;
        }

        // Load shader if path is set
        std::optional<vk::ShaderModule> loadedModule;
        vk::ShaderModule moduleToUse = shaderModule_;

        if (!shaderPath_.empty()) {
            auto result = ShaderLoader::loadShaderModule(rawDevice_, shaderPath_);
            if (!result) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "ComputePipelineBuilder: Failed to load shader: %s", shaderPath_.c_str());
                return false;
            }
            loadedModule = *result;
            moduleToUse = *loadedModule;
        }

        if (!moduleToUse) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ComputePipelineBuilder: No shader module set");
            return false;
        }

        // Build specialization info if we have spec constants
        vk::SpecializationInfo specInfo;
        if (!specMapEntries_.empty()) {
            specInfo
                .setMapEntries(specMapEntries_)
                .setDataSize(specData_.size())
                .setPData(specData_.data());
        }

        auto stageInfo = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eCompute)
            .setModule(moduleToUse)
            .setPName(entryPoint_);

        if (!specMapEntries_.empty()) {
            stageInfo.setPSpecializationInfo(&specInfo);
        }

        auto pipelineInfo = vk::ComputePipelineCreateInfo{}
            .setStage(stageInfo)
            .setLayout(pipelineLayout_);

        VkResult vkResult = vkCreateComputePipelines(
            rawDevice_,
            static_cast<VkPipelineCache>(pipelineCache_),
            1,
            reinterpret_cast<const VkComputePipelineCreateInfo*>(&pipelineInfo),
            nullptr,
            &outPipeline);

        // Cleanup loaded shader module
        if (loadedModule) {
            vk::Device(rawDevice_).destroyShaderModule(*loadedModule);
        }

        if (vkResult != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ComputePipelineBuilder: Failed to create pipeline (VkResult: %d)", vkResult);
            return false;
        }

        return true;
    }

    // Build raw pipeline and wrap in RAII (for mixed codebases)
    bool buildRawIntoRaii(std::optional<vk::raii::Pipeline>& outPipeline) {
        if (!raiiDevice_) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "ComputePipelineBuilder: RAII device required for buildRawIntoRaii()");
            return false;
        }

        VkPipeline rawPipeline = VK_NULL_HANDLE;
        if (!buildRaw(rawPipeline)) {
            return false;
        }

        outPipeline.emplace(*raiiDevice_, rawPipeline);
        return true;
    }

private:
    const vk::raii::Device* raiiDevice_ = nullptr;
    VkDevice rawDevice_ = VK_NULL_HANDLE;

    std::string shaderPath_;
    vk::ShaderModule shaderModule_ = nullptr;
    vk::PipelineLayout pipelineLayout_ = nullptr;
    vk::PipelineCache pipelineCache_ = nullptr;
    const char* entryPoint_ = "main";

    std::vector<vk::SpecializationMapEntry> specMapEntries_;
    std::vector<char> specData_;
};
