#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODE_STATUS_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODE_STATUS_HPP_
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

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODE_STATUS_HPP_
