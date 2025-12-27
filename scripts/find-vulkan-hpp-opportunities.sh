#!/bin/bash
# Script to identify Vulkan-Hpp refactoring opportunities
# Searches for patterns that could benefit from Vulkan-Hpp's modern C++ API

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SRC_DIR="$PROJECT_ROOT/src"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
BOLD='\033[1m'

echo -e "${BOLD}========================================${NC}"
echo -e "${BOLD}Vulkan-Hpp Refactoring Opportunities${NC}"
echo -e "${BOLD}========================================${NC}"
echo ""

# Track statistics
total_opportunities=0

# Function to report findings
report_finding() {
    local category="$1"
    local file="$2"
    local line="$3"
    local suggestion="$4"
    local example="$5"

    ((total_opportunities++)) || true

    echo -e "${CYAN}[$category]${NC} ${file}:${line}"
    echo -e "  ${YELLOW}→${NC} $suggestion"
    if [ -n "$example" ]; then
        echo -e "  ${GREEN}Example:${NC} $example"
    fi
    echo ""
}

# Function to search and report
search_pattern() {
    local pattern="$1"
    local category="$2"
    local suggestion="$3"
    local example="$4"
    local exclude_pattern="${5:-NOMATCH_PLACEHOLDER}"

    while IFS=: read -r file line content; do
        # Skip if matches exclude pattern
        if [[ "$content" =~ $exclude_pattern ]]; then
            continue
        fi
        # Get relative path
        rel_file="${file#$PROJECT_ROOT/}"
        report_finding "$category" "$rel_file" "$line" "$suggestion" "$example"
    done < <(grep -rn --include="*.cpp" --include="*.h" -E "$pattern" "$SRC_DIR" 2>/dev/null | head -50 || true)
}

echo -e "${BOLD}1. RAW VULKAN HANDLE DECLARATIONS${NC}"
echo -e "${BOLD}-----------------------------------${NC}"
echo "These raw Vk* handles could use vk::raii::* RAII wrappers"
echo ""

# Find raw VkBuffer, VkImage, etc. member declarations (not in function params)
for handle in "VkBuffer" "VkImage" "VkImageView" "VkSampler" "VkPipeline" "VkPipelineLayout" \
              "VkDescriptorSetLayout" "VkDescriptorPool" "VkRenderPass" "VkFramebuffer" \
              "VkCommandPool" "VkFence" "VkSemaphore" "VkShaderModule"; do
    while IFS=: read -r file line content; do
        # Skip if it's already using Managed* or unique_ptr
        if [[ "$content" =~ Managed|unique_ptr|Unique|std::vector ]]; then
            continue
        fi
        # Skip function parameters and casts
        if [[ "$content" =~ \(.*$handle.*\)|static_cast|reinterpret_cast ]]; then
            continue
        fi
        rel_file="${file#$PROJECT_ROOT/}"
        raii_type="${handle/Vk/vk::raii::}"
        report_finding "Raw Handle" "$rel_file" "$line" \
            "Replace raw $handle with $raii_type" \
            "$handle buffer; → $raii_type buffer{device, createInfo};"
    done < <(grep -rn --include="*.cpp" --include="*.h" -E "^\s+$handle\s+\w+[;=]" "$SRC_DIR" 2>/dev/null | head -20 || true)
done

echo -e "${BOLD}2. MANUAL vkCreate* CALLS${NC}"
echo -e "${BOLD}-------------------------${NC}"
echo "Direct Vulkan creation calls could use vk::raii constructors"
echo ""

# Find vkCreate* calls
for func in "vkCreateBuffer" "vkCreateImage" "vkCreateImageView" "vkCreateSampler" \
            "vkCreateGraphicsPipelines" "vkCreateComputePipelines" "vkCreatePipelineLayout" \
            "vkCreateDescriptorSetLayout" "vkCreateDescriptorPool" "vkCreateRenderPass" \
            "vkCreateFramebuffer" "vkCreateCommandPool" "vkCreateFence" "vkCreateSemaphore" \
            "vkCreateShaderModule"; do
    while IFS=: read -r file line content; do
        rel_file="${file#$PROJECT_ROOT/}"
        # Extract the type being created
        type_name="${func/vkCreate/}"
        type_name="${type_name/s$/}"  # Remove trailing 's' from Pipelines
        report_finding "Manual Create" "$rel_file" "$line" \
            "Use vk::raii::$type_name constructor instead of $func" \
            "$func(...) → vk::raii::$type_name{device, createInfo}"
    done < <(grep -rn --include="*.cpp" --include="*.h" -E "\b$func\s*\(" "$SRC_DIR" 2>/dev/null | head -10 || true)
done

