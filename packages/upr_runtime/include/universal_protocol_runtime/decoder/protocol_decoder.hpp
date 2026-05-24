#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__PROTOCOL_DECODER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__PROTOCOL_DECODER_HPP_
#include <array>
#include <optional>
#include <string_view>

#include "universal_protocol_runtime/compiler/compiled_protocol.hpp"
#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/decoder/decode_status.hpp"
#include "universal_protocol_runtime/decoder/decoded_message.hpp"

namespace universal_protocol_runtime {

struct DecodeFieldMask {
  std::array<bool, kMaxFieldsPerMessage> selected_fields{};

  [[nodiscard]] constexpr bool selects_all() const noexcept {
    for (const bool selected : selected_fields) {
      if (!selected) {
        return false;
      }
    }
    return true;
  }
};

struct DecodePlan {
  const CompiledProtocol* protocol = nullptr;
  const CompiledMessage* layout = nullptr;
  DecodeFieldMask field_mask{};
  bool has_field_mask = false;

  [[nodiscard]] bool valid() const noexcept { return protocol != nullptr && layout != nullptr; }
};

class ProtocolDecoder {
 public:
  explicit ProtocolDecoder(const CompiledProtocol& protocol) : protocol_(&protocol) {}

  ~ProtocolDecoder() noexcept = default;

  constexpr const CompiledProtocol* protocol() const noexcept { return protocol_; }

  DecodeStatus decode_any(ByteSpan frame, DecodedMessage* message) const;

  DecodeStatus decode_as(std::string_view message_name, ByteSpan frame, DecodedMessage* message) const;
  DecodeStatus decode_as(const CompiledMessage& compiled_message, ByteSpan frame, DecodedMessage* message) const;
  DecodeStatus decode_as(std::string_view message_name,
                         ByteSpan frame,
                         DecodedMessage* message,
                         const DecodeFieldMask& field_mask) const;
  DecodeStatus decode_as(const CompiledMessage& compiled_message,
                         ByteSpan frame,
                         DecodedMessage* message,
                         const DecodeFieldMask& field_mask) const;
  DecodeStatus decode_with_plan(const DecodePlan& plan, ByteSpan frame, DecodedMessage* message) const;
  std::optional<DecodePlan> make_plan(std::string_view message_name) const;
  std::optional<DecodePlan> make_plan(std::string_view message_name, const DecodeFieldMask& field_mask) const;

  static DecodeFieldMask select_all_fields() {
    DecodeFieldMask mask;
    mask.selected_fields.fill(true);
    return mask;
  }

 private:
  DecodeStatus decode_message(const CompiledMessage& compiled_message,
                              ByteSpan frame,
                              DecodedMessage* message,
                              const DecodeFieldMask* field_mask = nullptr) const;

  const CompiledProtocol* protocol_ = nullptr;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__PROTOCOL_DECODER_HPP_
