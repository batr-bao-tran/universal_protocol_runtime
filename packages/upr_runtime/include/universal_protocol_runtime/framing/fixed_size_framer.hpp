#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FIXED_SIZE_FRAMER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FIXED_SIZE_FRAMER_HPP_
#include <cstddef>

#include "universal_protocol_runtime/framing/framer.hpp"

namespace universal_protocol_runtime {

class FixedSizeFramer final : public IFramer {
 public:
  explicit FixedSizeFramer(size_t frame_size) : frame_size_(frame_size) {}

  ~FixedSizeFramer() noexcept override = default;

  FrameStatus try_frame(ByteSpan readable_bytes, FrameSlice* frame) const override;

 private:
  size_t frame_size_ = 0;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FIXED_SIZE_FRAMER_HPP_