echo -e "${BOLD}3. MANUAL vkDestroy* CALLS${NC}"
echo -e "${BOLD}--------------------------${NC}"
echo "Manual destruction calls are unnecessary with RAII"
echo ""

for func in "vkDestroyBuffer" "vkDestroyImage" "vkDestroyImageView" "vkDestroySampler" \
            "vkDestroyPipeline" "vkDestroyPipelineLayout" "vkDestroyDescriptorSetLayout" \
            "vkDestroyDescriptorPool" "vkDestroyRenderPass" "vkDestroyFramebuffer" \
            "vkDestroyCommandPool" "vkDestroyFence" "vkDestroySemaphore" "vkDestroyShaderModule"; do
    while IFS=: read -r file line content; do
        # Skip if in deleter/RAII implementation
        if [[ "$file" =~ VulkanRAII|Deleter ]]; then
            continue
        fi
        rel_file="${file#$PROJECT_ROOT/}"
        type_name="${func/vkDestroy/}"
        report_finding "Manual Destroy" "$rel_file" "$line" \
            "Remove $func - use vk::raii::$type_name for automatic cleanup" \
            "Destructor handles cleanup automatically"
    done < <(grep -rn --include="*.cpp" --include="*.h" -E "\b$func\s*\(" "$SRC_DIR" 2>/dev/null | head -10 || true)
done

echo -e "${BOLD}4. VK_STRUCTURE_TYPE_* USAGE${NC}"
echo -e "${BOLD}----------------------------${NC}"
echo "Vulkan-Hpp structs auto-initialize sType"
echo ""

while IFS=: read -r file line content; do
    rel_file="${file#$PROJECT_ROOT/}"
    # Extract the structure type
    if [[ "$content" =~ VK_STRUCTURE_TYPE_([A-Z_]+) ]]; then
        struct_type="${BASH_REMATCH[1]}"
        # Convert to vk:: style (e.g., BUFFER_CREATE_INFO -> BufferCreateInfo)
        vk_struct=$(echo "$struct_type" | sed 's/_\([A-Z]\)/\L\1/g' | sed 's/^./\U&/' | sed 's/_//g')
        report_finding "sType Init" "$rel_file" "$line" \
            "Use vk::$vk_struct{} - sType is auto-initialized" \
            "VkBufferCreateInfo{VK_STRUCTURE_TYPE_...} → vk::BufferCreateInfo{}"
    fi
done < <(grep -rn --include="*.cpp" --include="*.h" -E "VK_STRUCTURE_TYPE_" "$SRC_DIR" 2>/dev/null | head -30 || true)

echo -e "${BOLD}5. RAW ENUM VALUES${NC}"
echo -e "${BOLD}-------------------${NC}"
echo "Use type-safe vk:: enums instead of VK_* macros"
echo ""

# Count enum usage by type (fast summary instead of detailed listing)
declare -A enum_mappings=(
    ["VK_FORMAT_"]="vk::Format::e"
    ["VK_IMAGE_USAGE_"]="vk::ImageUsageFlagBits::e"
    ["VK_BUFFER_USAGE_"]="vk::BufferUsageFlagBits::e"
    ["VK_SHADER_STAGE_"]="vk::ShaderStageFlagBits::e"
    ["VK_PIPELINE_STAGE_"]="vk::PipelineStageFlagBits::e"
    ["VK_IMAGE_LAYOUT_"]="vk::ImageLayout::e"
    ["VK_DESCRIPTOR_TYPE_"]="vk::DescriptorType::e"
)

for prefix in "${!enum_mappings[@]}"; do
    vk_prefix="${enum_mappings[$prefix]}"
    count=$(grep -r --include="*.cpp" --include="*.h" -c "${prefix}" "$SRC_DIR" 2>/dev/null | awk -F: '{s+=$2} END {print s}' || echo "0")
    if [ "$count" -gt 0 ]; then
        echo -e "${CYAN}[Raw Enum]${NC} ${prefix}* found ~$count times"
        echo -e "  ${YELLOW}→${NC} Replace with ${vk_prefix}* type-safe enums"
        echo ""
        ((total_opportunities++)) || true
    fi
done

echo -e "${BOLD}6. VULKANRAII.H MIGRATION${NC}"
echo -e "${BOLD}--------------------------${NC}"
echo "Migrate from your custom VulkanRAII.h to Vulkan-Hpp"
echo ""

