#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FIXED_SIZE_FRAMER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FIXED_SIZE_FRAMER_HPP_
#include <cstddef>

#include "universal_protocol_runtime/framing/framer.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Framer that emits fixed-width frames.
 */
class FixedSizeFramer final : public IFramer {
 public:
  /**
   * @brief Constructs a fixed-size framer.
   * @param frame_size Size in bytes for each frame.
   * @return No return value.
   */
  explicit FixedSizeFramer(size_t frame_size) : frame_size_(frame_size) {}

  /**
   * @brief Destroys the framer.
   * @return No return value.
   */
  ~FixedSizeFramer() noexcept override = default;

  /**
   * @brief Attempts to extract one fixed-size frame.
   * @param readable_bytes Contiguous readable bytes to inspect.
   * @param frame Destination frame slice when a frame is ready.
   * @return Framing status for the current readable bytes.
   */
  FrameStatus try_frame(ByteSpan readable_bytes, FrameSlice* frame) const override;

 private:
  size_t frame_size_ = 0;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FIXED_SIZE_FRAMER_HPP_
