#include "universal_protocol_runtime/decoder/protocol_decoder.hpp"

#include <algorithm>
#include <cstring>

#include "universal_protocol_runtime/core/unreachable.hpp"

namespace universal_protocol_runtime {
namespace {

bool frame_matches_dispatch_prefix(const CompiledMessage& compiled_message, ByteSpan frame) {
  const std::span<const std::byte> dispatch_prefix = compiled_message.dispatch_prefix();
  if (dispatch_prefix.empty()) {
    return true;
  }
  return frame.size() >= dispatch_prefix.size() &&
         std::memcmp(dispatch_prefix.data(), frame.data(), dispatch_prefix.size()) == 0;
}

}  // namespace

std::string_view to_string(DecodeStatus status) {
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
  unreachable();
}

DecodeStatus ProtocolDecoder::decode_any(ByteSpan frame, DecodedMessage* message) const {
  if (protocol_ == nullptr) {
    return DecodeStatus::kInvalidData;
  }
  DecodeStatus best_failure = DecodeStatus::kMessageNotFound;
  bool attempted_candidate = false;
  for (const CompiledMessage& compiled_message : protocol_->messages()) {
    if (!frame_matches_dispatch_prefix(compiled_message, frame)) {
      continue;
    }
    attempted_candidate = true;
    const DecodeStatus status = decode_message(compiled_message, frame, message);
    if (status == DecodeStatus::kOk) {
      return status;
    }
    if (best_failure == DecodeStatus::kMessageNotFound || best_failure == DecodeStatus::kSchemaMismatch) {
      best_failure = status;
    }
  }
  return attempted_candidate ? best_failure : DecodeStatus::kMessageNotFound;
}

DecodeStatus ProtocolDecoder::decode_as(std::string_view message_name, ByteSpan frame, DecodedMessage* message) const {
  if (protocol_ == nullptr) {
    return DecodeStatus::kInvalidData;
  }
  const CompiledMessage* compiled_message = protocol_->find_message(message_name);
  if (compiled_message == nullptr) {
    return DecodeStatus::kMessageNotFound;
  }
  return decode_message(*compiled_message, frame, message);
}

DecodeStatus ProtocolDecoder::decode_message(const CompiledMessage& compiled_message,
                                             ByteSpan frame,
                                             DecodedMessage* message) const {
  return message->assign_from_layout(*protocol_, compiled_message, frame, nullptr);
}

}  // namespace universal_protocol_runtime
