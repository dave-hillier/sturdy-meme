#!/bin/bash
# Vulkan Usage Analysis Script
# Identifies all Vulkan API usages and custom RAII wrappers to guide vulkan-hpp migration
# Usage: ./scripts/analyze-vulkan-usage.sh [--verbose] [--file <specific_file>]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$PROJECT_ROOT/src"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

VERBOSE=false
SPECIFIC_FILE=""

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --file|-f)
            SPECIFIC_FILE="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--verbose] [--file <specific_file>]"
            exit 1
            ;;
    esac
done

echo -e "${BOLD}╔══════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}║           Vulkan Usage Analysis for vulkan-hpp Migration                 ║${NC}"
echo -e "${BOLD}╚══════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Determine search path
if [[ -n "$SPECIFIC_FILE" ]]; then
    SEARCH_PATH="$SPECIFIC_FILE"
else
    SEARCH_PATH="$SRC_DIR"
fi

# ============================================================================
# Section 1: VulkanRAII Custom Wrappers
# ============================================================================
echo -e "${CYAN}┌──────────────────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│ Section 1: Custom VulkanRAII Wrappers                                    │${NC}"
echo -e "${CYAN}└──────────────────────────────────────────────────────────────────────────┘${NC}"
echo ""

# Define RAII types and their vulkan-hpp equivalents
declare -A RAII_TYPES=(
    ["ManagedBuffer"]="vk::raii::Buffer"
    ["ManagedImage"]="vk::raii::Image"
    ["ManagedImageView"]="vk::raii::ImageView"
    ["ManagedSampler"]="vk::raii::Sampler"
    ["ManagedPipeline"]="vk::raii::Pipeline"
    ["ManagedPipelineLayout"]="vk::raii::PipelineLayout"
    ["ManagedRenderPass"]="vk::raii::RenderPass"
    ["ManagedFramebuffer"]="vk::raii::Framebuffer"
    ["ManagedDescriptorSetLayout"]="vk::raii::DescriptorSetLayout"
    ["ManagedCommandPool"]="vk::raii::CommandPool"
    ["ManagedSemaphore"]="vk::raii::Semaphore"
    ["ManagedFence"]="vk::raii::Fence"
    ["UniqueVmaBuffer"]="vk::raii::Buffer + VmaAllocation"
    ["UniqueVmaImage"]="vk::raii::Image + VmaAllocation"
    ["UniquePipeline"]="vk::raii::Pipeline"
    ["UniqueRenderPass"]="vk::raii::RenderPass"
    ["UniquePipelineLayout"]="vk::raii::PipelineLayout"
    ["UniqueDescriptorSetLayout"]="vk::raii::DescriptorSetLayout"
    ["UniqueImageView"]="vk::raii::ImageView"
    ["UniqueFramebuffer"]="vk::raii::Framebuffer"
    ["UniqueFence"]="vk::raii::Fence"
    ["UniqueSemaphore"]="vk::raii::Semaphore"
    ["UniqueCommandPool"]="vk::raii::CommandPool"
    ["UniqueDescriptorPool"]="vk::raii::DescriptorPool"
    ["UniqueSampler"]="vk::raii::Sampler"
)

for TYPE in "${!RAII_TYPES[@]}"; do
    COUNT=$(grep -r --include="*.cpp" --include="*.h" -l "$TYPE" "$SEARCH_PATH" 2>/dev/null | wc -l | tr -d ' ')
    if [[ $COUNT -gt 0 ]]; then
        echo -e "${YELLOW}$TYPE${NC} found in ${BOLD}$COUNT${NC} files → ${GREEN}${RAII_TYPES[$TYPE]}${NC}"
        if [[ "$VERBOSE" == "true" ]]; then
            grep -r --include="*.cpp" --include="*.h" -n "$TYPE" "$SEARCH_PATH" 2>/dev/null | head -5
            echo ""
        fi
    fi
done

echo ""

# ============================================================================
# Section 2: Raw Vulkan API Calls
# ============================================================================
echo -e "${CYAN}┌──────────────────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│ Section 2: Raw Vulkan API Calls (vkCreate*, vkCmd*, etc.)                │${NC}"
echo -e "${CYAN}└──────────────────────────────────────────────────────────────────────────┘${NC}"
echo ""

