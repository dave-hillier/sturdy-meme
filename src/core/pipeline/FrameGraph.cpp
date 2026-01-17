#include "FrameGraph.h"
#include "threading/TaskScheduler.h"
#include "vulkan/ThreadedCommandPool.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <atomic>
#include <queue>
#include <sstream>

FrameGraph::PassId FrameGraph::addPass(const std::string& name, PassFunction execute) {
    return addPass(PassConfig{
        .name = name,
        .execute = std::move(execute),
        .canUseSecondary = false,
        .mainThreadOnly = true,
        .priority = 0
    });
}

FrameGraph::PassId FrameGraph::addPass(const PassConfig& config) {
    PassId id = nextPassId_++;

    Pass pass;
    pass.id = id;
    pass.config = config;
    pass.enabled = true;

    passes_.push_back(std::move(pass));
    nameToId_[config.name] = id;

    compiled_ = false;
    return id;
}

void FrameGraph::addDependency(PassId from, PassId to) {
    if (from >= passes_.size() || to >= passes_.size()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "FrameGraph: Invalid pass ID in addDependency(%u, %u)", from, to);
        return;
    }

    if (from == to) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "FrameGraph: Cannot add self-dependency for pass %u", from);
        return;
    }

    // Add 'to' depends on 'from'
    auto& deps = passes_[to].dependencies;
    if (std::find(deps.begin(), deps.end(), from) == deps.end()) {
        deps.push_back(from);
    }

    // Add 'from' has dependent 'to'
    auto& dependents = passes_[from].dependents;
    if (std::find(dependents.begin(), dependents.end(), to) == dependents.end()) {
        dependents.push_back(to);
    }

    compiled_ = false;
}

void FrameGraph::removePass(PassId id) {
    if (id >= passes_.size()) {
        return;
    }

    // Remove from name map
    nameToId_.erase(passes_[id].config.name);

    // Remove references from other passes
    for (auto& pass : passes_) {
        auto& deps = pass.dependencies;
        deps.erase(std::remove(deps.begin(), deps.end(), id), deps.end());

        auto& dependents = pass.dependents;
        dependents.erase(std::remove(dependents.begin(), dependents.end(), id), dependents.end());
    }

    // Mark as removed (don't actually erase to preserve IDs)
    passes_[id].config.name = "";
    passes_[id].config.execute = nullptr;
    passes_[id].enabled = false;

    compiled_ = false;
}

void FrameGraph::setPassEnabled(PassId id, bool enabled) {
    if (id < passes_.size()) {
        passes_[id].enabled = enabled;
    }
}

bool FrameGraph::isPassEnabled(PassId id) const {
    return id < passes_.size() && passes_[id].enabled;
}

bool FrameGraph::topologicalSort(std::vector<std::vector<PassId>>& levels) {
    levels.clear();

    // Calculate in-degree for each pass (only count enabled passes)
    std::vector<uint32_t> inDegree(passes_.size(), 0);
    std::vector<bool> active(passes_.size(), false);

    for (const auto& pass : passes_) {
        if (!pass.enabled || !pass.config.execute) continue;
        active[pass.id] = true;

        for (PassId dep : pass.dependencies) {
            if (dep < passes_.size() && passes_[dep].enabled && passes_[dep].config.execute) {
                inDegree[pass.id]++;
            }
        }
    }

    // Find all passes with no dependencies (start nodes)
    std::queue<PassId> readyQueue;
    for (size_t i = 0; i < passes_.size(); ++i) {
        if (active[i] && inDegree[i] == 0) {
            readyQueue.push(static_cast<PassId>(i));
        }
    }

    size_t processedCount = 0;
    size_t activeCount = 0;
    for (bool a : active) if (a) activeCount++;

    while (!readyQueue.empty()) {
        // Process all passes at current level
        std::vector<PassId> currentLevel;
        size_t levelSize = readyQueue.size();

        for (size_t i = 0; i < levelSize; ++i) {
            PassId id = readyQueue.front();
            readyQueue.pop();

            currentLevel.push_back(id);
            processedCount++;

            // Decrement in-degree for dependents
            for (PassId dependent : passes_[id].dependents) {
                if (active[dependent]) {
                    inDegree[dependent]--;
                    if (inDegree[dependent] == 0) {
                        readyQueue.push(dependent);
                    }
                }
            }
        }

        // Sort level by priority (higher priority first)
        std::sort(currentLevel.begin(), currentLevel.end(),
            [this](PassId a, PassId b) {
                return passes_[a].config.priority > passes_[b].config.priority;
            });

        levels.push_back(std::move(currentLevel));
    }

    // Check for cycles
    if (processedCount != activeCount) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "FrameGraph: Cycle detected! Processed %zu of %zu active passes",
            processedCount, activeCount);
        return false;
    }

    return true;
}

