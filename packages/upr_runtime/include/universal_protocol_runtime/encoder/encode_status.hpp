#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__ENCODE_STATUS_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__ENCODE_STATUS_HPP_

#include <string_view>

namespace universal_protocol_runtime {

enum class EncodeStatus {
  kOk,
  kBufferTooSmall,
  kInvalidData,
  kSchemaMismatch,
  kFieldLimitExceeded,
};

constexpr std::string_view to_string(EncodeStatus status) noexcept {
  switch (status) {
    case EncodeStatus::kOk:
      return "ok";
    case EncodeStatus::kBufferTooSmall:
      return "buffer_too_small";
    case EncodeStatus::kInvalidData:
      return "invalid_data";
    case EncodeStatus::kSchemaMismatch:
      return "schema_mismatch";
    case EncodeStatus::kFieldLimitExceeded:
      return "field_limit_exceeded";
  }
  return "unknown";
}

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__ENCODE_STATUS_HPP_