# 6a. Managed* wrapper classes
echo -e "${YELLOW}Managed* Wrapper Classes:${NC}"
declare -A managed_mappings=(
    ["ManagedPipeline"]="vk::raii::Pipeline"
    ["ManagedPipelineLayout"]="vk::raii::PipelineLayout"
    ["ManagedDescriptorSetLayout"]="vk::raii::DescriptorSetLayout"
    ["ManagedRenderPass"]="vk::raii::RenderPass"
    ["ManagedFramebuffer"]="vk::raii::Framebuffer"
    ["ManagedImageView"]="vk::raii::ImageView"
    ["ManagedCommandPool"]="vk::raii::CommandPool"
    ["ManagedFence"]="vk::raii::Fence"
    ["ManagedSemaphore"]="vk::raii::Semaphore"
    ["ManagedSampler"]="vk::raii::Sampler"
    ["ManagedDescriptorPool"]="vk::raii::DescriptorPool"
)

for managed in "${!managed_mappings[@]}"; do
    raii_type="${managed_mappings[$managed]}"
    while IFS=: read -r file line content; do
        rel_file="${file#$PROJECT_ROOT/}"
        # Skip VulkanRAII.h itself
        if [[ "$rel_file" == *"VulkanRAII.h"* ]]; then
            continue
        fi
        report_finding "Managed→RAII" "$rel_file" "$line" \
            "Replace $managed with $raii_type" \
            "$managed pipeline; → $raii_type pipeline{device, createInfo};"
    done < <(grep -rn --include="*.cpp" --include="*.h" -E "\b$managed\b" "$SRC_DIR" 2>/dev/null | head -5 || true)
done

# 6b. Unique* type aliases
echo -e "${YELLOW}Unique* Type Aliases:${NC}"
for unique_type in "UniquePipeline" "UniqueRenderPass" "UniquePipelineLayout" \
                   "UniqueDescriptorSetLayout" "UniqueImageView" "UniqueFramebuffer" \
                   "UniqueFence" "UniqueSemaphore" "UniqueCommandPool" "UniqueDescriptorPool" \
                   "UniqueSampler"; do
    raii_type="${unique_type/Unique/vk::raii::}"
    count=$(grep -r --include="*.cpp" --include="*.h" -c "\b$unique_type\b" "$SRC_DIR" 2>/dev/null | awk -F: '{s+=$2} END {print s+0}')
    if [ "$count" -gt 0 ]; then
        echo -e "${CYAN}[Unique→RAII]${NC} $unique_type used $count times"
        echo -e "  ${YELLOW}→${NC} Replace with $raii_type"
        echo ""
        ((total_opportunities++)) || true
    fi
done

# 6c. VMA-integrated types (need special handling)
echo -e "${YELLOW}VMA-Integrated Types (require custom handling):${NC}"
for vma_type in "ManagedBuffer" "ManagedImage" "UniqueVmaBuffer" "UniqueVmaImage"; do
    count=$(grep -r --include="*.cpp" --include="*.h" -c "\b$vma_type\b" "$SRC_DIR" 2>/dev/null | awk -F: '{s+=$2} END {print s+0}')
    if [ "$count" -gt 0 ]; then
        echo -e "${CYAN}[VMA Type]${NC} $vma_type used $count times"
        echo -e "  ${YELLOW}→${NC} Keep as-is OR create vk::raii wrapper with VMA allocator"
        echo -e "  ${GREEN}Note:${NC} Vulkan-Hpp doesn't include VMA - need custom integration"
        echo ""
        ((total_opportunities++)) || true
    fi
done

# 6d. Custom deleters
echo -e "${YELLOW}Custom Deleters:${NC}"
deleter_count=$(grep -r --include="*.cpp" --include="*.h" -c "VkHandleDeleter\|VmaImageDeleter\|VmaBufferDeleter" "$SRC_DIR" 2>/dev/null | awk -F: '{s+=$2} END {print s+0}')
if [ "$deleter_count" -gt 0 ]; then
    echo -e "${CYAN}[Deleters]${NC} Custom deleters used $deleter_count times"
    echo -e "  ${YELLOW}→${NC} vk::raii handles destruction automatically"
    echo -e "  ${GREEN}Note:${NC} VMA deleters still needed for VMA-allocated resources"
    echo ""
    ((total_opportunities++)) || true
fi

# 6e. RAII scope helpers
echo -e "${YELLOW}RAII Scope Helpers:${NC}"
for helper in "CommandScope" "RenderPassScope" "ScopeGuard"; do
    count=$(grep -r --include="*.cpp" --include="*.h" -c "\b$helper\b" "$SRC_DIR" 2>/dev/null | awk -F: '{s+=$2} END {print s+0}')
    if [ "$count" -gt 0 ]; then
        case "$helper" in
            "CommandScope")
                suggestion="Use vk::raii::CommandBuffer with RAII lifetime"
                ;;
            "RenderPassScope")
                suggestion="Use vk::raii methods or keep for fluent builder pattern"
                ;;
            "ScopeGuard")
                suggestion="Keep as-is - generic C++ utility not Vulkan-specific"
                ;;
        esac
        echo -e "${CYAN}[Scope Helper]${NC} $helper used $count times"
        echo -e "  ${YELLOW}→${NC} $suggestion"
        echo ""
        ((total_opportunities++)) || true
    fi