bool FrameGraph::compile() {
    if (!topologicalSort(executionLevels_)) {
        compiled_ = false;
        return false;
    }

    SDL_Log("FrameGraph: Compiled with %zu levels:", executionLevels_.size());
    for (size_t i = 0; i < executionLevels_.size(); ++i) {
        std::string passNames;
        for (PassId id : executionLevels_[i]) {
            if (!passNames.empty()) passNames += ", ";
            passNames += passes_[id].config.name;
        }
        SDL_Log("  Level %zu: [%s]", i, passNames.c_str());
    }

    compiled_ = true;
    return true;
}

void FrameGraph::execute(RenderContext& context, TaskScheduler* scheduler) {
    if (!compiled_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "FrameGraph: Cannot execute - not compiled");
        return;
    }

    for (const auto& level : executionLevels_) {
        if (level.empty()) continue;

        // Check if we can parallelize this level
        bool canParallelize = scheduler != nullptr && level.size() > 1;
        if (canParallelize) {
            // Check if all passes in level support parallel execution
            for (PassId id : level) {
                if (passes_[id].config.mainThreadOnly) {
                    canParallelize = false;
                    break;
                }
            }
        }

        if (canParallelize) {
            // Execute passes in parallel using task scheduler
            TaskGroup group;
            for (PassId id : level) {
                if (!passes_[id].enabled) continue;

                scheduler->submit([this, id, &context]() {
                    passes_[id].config.execute(context);
                }, &group);
            }
            group.wait();
        } else {
            // Execute passes sequentially
            for (PassId id : level) {
                // Bounds check
                if (id >= passes_.size()) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "FrameGraph: Invalid pass ID %u (max %zu)", id, passes_.size());
                    continue;
                }

                if (!passes_[id].enabled) continue;

                const auto& config = passes_[id].config;

                // Skip passes with null execute function
                if (!config.execute) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "FrameGraph: Skipping pass %s with null execute function", config.name.c_str());
                    continue;
                }

                // Check if this pass uses secondary command buffers for parallel recording
                if (config.canUseSecondary &&
                    config.secondarySlots > 0 &&
                    config.secondaryRecord &&
                    context.threadedCommandPool &&
                    scheduler) {

                    // Parallel secondary command buffer recording (Phase 4)
                    executeWithSecondaryBuffers(context, passes_[id], scheduler);
                } else {
                    // Standard primary buffer execution
                    config.execute(context);
                }
            }
        }
    }
}