# vkCreate* functions and their vulkan-hpp builder equivalents
declare -A VK_CREATE_FUNCS=(
    ["vkCreateBuffer"]="device.createBuffer(vk::BufferCreateInfo{}.setSize(...).setUsage(...))"
    ["vkCreateImage"]="device.createImage(vk::ImageCreateInfo{}.setImageType(...).setFormat(...))"
    ["vkCreateImageView"]="device.createImageView(vk::ImageViewCreateInfo{}.setImage(...).setViewType(...))"
    ["vkCreateSampler"]="device.createSampler(vk::SamplerCreateInfo{}.setMagFilter(...).setMinFilter(...))"
    ["vkCreatePipeline"]="device.createGraphicsPipeline(cache, vk::GraphicsPipelineCreateInfo{}.setStages(...))"
    ["vkCreateGraphicsPipelines"]="device.createGraphicsPipelines(cache, pipelineInfos)"
    ["vkCreateComputePipelines"]="device.createComputePipelines(cache, pipelineInfos)"
    ["vkCreatePipelineLayout"]="device.createPipelineLayout(vk::PipelineLayoutCreateInfo{}.setSetLayouts(...))"
    ["vkCreateRenderPass"]="device.createRenderPass(vk::RenderPassCreateInfo{}.setAttachments(...).setSubpasses(...))"
    ["vkCreateFramebuffer"]="device.createFramebuffer(vk::FramebufferCreateInfo{}.setRenderPass(...).setAttachments(...))"
    ["vkCreateDescriptorSetLayout"]="device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{}.setBindings(...))"
    ["vkCreateDescriptorPool"]="device.createDescriptorPool(vk::DescriptorPoolCreateInfo{}.setMaxSets(...).setPoolSizes(...))"
    ["vkCreateCommandPool"]="device.createCommandPool(vk::CommandPoolCreateInfo{}.setQueueFamilyIndex(...).setFlags(...))"
    ["vkCreateFence"]="device.createFence(vk::FenceCreateInfo{}.setFlags(...))"
    ["vkCreateSemaphore"]="device.createSemaphore(vk::SemaphoreCreateInfo{})"
    ["vkCreateShaderModule"]="device.createShaderModule(vk::ShaderModuleCreateInfo{}.setCodeSize(...).setPCode(...))"
    ["vkCreateSwapchainKHR"]="device.createSwapchainKHR(vk::SwapchainCreateInfoKHR{}.setSurface(...).setMinImageCount(...))"
)

for FUNC in "${!VK_CREATE_FUNCS[@]}"; do
    COUNT=$(grep -r --include="*.cpp" --include="*.h" "$FUNC" "$SEARCH_PATH" 2>/dev/null | grep -v "^\s*//" | wc -l | tr -d ' ')
    if [[ $COUNT -gt 0 ]]; then
        echo -e "${RED}$FUNC${NC} found ${BOLD}$COUNT${NC} times"
        echo -e "  ${GREEN}→ ${VK_CREATE_FUNCS[$FUNC]}${NC}"
        if [[ "$VERBOSE" == "true" ]]; then
            grep -r --include="*.cpp" --include="*.h" -n "$FUNC" "$SEARCH_PATH" 2>/dev/null | grep -v "^\s*//" | head -3
        fi
        echo ""
    fi
done

# ============================================================================
# Section 3: Vulkan Command Functions (vkCmd*)
# ============================================================================
echo -e "${CYAN}┌──────────────────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│ Section 3: Vulkan Command Buffer Functions (vkCmd*)                      │${NC}"
echo -e "${CYAN}└──────────────────────────────────────────────────────────────────────────┘${NC}"
echo ""

