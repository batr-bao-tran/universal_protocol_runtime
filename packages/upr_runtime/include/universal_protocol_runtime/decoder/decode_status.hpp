#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODE_STATUS_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODE_STATUS_HPP_
#include <string_view>

namespace universal_protocol_runtime {

enum class DecodeStatus {
  kOk,
  kMessageNotFound,
  kSchemaMismatch,
  kInvalidData,
  kChecksumMismatch,
  kFieldLimitExceeded,
};

std::string_view to_string(DecodeStatus status);

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODE_STATUS_HPP_
