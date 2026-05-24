#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FRAMER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FRAMER_HPP_
#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/framing/frame_result.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Abstract interface that extracts frames from readable bytes.
 */
class IFramer {
 public:
  /**
   * @brief Destroys the framer interface.
   * @return No return value.
   */
  virtual ~IFramer() noexcept = default;

  /**
   * @brief Attempts to identify one complete frame from readable bytes.
   * @param readable_bytes Contiguous readable bytes to inspect.
   * @param frame Destination frame slice when a frame is ready.
   * @return Framing status for the current readable bytes.
   */
  virtual FrameStatus try_frame(ByteSpan readable_bytes, FrameSlice* frame) const = 0;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FRAMER_HPP_
