#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__PROTOCOL_DECODER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__PROTOCOL_DECODER_HPP_
#include <string_view>

#include "universal_protocol_runtime/compiler/compiled_protocol.hpp"
#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/decoder/decode_status.hpp"
#include "universal_protocol_runtime/decoder/decoded_message.hpp"

namespace universal_protocol_runtime {

class ProtocolDecoder {
 public:
  explicit ProtocolDecoder(const CompiledProtocol& protocol) : protocol_(&protocol) {}

  ~ProtocolDecoder() noexcept = default;

  DecodeStatus decode_any(ByteSpan frame, DecodedMessage* message) const;

  DecodeStatus decode_as(std::string_view message_name, ByteSpan frame, DecodedMessage* message) const;

 private:
  DecodeStatus decode_message(const CompiledMessage& compiled_message, ByteSpan frame, DecodedMessage* message) const;

  const CompiledProtocol* protocol_ = nullptr;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__PROTOCOL_DECODER_HPP_