declare -A VK_CMD_FUNCS=(
    ["vkCmdBeginRenderPass"]="cmd.beginRenderPass(vk::RenderPassBeginInfo{}.setRenderPass(...).setFramebuffer(...), vk::SubpassContents::eInline)"
    ["vkCmdEndRenderPass"]="cmd.endRenderPass()"
    ["vkCmdBindPipeline"]="cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline)"
    ["vkCmdBindVertexBuffers"]="cmd.bindVertexBuffers(0, buffers, offsets)"
    ["vkCmdBindIndexBuffer"]="cmd.bindIndexBuffer(buffer, offset, vk::IndexType::eUint32)"
    ["vkCmdBindDescriptorSets"]="cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, sets, dynamicOffsets)"
    ["vkCmdDraw"]="cmd.draw(vertexCount, instanceCount, firstVertex, firstInstance)"
    ["vkCmdDrawIndexed"]="cmd.drawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance)"
    ["vkCmdDrawIndexedIndirect"]="cmd.drawIndexedIndirect(buffer, offset, drawCount, stride)"
    ["vkCmdDispatch"]="cmd.dispatch(groupCountX, groupCountY, groupCountZ)"
    ["vkCmdCopyBuffer"]="cmd.copyBuffer(src, dst, vk::BufferCopy{}.setSrcOffset(...).setDstOffset(...).setSize(...))"
    ["vkCmdCopyBufferToImage"]="cmd.copyBufferToImage(buffer, image, layout, regions)"
    ["vkCmdCopyImage"]="cmd.copyImage(src, srcLayout, dst, dstLayout, regions)"
    ["vkCmdBlitImage"]="cmd.blitImage(src, srcLayout, dst, dstLayout, regions, filter)"
    ["vkCmdPipelineBarrier"]="cmd.pipelineBarrier(srcStage, dstStage, {}, memoryBarriers, bufferBarriers, imageBarriers)"
    ["vkCmdPushConstants"]="cmd.pushConstants(layout, stages, offset, size, data)"
    ["vkCmdSetViewport"]="cmd.setViewport(0, vk::Viewport{}.setX(...).setY(...).setWidth(...).setHeight(...))"
    ["vkCmdSetScissor"]="cmd.setScissor(0, vk::Rect2D{}.setOffset(...).setExtent(...))"
)

for FUNC in "${!VK_CMD_FUNCS[@]}"; do
    COUNT=$(grep -r --include="*.cpp" --include="*.h" "$FUNC" "$SEARCH_PATH" 2>/dev/null | grep -v "^\s*//" | wc -l | tr -d ' ')
    if [[ $COUNT -gt 0 ]]; then
        echo -e "${RED}$FUNC${NC} found ${BOLD}$COUNT${NC} times"
        echo -e "  ${GREEN}→ ${VK_CMD_FUNCS[$FUNC]}${NC}"
        echo ""
    fi
done

# ============================================================================
# Section 4: Vulkan Structure Types (VkFooCreateInfo)
# ============================================================================
echo -e "${CYAN}┌──────────────────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│ Section 4: Vulkan C Structures (migrate to vk::* builder pattern)        │${NC}"
echo -e "${CYAN}└──────────────────────────────────────────────────────────────────────────┘${NC}"
echo ""

STRUCT_PATTERNS=(
    "VkBufferCreateInfo"
    "VkImageCreateInfo"
    "VkImageViewCreateInfo"
    "VkSamplerCreateInfo"
    "VkGraphicsPipelineCreateInfo"
    "VkComputePipelineCreateInfo"
    "VkPipelineLayoutCreateInfo"
    "VkRenderPassCreateInfo"
    "VkRenderPassBeginInfo"
    "VkFramebufferCreateInfo"
    "VkDescriptorSetLayoutCreateInfo"
    "VkDescriptorPoolCreateInfo"
    "VkCommandPoolCreateInfo"
    "VkCommandBufferAllocateInfo"
    "VkCommandBufferBeginInfo"
    "VkFenceCreateInfo"
    "VkSemaphoreCreateInfo"
    "VkSubmitInfo"
    "VkPresentInfoKHR"
    "VkSwapchainCreateInfoKHR"
    "VkAttachmentDescription"
    "VkSubpassDescription"
    "VkSubpassDependency"
    "VkPipelineShaderStageCreateInfo"
    "VkPipelineVertexInputStateCreateInfo"
    "VkPipelineInputAssemblyStateCreateInfo"
    "VkPipelineViewportStateCreateInfo"
    "VkPipelineRasterizationStateCreateInfo"
    "VkPipelineMultisampleStateCreateInfo"
    "VkPipelineDepthStencilStateCreateInfo"
    "VkPipelineColorBlendStateCreateInfo"
    "VkPipelineDynamicStateCreateInfo"
    "VkDescriptorSetLayoutBinding"
    "VkDescriptorPoolSize"
    "VkWriteDescriptorSet"
    "VkDescriptorBufferInfo"
    "VkDescriptorImageInfo"
    "VkImageMemoryBarrier"
    "VkBufferMemoryBarrier"
    "VkMemoryBarrier"
)

