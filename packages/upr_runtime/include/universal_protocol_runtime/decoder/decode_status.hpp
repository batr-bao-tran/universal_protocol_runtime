#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODE_STATUS_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODE_STATUS_HPP_
#include <cstddef>
#include <string_view>

namespace universal_protocol_runtime {

/**
 * @brief Result codes returned by runtime decode operations.
 */
enum class DecodeStatus {
  kOk,
  kMessageNotFound,
  kSchemaMismatch,
  kInvalidData,
  kChecksumMismatch,
  kFieldLimitExceeded,
};

/**
 * @brief Converts a decode status to a stable string name.
 * @param status Status value to stringify.
 * @return String representation of the status.
 */
constexpr std::string_view to_string(DecodeStatus status) noexcept {
  switch (status) {
    case DecodeStatus::kOk:
      return "ok";
    case DecodeStatus::kMessageNotFound:
      return "message_not_found";
    case DecodeStatus::kSchemaMismatch:
      return "schema_mismatch";
    case DecodeStatus::kInvalidData:
      return "invalid_data";
    case DecodeStatus::kChecksumMismatch:
      return "checksum_mismatch";
    case DecodeStatus::kFieldLimitExceeded:
      return "field_limit_exceeded";
  }
  return "unknown";
}

/**
 * @brief Rich decode failure context.
 *
 * Populated by generated decoders to identify which field failed and at what
 * byte offset within the frame, so a malformed frame is as easy to debug as a
 * JSON parse error. `field_name` is empty and `byte_offset` is 0 on success or
 * when no field-level context is available (for example a top-level dispatch
 * prefix mismatch).
 */
struct DecodeError {
  DecodeStatus status = DecodeStatus::kOk;
  std::string_view field_name;
  std::size_t byte_offset = 0;

  /**
   * @brief Reports whether the error represents a successful decode.
   * @return `true` when status is `kOk`.
   */
  constexpr bool ok() const noexcept { return status == DecodeStatus::kOk; }
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODE_STATUS_HPP_
