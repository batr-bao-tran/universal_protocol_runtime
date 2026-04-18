#include "universal_protocol_runtime/framing/length_prefixed_framer.hpp"

#include <optional>

#include "universal_protocol_runtime/core/compiler_hints.hpp"

namespace universal_protocol_runtime {
namespace {

constexpr uint8_t kPrefixWidth1 = sizeof(uint8_t);
constexpr uint8_t kPrefixWidth2 = sizeof(uint16_t);
constexpr uint8_t kPrefixWidth4 = sizeof(uint32_t);
constexpr size_t kBitsPerByte = 8U;

std::optional<uint64_t> read_prefix(ByteSpan bytes, ByteOrder byte_order) {
  if (UPR_UNLIKELY(bytes.size() > sizeof(uint64_t))) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }
  uint64_t value = 0;
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      for (size_t index = 0; index < bytes.size(); ++index) {
        value |= static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[index])) << (index * kBitsPerByte);
      }
      return value;
    case ByteOrder::kBigEndian:
      for (const std::byte byte : bytes) {
        value <<= kBitsPerByte;
        value |= static_cast<uint64_t>(std::to_integer<uint8_t>(byte));
      }
      return value;
  }
  return std::nullopt;
}

}  // namespace

FrameStatus LengthPrefixedFramer::try_frame(ByteSpan readable_bytes, FrameSlice* frame) const {
  if (options_.prefix_width_bytes != kPrefixWidth1 && options_.prefix_width_bytes != kPrefixWidth2 &&
      options_.prefix_width_bytes != kPrefixWidth4) {
    return FrameStatus::kInvalidFrame;
  }
  if (readable_bytes.size() < options_.prefix_width_bytes) {
    return FrameStatus::kNeedMoreData;
  }
  const auto payload_size = read_prefix(readable_bytes.first(options_.prefix_width_bytes), options_.byte_order);
  if (!payload_size.has_value() || *payload_size > options_.max_payload_size) {
    return FrameStatus::kInvalidFrame;
  }
  const size_t total_size = options_.prefix_width_bytes + static_cast<size_t>(*payload_size);
  if (readable_bytes.size() < total_size) {
    return FrameStatus::kNeedMoreData;
  }
  if (frame != nullptr) {
    *frame = FrameSlice{
        .offset = options_.include_prefix_in_payload ? 0U : static_cast<size_t>(options_.prefix_width_bytes),
        .size = options_.include_prefix_in_payload ? total_size : static_cast<size_t>(*payload_size),
        .bytes_consumed = total_size,
    };
  }
  return FrameStatus::kReady;
}

}  // namespace universal_protocol_runtime
