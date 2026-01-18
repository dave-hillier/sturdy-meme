#pragma once

// BufferUtils.h - Umbrella header for backward compatibility
// New code should include only the specific headers needed:
//   - SingleBuffer.h        - Single one-shot buffers
//   - PerFrameBuffer.h      - Per-frame buffer sets
//   - DoubleBufferedBuffer.h - Double-buffered buffer sets
//   - DynamicUniformBuffer.h - Dynamic uniform buffers
//   - DoubleBufferedImage.h  - Ping-pong image sets
//   - FrameIndexedBuffers.h  - RAII frame-indexed buffers
//   - PingPongTracker.h      - Read/write index tracking

#include "SingleBuffer.h"
#include "PerFrameBuffer.h"
#include "DoubleBufferedBuffer.h"
#include "DynamicUniformBuffer.h"
#include "DoubleBufferedImage.h"
#include "FrameIndexedBuffers.h"
#include "PingPongTracker.h"
