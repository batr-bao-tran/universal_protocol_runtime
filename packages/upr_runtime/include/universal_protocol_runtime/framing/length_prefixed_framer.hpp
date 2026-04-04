#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__LENGTH_PREFIXED_FRAMER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__LENGTH_PREFIXED_FRAMER_HPP_
#include <cstddef>
#include <cstdint>

#include "universal_protocol_runtime/core/types.hpp"
#include "universal_protocol_runtime/framing/framer.hpp"

namespace universal_protocol_runtime {

struct LengthPrefixedFramerOptions {
  uint8_t prefix_width_bytes = 2;
  ByteOrder byte_order = ByteOrder::kLittleEndian;
  bool include_prefix_in_payload = false;
  size_t max_payload_size = 1024 * 1024;
};

class LengthPrefixedFramer final : public IFramer {
 public:
  explicit LengthPrefixedFramer(LengthPrefixedFramerOptions options) : options_(options) {}

  ~LengthPrefixedFramer() noexcept override = default;

  FrameStatus try_frame(ByteSpan readable_bytes, FrameSlice* frame) const override;

 private:
  LengthPrefixedFramerOptions options_;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__LENGTH_PREFIXED_FRAMER_HPP_
