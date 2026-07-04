#include "universal_protocol_runtime/framing/fixed_size_framer.hpp"

namespace universal_protocol_runtime {

FrameStatus FixedSizeFramer::try_frame(ByteSpan readable_bytes, FrameSlice* frame) const {
  if (frame_size_ == 0) {
    return FrameStatus::kInvalidFrame;
  }
  if (readable_bytes.size() < frame_size_) {
    return FrameStatus::kNeedMoreData;
  }
  if (frame != nullptr) {
    *frame = FrameSlice{
        .offset = 0,
        .size = frame_size_,
        .bytes_consumed = frame_size_,
    };
  }
  return FrameStatus::kReady;
}

}  // namespace universal_protocol_runtime
