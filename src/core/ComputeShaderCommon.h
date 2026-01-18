#pragma once

#include <cstdint>

// Common compute shader constants and utilities
// These replace magic numbers scattered across vegetation, atmosphere, and terrain systems

namespace ComputeConstants {

// Standard workgroup sizes for different compute patterns
constexpr uint32_t WORKGROUP_SIZE_1D = 256;        // Default for 1D particle/instance processing
constexpr uint32_t WORKGROUP_SIZE_2D = 16;         // Default for 2D image/grid processing (16x16 threads)
constexpr uint32_t WORKGROUP_SIZE_2D_TOTAL = 256;  // 16 * 16 = 256 threads per workgroup

// Calculate dispatch count for 1D workloads
// Returns ceil(itemCount / workgroupSize)
inline constexpr uint32_t getDispatchCount1D(uint32_t itemCount, uint32_t workgroupSize = WORKGROUP_SIZE_1D) {
    return (itemCount + workgroupSize - 1) / workgroupSize;
}

// Calculate dispatch count for 2D workloads
// Returns ceil(dimension / workgroupSize)
inline constexpr uint32_t getDispatchCount2D(uint32_t dimension, uint32_t workgroupSize = WORKGROUP_SIZE_2D) {
    return (dimension + workgroupSize - 1) / workgroupSize;
}

} // namespace ComputeConstants
