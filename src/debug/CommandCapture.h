#pragma once

#include <vulkan/vulkan.hpp>
#include <string>
#include <vector>
#include <chrono>

/**
 * CommandCapture - Records detailed command buffer operations for debugging.
 *
 * Provides frame-by-frame capture of all recorded commands with timing.
 * Can operate in continuous mode (every frame) or single-shot capture.
 *
 * Usage:
 *   // At frame start:
 *   capture.beginFrame();
 *
 *   // When recording commands:
 *   capture.recordDraw("TerrainSystem", drawCount, instanceCount);
 *   capture.recordDispatch("GrassCull", groupsX, groupsY, groupsZ);
 *   capture.recordBarrier("ShadowToRead", srcStage, dstStage);
 *
 *   // At frame end:
 *   capture.endFrame();
 *
 *   // Query results:
 *   const auto& frame = capture.getLastCapture();
 */

enum class CommandType {
    Draw,
    DrawIndexed,
    DrawIndirect,
    DrawIndexedIndirect,
    Dispatch,
    DispatchIndirect,
    BeginRenderPass,
    EndRenderPass,
    BindPipeline,
    BindDescriptorSet,
    PushConstants,
    PipelineBarrier,
    CopyBuffer,
    CopyImage,
    BlitImage,
    ClearImage,
    Other
};

inline const char* commandTypeName(CommandType type) {
    switch (type) {
        case CommandType::Draw: return "Draw";
        case CommandType::DrawIndexed: return "DrawIndexed";
        case CommandType::DrawIndirect: return "DrawIndirect";
        case CommandType::DrawIndexedIndirect: return "DrawIndexedIndirect";
        case CommandType::Dispatch: return "Dispatch";
        case CommandType::DispatchIndirect: return "DispatchIndirect";
        case CommandType::BeginRenderPass: return "BeginRenderPass";
        case CommandType::EndRenderPass: return "EndRenderPass";
        case CommandType::BindPipeline: return "BindPipeline";
        case CommandType::BindDescriptorSet: return "BindDescriptorSet";
        case CommandType::PushConstants: return "PushConstants";
        case CommandType::PipelineBarrier: return "PipelineBarrier";
        case CommandType::CopyBuffer: return "CopyBuffer";
        case CommandType::CopyImage: return "CopyImage";
        case CommandType::BlitImage: return "BlitImage";
        case CommandType::ClearImage: return "ClearImage";
        case CommandType::Other: return "Other";
    }
    return "Unknown";
}

struct CapturedCommand {
    CommandType type;
    std::string source;           // Which system recorded this (e.g., "TerrainSystem")
    std::string details;          // Additional info (e.g., "vertices=1024, instances=50")
    float timestampMs = 0.0f;     // Time since frame start when recorded

    // For draw commands
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t indexCount = 0;

    // For dispatch commands
    uint32_t groupCountX = 0;
    uint32_t groupCountY = 0;
    uint32_t groupCountZ = 0;

    // For pipeline binds
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
};

struct CapturedFrame {
    uint64_t frameNumber = 0;
    float totalTimeMs = 0.0f;
    std::vector<CapturedCommand> commands;

    // Summary stats
    uint32_t drawCount = 0;
    uint32_t dispatchCount = 0;
    uint32_t renderPassCount = 0;
    uint32_t pipelineBindCount = 0;
    uint32_t descriptorBindCount = 0;
    uint32_t barrierCount = 0;

    void clear() {
        commands.clear();
        drawCount = dispatchCount = renderPassCount = 0;
        pipelineBindCount = descriptorBindCount = barrierCount = 0;
    }

    std::string getSummary() const {
        std::string result;
        result += "Frame " + std::to_string(frameNumber) + ": ";
        result += std::to_string(commands.size()) + " commands, ";
        result += std::to_string(drawCount) + " draws, ";
        result += std::to_string(dispatchCount) + " dispatches, ";
        result += std::to_string(renderPassCount) + " render passes\n";
        return result;
    }
};

class CommandCapture {
public:
    CommandCapture() = default;

    // === Capture Control ===

    // Enable/disable continuous capture (every frame)
    void setContinuousCapture(bool enabled) { continuousCapture_ = enabled; }
    bool isContinuousCapture() const { return continuousCapture_; }

    // Request a single frame capture (auto-disables after capture)
    void requestSingleCapture() { singleCaptureRequested_ = true; }

    // Check if currently capturing
    bool isCapturing() const { return capturing_; }

    // === Frame Recording ===

    void beginFrame(uint64_t frameNumber) {
        if (!continuousCapture_ && !singleCaptureRequested_) {
            capturing_ = false;
            return;
        }

        capturing_ = true;
        currentFrame_.clear();
        currentFrame_.frameNumber = frameNumber;
        frameStartTime_ = std::chrono::high_resolution_clock::now();
    }

    void endFrame() {
        if (!capturing_) return;

        auto endTime = std::chrono::high_resolution_clock::now();
        currentFrame_.totalTimeMs =
            std::chrono::duration<float, std::milli>(endTime - frameStartTime_).count();

        lastCapture_ = currentFrame_;
        hasCapture_ = true;

        if (singleCaptureRequested_) {
            singleCaptureRequested_ = false;
        }

        capturing_ = false;
    }

    // === Command Recording ===

    void recordDraw(const char* source, uint32_t vertexCount, uint32_t instanceCount = 1) {
        if (!capturing_) return;
        CapturedCommand cmd;
        cmd.type = CommandType::Draw;
        cmd.source = source;
        cmd.vertexCount = vertexCount;
        cmd.instanceCount = instanceCount;
        cmd.timestampMs = getTimeSinceFrameStart();
        cmd.details = "vertices=" + std::to_string(vertexCount) +
                      " instances=" + std::to_string(instanceCount);
        currentFrame_.commands.push_back(std::move(cmd));
        currentFrame_.drawCount++;
    }