done

# 6f. Factory methods that return raw handles
echo -e "${YELLOW}Factory Methods Returning Raw Handles:${NC}"
while IFS=: read -r file line content; do
    rel_file="${file#$PROJECT_ROOT/}"
    if [[ "$content" =~ (create[A-Z][a-zA-Z]*)\( ]]; then
        func_name="${BASH_REMATCH[1]}"
        report_finding "Factory" "$rel_file" "$line" \
            "Consider returning vk::raii::* from $func_name" \
            "VkBuffer ${func_name}(...) → vk::raii::Buffer ${func_name}(...)"
    fi
done < <(grep -rn --include="*.cpp" --include="*.h" -E "^(VkBuffer|VkImage|VkImageView|VkSampler|VkPipeline)\s+create[A-Z]" "$SRC_DIR" 2>/dev/null | head -10 || true)

echo -e "${BOLD}7. FUNCTION POINTER LOADING${NC}"
echo -e "${BOLD}---------------------------${NC}"
echo "Vulkan-Hpp handles function loading automatically"
echo ""

while IFS=: read -r file line content; do
    rel_file="${file#$PROJECT_ROOT/}"
    report_finding "Func Loading" "$rel_file" "$line" \
        "Vulkan-Hpp's DispatchLoaderDynamic handles extension loading" \
        "vkGetDeviceProcAddr → VULKAN_HPP_DEFAULT_DISPATCHER"
done < <(grep -rn --include="*.cpp" --include="*.h" -E "vkGet(Device|Instance)ProcAddr|PFN_vk" "$SRC_DIR" 2>/dev/null | head -5 || true)

echo -e "${BOLD}8. ERROR CHECKING PATTERNS${NC}"
echo -e "${BOLD}--------------------------${NC}"
echo "Vulkan-Hpp can use exceptions or vk::Result for cleaner error handling"
echo ""

while IFS=: read -r file line content; do
    rel_file="${file#$PROJECT_ROOT/}"
    report_finding "Error Check" "$rel_file" "$line" \
        "Use vk::Result or exceptions instead of VkResult checks" \
        "if (result != VK_SUCCESS) → try/catch or result.value()"
done < <(grep -rn --include="*.cpp" --include="*.h" -E "!= VK_SUCCESS|== VK_SUCCESS" "$SRC_DIR" 2>/dev/null | head -10 || true)

echo -e "${BOLD}9. POTENTIAL REFACTORING FILES${NC}"
echo -e "${BOLD}------------------------------${NC}"
echo "Files with highest refactoring potential (by raw Vulkan usage):"
echo ""

# Count raw Vulkan calls per file
declare -A file_scores
while IFS=: read -r file rest; do
    ((file_scores["$file"]++)) || file_scores["$file"]=1
done < <(grep -rn --include="*.cpp" --include="*.h" -E "vk(Create|Destroy|Cmd|Get|Allocate|Free|Begin|End)[A-Z]|VK_STRUCTURE_TYPE_" "$SRC_DIR" 2>/dev/null || true)

# Sort and display top files
for file in "${!file_scores[@]}"; do
    echo "${file_scores[$file]} $file"
done | sort -rn | head -15 | while read count file; do
    rel_file="${file#$PROJECT_ROOT/}"
    echo -e "  ${YELLOW}$count${NC} opportunities in ${CYAN}$rel_file${NC}"
done

echo ""
echo -e "${BOLD}========================================${NC}"
echo -e "${BOLD}SUMMARY${NC}"
echo -e "${BOLD}========================================${NC}"
echo -e "Total refactoring opportunities found: ${GREEN}$total_opportunities${NC}"
echo ""
echo -e "${BOLD}Recommended Migration Strategy:${NC}"
echo "1. Start with new code - use vk::raii::* for new features"
echo "2. Keep VMA integration - vk::raii doesn't replace memory allocation"
echo "3. Migrate leaf classes first (renderers, systems)"
echo "4. Update VulkanContext last as it's the foundation"
echo "5. Use interop: rawHandle = *raiiObject to mix old/new code"
echo ""
echo -e "${BOLD}Key Files to Update First:${NC}"
echo "  - src/core/VulkanResourceFactory.cpp (factory methods)"
echo "  - src/core/PipelineBuilder.cpp (pipeline creation)"
echo "  - Individual renderer/system classes"
echo ""
echo -e "${BOLD}Files to Keep As-Is:${NC}"
echo "  - src/core/VulkanRAII.h (can coexist or be phased out)"
echo "  - VMA-related code (ManagedBuffer, ManagedImage)"