for STRUCT in "${STRUCT_PATTERNS[@]}"; do
    COUNT=$(grep -r --include="*.cpp" --include="*.h" "$STRUCT" "$SEARCH_PATH" 2>/dev/null | grep -v "^\s*//" | wc -l | tr -d ' ')
    if [[ $COUNT -gt 0 ]]; then
        # Convert VkFooBar to vk::FooBar
        VK_HPP_TYPE=$(echo "$STRUCT" | sed 's/^Vk/vk::/')
        echo -e "${YELLOW}$STRUCT${NC} found ${BOLD}$COUNT${NC} times → ${GREEN}${VK_HPP_TYPE}{}${NC}"
    fi
done

echo ""

# ============================================================================
# Section 5: Vulkan Handle Types
# ============================================================================
echo -e "${CYAN}┌──────────────────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│ Section 5: Raw Vulkan Handle Types                                       │${NC}"
echo -e "${CYAN}└──────────────────────────────────────────────────────────────────────────┘${NC}"
echo ""

declare -A VK_HANDLES=(
    ["VkDevice[^A-Za-z]"]="vk::Device or vk::raii::Device"
    ["VkInstance[^A-Za-z]"]="vk::Instance or vk::raii::Instance"
    ["VkPhysicalDevice[^A-Za-z]"]="vk::PhysicalDevice or vk::raii::PhysicalDevice"
    ["VkQueue[^A-Za-z]"]="vk::Queue or vk::raii::Queue"
    ["VkCommandBuffer[^A-Za-z]"]="vk::CommandBuffer or vk::raii::CommandBuffer"
    ["VkBuffer[^A-Za-z]"]="vk::Buffer or vk::raii::Buffer"
    ["VkImage[^A-Za-z]"]="vk::Image or vk::raii::Image"
    ["VkImageView[^A-Za-z]"]="vk::ImageView or vk::raii::ImageView"
    ["VkSampler[^A-Za-z]"]="vk::Sampler or vk::raii::Sampler"
    ["VkPipeline[^A-Za-z]"]="vk::Pipeline or vk::raii::Pipeline"
    ["VkPipelineLayout[^A-Za-z]"]="vk::PipelineLayout or vk::raii::PipelineLayout"
    ["VkRenderPass[^A-Za-z]"]="vk::RenderPass or vk::raii::RenderPass"
    ["VkFramebuffer[^A-Za-z]"]="vk::Framebuffer or vk::raii::Framebuffer"
    ["VkDescriptorSet[^A-Za-z]"]="vk::DescriptorSet or vk::raii::DescriptorSet"
    ["VkDescriptorSetLayout[^A-Za-z]"]="vk::DescriptorSetLayout or vk::raii::DescriptorSetLayout"
    ["VkDescriptorPool[^A-Za-z]"]="vk::DescriptorPool or vk::raii::DescriptorPool"
    ["VkCommandPool[^A-Za-z]"]="vk::CommandPool or vk::raii::CommandPool"
    ["VkFence[^A-Za-z]"]="vk::Fence or vk::raii::Fence"
    ["VkSemaphore[^A-Za-z]"]="vk::Semaphore or vk::raii::Semaphore"
    ["VkShaderModule[^A-Za-z]"]="vk::ShaderModule or vk::raii::ShaderModule"
    ["VkSwapchainKHR"]="vk::SwapchainKHR or vk::raii::SwapchainKHR"
    ["VkSurfaceKHR"]="vk::SurfaceKHR or vk::raii::SurfaceKHR"
)

for PATTERN in "${!VK_HANDLES[@]}"; do
    DISPLAY_NAME=$(echo "$PATTERN" | sed 's/\[.*$//')
    COUNT=$(grep -rE --include="*.cpp" --include="*.h" "$PATTERN" "$SEARCH_PATH" 2>/dev/null | grep -v "^\s*//" | wc -l | tr -d ' ')
    if [[ $COUNT -gt 0 ]]; then
        echo -e "${YELLOW}$DISPLAY_NAME${NC} found ${BOLD}$COUNT${NC} times → ${GREEN}${VK_HANDLES[$PATTERN]}${NC}"
    fi
done

echo ""

