#pragma once

#include <cstdint>
#include <utility>

namespace BufferUtils {

// Tracks which buffer is for reading vs writing in a ping-pong scheme
class PingPongTracker {
public:
    void advance() { std::swap(writeIndex_, readIndex_); }
    void reset() { writeIndex_ = 0; readIndex_ = 1; }

    uint32_t getWriteIndex() const { return writeIndex_; }
    uint32_t getReadIndex() const { return readIndex_; }

private:
    uint32_t writeIndex_ = 0;
    uint32_t readIndex_ = 1;
};

}  // namespace BufferUtils