void FrameGraph::executeWithSecondaryBuffers(
    RenderContext& context,
    const Pass& pass,
    TaskScheduler* scheduler) {

    const auto& config = pass.config;
    uint32_t numSlots = config.secondarySlots;
    ThreadedCommandPool* pool = context.threadedCommandPool;

    // Validate inputs
    if (numSlots == 0 || !pool || !scheduler || !config.secondaryRecord) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "FrameGraph: Invalid parameters for secondary buffer execution (pass: %s)",
            config.name.c_str());
        // Fall back to standard execution
        if (config.execute) {
            config.execute(context);
        }
        return;
    }

    // Validate render pass and framebuffer for inheritance
    if (!context.renderPass || !context.framebuffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "FrameGraph: Missing renderPass or framebuffer for secondary buffers (pass: %s)",
            config.name.c_str());
        if (config.execute) {
            config.execute(context);
        }
        return;
    }

    // Allocate secondary command buffers for each slot
    std::vector<vk::CommandBuffer> secondaryBuffers(numSlots);
    std::atomic<uint32_t> failureCount{0};

    // Create a task group for parallel recording
    TaskGroup group;

    for (uint32_t slot = 0; slot < numSlots; ++slot) {
        scheduler->submit([&, slot]() {
            try {
                // Get thread ID for command pool allocation
                uint32_t threadId = TaskScheduler::instance().getCurrentThreadId();

                // Allocate secondary buffer from thread's pool
                vk::CommandBuffer secondary = pool->allocateSecondary(context.frameIndex, threadId);
                if (!secondary) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "FrameGraph: Failed to allocate secondary buffer for slot %u", slot);
                    failureCount.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                // Begin secondary command buffer with render pass inheritance
                auto inheritance = vk::CommandBufferInheritanceInfo{}
                    .setRenderPass(context.renderPass)
                    .setSubpass(0)
                    .setFramebuffer(context.framebuffer);

                secondary.begin(vk::CommandBufferBeginInfo{}
                    .setFlags(vk::CommandBufferUsageFlagBits::eRenderPassContinue |
                              vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
                    .setPInheritanceInfo(&inheritance));

                // Create context for this secondary buffer
                RenderContext secondaryCtx = context;
                secondaryCtx.commandBuffer = secondary;

                // Record commands for this slot
                config.secondaryRecord(secondaryCtx, slot);

                // End secondary buffer
                secondary.end();

                // Store for later execution
                secondaryBuffers[slot] = secondary;
            } catch (const vk::SystemError& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "FrameGraph: Vulkan error in secondary buffer slot %u: %s", slot, e.what());
                failureCount.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "FrameGraph: Exception in secondary buffer slot %u: %s", slot, e.what());
                failureCount.fetch_add(1, std::memory_order_relaxed);
            } catch (...) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "FrameGraph: Unknown exception in secondary buffer slot %u", slot);
                failureCount.fetch_add(1, std::memory_order_relaxed);
            }
        }, &group);
    }

    // Wait for all secondary buffers to be recorded
    group.wait();

    // Check if any slots failed
    if (failureCount.load(std::memory_order_relaxed) > 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "FrameGraph: %u/%u secondary buffer slots failed for pass %s",
            failureCount.load(), numSlots, config.name.c_str());
        // Filter out null buffers
        secondaryBuffers.erase(
            std::remove_if(secondaryBuffers.begin(), secondaryBuffers.end(),
                          [](vk::CommandBuffer buf) { return !buf; }),
            secondaryBuffers.end());
    }

    // Pass the secondary buffers to the execute function
    // The execute function is responsible for:
    // 1. Beginning the render pass with eSecondaryCommandBuffers
    // 2. Calling commandBuffer.executeCommands(secondaryBuffers)
    // 3. Ending the render pass
    context.secondaryBuffers = &secondaryBuffers;
    if (config.execute) {
        config.execute(context);
    }
    context.secondaryBuffers = nullptr;
}

FrameGraph::PassId FrameGraph::getPass(const std::string& name) const {
    auto it = nameToId_.find(name);
    return (it != nameToId_.end()) ? it->second : INVALID_PASS;
}

void FrameGraph::clear() {
    passes_.clear();
    nameToId_.clear();
    executionLevels_.clear();
    nextPassId_ = 0;
    compiled_ = false;
}

std::string FrameGraph::debugString() const {
    std::stringstream ss;
    ss << "FrameGraph (" << passes_.size() << " passes):\n";

    for (const auto& pass : passes_) {
        if (!pass.config.execute) continue;

        ss << "  " << pass.config.name;
        if (!pass.enabled) ss << " [DISABLED]";
        ss << " (id=" << pass.id << ")";

        if (!pass.dependencies.empty()) {
            ss << " <- [";
            for (size_t i = 0; i < pass.dependencies.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << passes_[pass.dependencies[i]].config.name;
            }
            ss << "]";
        }
        ss << "\n";
    }

    if (compiled_) {
        ss << "\nExecution order (" << executionLevels_.size() << " levels):\n";
        for (size_t i = 0; i < executionLevels_.size(); ++i) {
            ss << "  Level " << i << ": ";
            for (size_t j = 0; j < executionLevels_[i].size(); ++j) {
                if (j > 0) ss << ", ";
                ss << passes_[executionLevels_[i][j]].config.name;
            }
            ss << "\n";
        }
    }

    return ss.str();
}