# ============================================================================
# Section 6: Summary and Statistics
# ============================================================================
echo -e "${CYAN}┌──────────────────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│ Section 6: Migration Summary                                             │${NC}"
echo -e "${CYAN}└──────────────────────────────────────────────────────────────────────────┘${NC}"
echo ""

TOTAL_VK_CALLS=$(grep -rE --include="*.cpp" --include="*.h" "vk[A-Z][a-zA-Z]+\(" "$SEARCH_PATH" 2>/dev/null | grep -v "^\s*//" | wc -l | tr -d ' ')
TOTAL_VK_STRUCTS=$(grep -rE --include="*.cpp" --include="*.h" "Vk[A-Z][a-zA-Z]+Info" "$SEARCH_PATH" 2>/dev/null | grep -v "^\s*//" | wc -l | tr -d ' ')
TOTAL_RAII_USAGE=$(grep -rE --include="*.cpp" --include="*.h" "(Managed|Unique)(Buffer|Image|Sampler|Pipeline|RenderPass|Fence|Semaphore)" "$SEARCH_PATH" 2>/dev/null | wc -l | tr -d ' ')

echo -e "${BOLD}Statistics:${NC}"
echo -e "  Raw Vulkan function calls (vk*):  ${RED}$TOTAL_VK_CALLS${NC}"
echo -e "  Raw Vulkan struct usages (Vk*Info): ${RED}$TOTAL_VK_STRUCTS${NC}"
echo -e "  Custom RAII wrapper usages:        ${YELLOW}$TOTAL_RAII_USAGE${NC}"

echo ""
echo -e "${BOLD}Files with most Vulkan usage:${NC}"
grep -rE --include="*.cpp" --include="*.h" "vk[A-Z]|Vk[A-Z]" "$SEARCH_PATH" 2>/dev/null | \
    cut -d: -f1 | sort | uniq -c | sort -rn | head -10 | \
    while read count file; do
        echo -e "  ${BOLD}$count${NC} usages: $file"
    done

echo ""

# ============================================================================
# Section 7: Conversion Guide
# ============================================================================
echo -e "${CYAN}╔══════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║                    vulkan-hpp Conversion Guide                           ║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""

cat << 'GUIDE'
BUILDER PATTERN (Required - positional constructors disabled)
═════════════════════════════════════════════════════════════

✗ WRONG (positional constructor - will not compile):
  vk::BufferCreateInfo bufferInfo(flags, size, usage, sharingMode);

✓ CORRECT (builder pattern using setters):
  auto bufferInfo = vk::BufferCreateInfo{}
      .setSize(bufferSize)
      .setUsage(vk::BufferUsageFlagBits::eVertexBuffer)
      .setSharingMode(vk::SharingMode::eExclusive);

✓ ALSO CORRECT (C++20 designated initializers):
  vk::BufferCreateInfo bufferInfo{
      .size = bufferSize,
      .usage = vk::BufferUsageFlagBits::eVertexBuffer,
      .sharingMode = vk::SharingMode::eExclusive
  };


COMMON CONVERSION PATTERNS
══════════════════════════

1. Buffer Creation (with VMA):
   ────────────────────────────
   BEFORE:
     VkBufferCreateInfo bufferInfo{};
     bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
     bufferInfo.size = size;
     bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
     vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &alloc, nullptr);

   AFTER:
     auto bufferInfo = vk::BufferCreateInfo{}
         .setSize(size)
         .setUsage(vk::BufferUsageFlagBits::eVertexBuffer);
     vmaCreateBuffer(allocator,
         reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
         &allocInfo, &buffer, &alloc, nullptr);


2. Image View Creation:
   ────────────────────────────
   BEFORE:
     VkImageViewCreateInfo viewInfo{};
     viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
     viewInfo.image = image;
     viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
     viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
     viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
     viewInfo.subresourceRange.levelCount = 1;
     viewInfo.subresourceRange.layerCount = 1;
     vkCreateImageView(device, &viewInfo, nullptr, &imageView);

   AFTER:
     auto viewInfo = vk::ImageViewCreateInfo{}
         .setImage(image)
         .setViewType(vk::ImageViewType::e2D)
         .setFormat(vk::Format::eR8G8B8A8Srgb)
         .setSubresourceRange(vk::ImageSubresourceRange{}
             .setAspectMask(vk::ImageAspectFlagBits::eColor)
             .setLevelCount(1)
             .setLayerCount(1));
     auto imageView = device.createImageView(viewInfo);


