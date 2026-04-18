#include "universal_protocol_runtime/decoder/protocol_decoder.hpp"

#include <cstring>

#include "universal_protocol_runtime/core/compiler_hints.hpp"

namespace universal_protocol_runtime {
namespace {

bool frame_matches_dispatch_prefix(const CompiledMessage& compiled_message, ByteSpan frame) {
  const std::span<const std::byte> dispatch_prefix = compiled_message.dispatch_prefix();
  if (UPR_LIKELY(dispatch_prefix.empty())) {
    return true;
  }
  return frame.size() >= dispatch_prefix.size() &&
         std::memcmp(dispatch_prefix.data(), frame.data(), dispatch_prefix.size()) == 0;
}

DecodeStatus try_decode_candidates(const ProtocolDecoder& decoder,
                                   const CompiledProtocol& protocol,
                                   std::span<const size_t> candidate_ids,
                                   ByteSpan frame,
                                   DecodedMessage* message,
                                   DecodeStatus best_failure,
                                   bool* attempted_candidate) {
  for (const size_t candidate_id : candidate_ids) {
    const CompiledMessage& compiled_message = protocol.messages()[candidate_id];
    if (UPR_UNLIKELY(!frame_matches_dispatch_prefix(compiled_message, frame))) {
      continue;
    }
    *attempted_candidate = true;
    const DecodeStatus status = decoder.decode_as(compiled_message, frame, message);
    if (UPR_LIKELY(status == DecodeStatus::kOk)) {
      return status;
    }
    if (best_failure == DecodeStatus::kMessageNotFound || best_failure == DecodeStatus::kSchemaMismatch) {
      best_failure = status;
    }
  }
  return best_failure;
}

}  // namespace

DecodeStatus ProtocolDecoder::decode_any(ByteSpan frame, DecodedMessage* message) const {
  DecodeStatus best_failure = DecodeStatus::kMessageNotFound;
  bool attempted_candidate = false;
  best_failure = try_decode_candidates(
      *this, *protocol_, protocol_->dispatch_candidate_ids(frame), frame, message, best_failure, &attempted_candidate);
  if (UPR_LIKELY(best_failure == DecodeStatus::kOk)) {
    return best_failure;
  }
  best_failure = try_decode_candidates(
      *this, *protocol_, protocol_->fallback_candidate_ids(), frame, message, best_failure, &attempted_candidate);
  return attempted_candidate ? best_failure : DecodeStatus::kMessageNotFound;
}

DecodeStatus ProtocolDecoder::decode_as(std::string_view message_name, ByteSpan frame, DecodedMessage* message) const {
  const CompiledMessage* compiled_message = protocol_->find_message(message_name);
  if (compiled_message == nullptr) {
    return DecodeStatus::kMessageNotFound;
  }
  return decode_message(*compiled_message, frame, message);
}

DecodeStatus ProtocolDecoder::decode_as(const CompiledMessage& compiled_message,
                                        ByteSpan frame,
                                        DecodedMessage* message) const {
  return decode_message(compiled_message, frame, message);
}

DecodeStatus ProtocolDecoder::decode_message(const CompiledMessage& compiled_message,
                                             ByteSpan frame,
                                             DecodedMessage* message) const {
  return message->assign_from_layout(*protocol_, compiled_message, frame, nullptr);
}

}  // namespace universal_protocol_runtime
