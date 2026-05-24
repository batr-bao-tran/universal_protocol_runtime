#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__LENGTH_PREFIXED_FRAMER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__LENGTH_PREFIXED_FRAMER_HPP_
#include <cstddef>
#include <cstdint>

#include "universal_protocol_runtime/core/types.hpp"
#include "universal_protocol_runtime/framing/framer.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Options for framing messages with a fixed-width length prefix.
 */
struct LengthPrefixedFramerOptions {
  uint8_t prefix_width_bytes = 2;
  ByteOrder byte_order = ByteOrder::kLittleEndian;
  bool include_prefix_in_payload = false;
  size_t max_payload_size = 1024 * 1024;
};

/**
 * @brief Framer that extracts messages preceded by a numeric length prefix.
 */
class LengthPrefixedFramer final : public IFramer {
 public:
  /**
   * @brief Constructs a length-prefixed framer.
   * @param options Framer configuration.
   * @return No return value.
   */
  explicit LengthPrefixedFramer(LengthPrefixedFramerOptions options) : options_(options) {}

  /**
   * @brief Destroys the framer.
   * @return No return value.
   */
  ~LengthPrefixedFramer() noexcept override = default;

  /**
   * @brief Attempts to extract one length-prefixed frame.
   * @param readable_bytes Contiguous readable bytes to inspect.
   * @param frame Destination frame slice when a frame is ready.
   * @return Framing status for the current readable bytes.
   */
  FrameStatus try_frame(ByteSpan readable_bytes, FrameSlice* frame) const override;

 private:
  LengthPrefixedFramerOptions options_;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__LENGTH_PREFIXED_FRAMER_HPP_