    void recordDrawIndexed(const char* source, uint32_t indexCount, uint32_t instanceCount = 1) {
        if (!capturing_) return;
        CapturedCommand cmd;
        cmd.type = CommandType::DrawIndexed;
        cmd.source = source;
        cmd.indexCount = indexCount;
        cmd.instanceCount = instanceCount;
        cmd.timestampMs = getTimeSinceFrameStart();
        cmd.details = "indices=" + std::to_string(indexCount) +
                      " instances=" + std::to_string(instanceCount);
        currentFrame_.commands.push_back(std::move(cmd));
        currentFrame_.drawCount++;
    }

    void recordDrawIndirect(const char* source, uint32_t drawCount) {
        if (!capturing_) return;
        CapturedCommand cmd;
        cmd.type = CommandType::DrawIndirect;
        cmd.source = source;
        cmd.timestampMs = getTimeSinceFrameStart();
        cmd.details = "drawCount=" + std::to_string(drawCount);
        currentFrame_.commands.push_back(std::move(cmd));
        currentFrame_.drawCount += drawCount;
    }

    void recordDispatch(const char* source, uint32_t groupX, uint32_t groupY, uint32_t groupZ) {
        if (!capturing_) return;
        CapturedCommand cmd;
        cmd.type = CommandType::Dispatch;
        cmd.source = source;
        cmd.groupCountX = groupX;
        cmd.groupCountY = groupY;
        cmd.groupCountZ = groupZ;
        cmd.timestampMs = getTimeSinceFrameStart();
        cmd.details = "groups=(" + std::to_string(groupX) + "," +
                      std::to_string(groupY) + "," + std::to_string(groupZ) + ")";
        currentFrame_.commands.push_back(std::move(cmd));
        currentFrame_.dispatchCount++;
    }

    void recordBeginRenderPass(const char* source, const char* passName = nullptr) {
        if (!capturing_) return;
        CapturedCommand cmd;
        cmd.type = CommandType::BeginRenderPass;
        cmd.source = source;
        cmd.timestampMs = getTimeSinceFrameStart();
        if (passName) cmd.details = passName;
        currentFrame_.commands.push_back(std::move(cmd));
        currentFrame_.renderPassCount++;
    }

    void recordEndRenderPass(const char* source) {
        if (!capturing_) return;
        CapturedCommand cmd;
        cmd.type = CommandType::EndRenderPass;
        cmd.source = source;
        cmd.timestampMs = getTimeSinceFrameStart();
        currentFrame_.commands.push_back(std::move(cmd));
    }

    void recordBindPipeline(const char* source, VkPipelineBindPoint bindPoint) {
        if (!capturing_) return;
        CapturedCommand cmd;
        cmd.type = CommandType::BindPipeline;
        cmd.source = source;
        cmd.bindPoint = bindPoint;
        cmd.timestampMs = getTimeSinceFrameStart();
        cmd.details = (bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) ? "Graphics" : "Compute";
        currentFrame_.commands.push_back(std::move(cmd));
        currentFrame_.pipelineBindCount++;
    }

    void recordBindDescriptorSet(const char* source, uint32_t setIndex) {
        if (!capturing_) return;
        CapturedCommand cmd;
        cmd.type = CommandType::BindDescriptorSet;
        cmd.source = source;
        cmd.timestampMs = getTimeSinceFrameStart();
        cmd.details = "set=" + std::to_string(setIndex);
        currentFrame_.commands.push_back(std::move(cmd));
        currentFrame_.descriptorBindCount++;
    }

    void recordPipelineBarrier(const char* source, const char* description = nullptr) {
        if (!capturing_) return;
        CapturedCommand cmd;
        cmd.type = CommandType::PipelineBarrier;
        cmd.source = source;
        cmd.timestampMs = getTimeSinceFrameStart();
        if (description) cmd.details = description;
        currentFrame_.commands.push_back(std::move(cmd));
        currentFrame_.barrierCount++;
    }

    void recordOther(const char* source, const char* description) {
        if (!capturing_) return;
        CapturedCommand cmd;
        cmd.type = CommandType::Other;
        cmd.source = source;
        cmd.timestampMs = getTimeSinceFrameStart();
        cmd.details = description;
        currentFrame_.commands.push_back(std::move(cmd));
    }

    // === Results ===

    bool hasCapture() const { return hasCapture_; }
    const CapturedFrame& getLastCapture() const { return lastCapture_; }

    // Generate a detailed report for the last captured frame
    std::string generateReport() const {
        if (!hasCapture_) return "No capture available\n";

        std::string report;
        report += "=== Command Capture Report ===\n";
        report += lastCapture_.getSummary();
        report += "\n";

        std::string currentSource;
        for (const auto& cmd : lastCapture_.commands) {
            // Group by source
            if (cmd.source != currentSource) {
                currentSource = cmd.source;
                report += "\n[" + currentSource + "]\n";
            }

            report += "  ";
            report += commandTypeName(cmd.type);
            if (!cmd.details.empty()) {
                report += " (" + cmd.details + ")";
            }
            report += " @" + std::to_string(cmd.timestampMs) + "ms\n";
        }

        return report;
    }

private:
    float getTimeSinceFrameStart() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<float, std::milli>(now - frameStartTime_).count();
    }

    bool continuousCapture_ = false;
    bool singleCaptureRequested_ = false;
    bool capturing_ = false;
    bool hasCapture_ = false;

    CapturedFrame currentFrame_;
    CapturedFrame lastCapture_;
    std::chrono::high_resolution_clock::time_point frameStartTime_;
};
