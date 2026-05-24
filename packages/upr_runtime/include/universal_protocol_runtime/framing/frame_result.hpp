#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FRAME_RESULT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FRAME_RESULT_HPP_
#include <cstddef>
#include <string_view>

namespace universal_protocol_runtime {

enum class FrameStatus {
  kReady,
  kNeedMoreData,
  kInvalidFrame,
};

/**
 * @brief Byte-range description for one extracted frame.
 */
struct FrameSlice {
  size_t offset = 0;
  size_t size = 0;
  size_t bytes_consumed = 0;
};

/**
 * @brief Converts a frame status to a stable string view.
 * @param status Frame status value.
 * @return String representation of the frame status.
 */
constexpr std::string_view to_string(FrameStatus status) noexcept {
  switch (status) {
    case FrameStatus::kReady:
      return "ready";
    case FrameStatus::kNeedMoreData:
      return "need_more_data";
    case FrameStatus::kInvalidFrame:
      return "invalid_frame";
  }
  return "unknown";
}

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_FRAMING__FRAME_RESULT_HPP_
