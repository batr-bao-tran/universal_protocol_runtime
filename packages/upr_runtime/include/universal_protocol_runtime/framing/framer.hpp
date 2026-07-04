#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FRAMER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FRAMER_HPP_
#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/framing/frame_result.hpp"

namespace universal_protocol_runtime {

class IFramer {
 public:
  virtual ~IFramer() noexcept = default;

  virtual FrameStatus try_frame(ByteSpan readable_bytes, FrameSlice* frame) const = 0;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FRAMER_HPP_