3. Pipeline Creation:
   ────────────────────────────
   BEFORE:
     VkGraphicsPipelineCreateInfo pipelineInfo{};
     pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
     pipelineInfo.stageCount = 2;
     pipelineInfo.pStages = shaderStages;
     // ... many more fields ...
     vkCreateGraphicsPipelines(device, cache, 1, &pipelineInfo, nullptr, &pipeline);

   AFTER:
     auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
         .setStages(shaderStages)
         .setPVertexInputState(&vertexInputInfo)
         .setPInputAssemblyState(&inputAssembly)
         .setPViewportState(&viewportState)
         .setPRasterizationState(&rasterizer)
         .setPMultisampleState(&multisampling)
         .setPDepthStencilState(&depthStencil)
         .setPColorBlendState(&colorBlending)
         .setPDynamicState(&dynamicState)
         .setLayout(pipelineLayout)
         .setRenderPass(renderPass);
     auto pipeline = device.createGraphicsPipeline(cache, pipelineInfo);


4. Command Buffer Recording:
   ────────────────────────────
   BEFORE:
     VkRenderPassBeginInfo renderPassInfo{};
     renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
     renderPassInfo.renderPass = renderPass;
     renderPassInfo.framebuffer = framebuffer;
     renderPassInfo.renderArea.extent = {width, height};
     vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
     vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
     vkCmdDraw(cmd, 3, 1, 0, 0);
     vkCmdEndRenderPass(cmd);

   AFTER:
     cmd.beginRenderPass(vk::RenderPassBeginInfo{}
         .setRenderPass(renderPass)
         .setFramebuffer(framebuffer)
         .setRenderArea(vk::Rect2D{{0,0}, {width, height}}),
         vk::SubpassContents::eInline);
     cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
     cmd.draw(3, 1, 0, 0);
     cmd.endRenderPass();


5. Descriptor Updates:
   ────────────────────────────
   BEFORE:
     VkDescriptorBufferInfo bufferInfo{};
     bufferInfo.buffer = uniformBuffer;
     bufferInfo.offset = 0;
     bufferInfo.range = sizeof(UBO);

     VkWriteDescriptorSet write{};
     write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
     write.dstSet = descriptorSet;
     write.dstBinding = 0;
     write.descriptorCount = 1;
     write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
     write.pBufferInfo = &bufferInfo;
     vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

   AFTER:
     auto bufferInfo = vk::DescriptorBufferInfo{}
         .setBuffer(uniformBuffer)
         .setOffset(0)
         .setRange(sizeof(UBO));

     device.updateDescriptorSets(
         vk::WriteDescriptorSet{}
             .setDstSet(descriptorSet)
             .setDstBinding(0)
             .setDescriptorType(vk::DescriptorType::eUniformBuffer)
             .setBufferInfo(bufferInfo),
         nullptr);


RAII WRAPPER MIGRATION
═══════════════════════

Your custom VulkanRAII.h types map to vulkan-hpp RAII:

  ManagedBuffer           → vk::raii::Buffer (but keep VMA for allocation)
  ManagedImage            → vk::raii::Image (but keep VMA for allocation)
  ManagedImageView        → vk::raii::ImageView
  ManagedSampler          → vk::raii::Sampler
  ManagedPipeline         → vk::raii::Pipeline
  ManagedPipelineLayout   → vk::raii::PipelineLayout
  ManagedRenderPass       → vk::raii::RenderPass
  ManagedFramebuffer      → vk::raii::Framebuffer
  ManagedFence            → vk::raii::Fence
  ManagedSemaphore        → vk::raii::Semaphore
  ManagedCommandPool      → vk::raii::CommandPool

NOTE: For VMA-allocated resources, you'll likely want to keep your
custom wrappers since vk::raii types don't integrate with VMA directly.


DYNAMIC DISPATCH SETUP (Required with VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
═══════════════════════════════════════════════════════════════════════════

Add this to ONE .cpp file (e.g., main.cpp or VulkanContext.cpp):

  VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

Initialize the dispatcher after creating the instance and device:

  // After instance creation:
  VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

  // After device creation:
  VULKAN_HPP_DEFAULT_DISPATCHER.init(device);

GUIDE

echo ""
echo -e "${BOLD}Analysis complete.${NC} Run with ${GREEN}--verbose${NC} for detailed file locations."
