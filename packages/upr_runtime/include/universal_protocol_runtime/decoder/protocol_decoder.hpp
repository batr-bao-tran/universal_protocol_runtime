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

/**
 * @brief Selects which fields should be materialized during decode.
 */
struct DecodeFieldMask {
  std::array<bool, kMaxFieldsPerMessage> selected_fields{};

  /**
   * @brief Checks whether every field is selected.
   * @return `true` when the mask selects all fields.
   */
  [[nodiscard]] constexpr bool selects_all() const noexcept {
    for (const bool selected : selected_fields) {
      if (!selected) {
        return false;
      }
    }
    return true;
  }
};

/**
 * @brief Reusable decode plan for one compiled message and optional field mask.
 */
struct DecodePlan {
  const CompiledProtocol* protocol = nullptr;
  const CompiledMessage* layout = nullptr;
  DecodeFieldMask field_mask{};
  bool has_field_mask = false;

  /**
   * @brief Checks whether the plan references a valid protocol and message.
   * @return `true` when the plan can be used for decode.
   */
  [[nodiscard]] bool valid() const noexcept { return protocol != nullptr && layout != nullptr; }
};

/**
 * @brief Decoder for compiled protocol messages.
 */
class ProtocolDecoder {
 public:
  /**
   * @brief Constructs a decoder for one compiled protocol.
   * @param protocol Compiled protocol metadata.
   * @return No return value.
   */
  explicit ProtocolDecoder(const CompiledProtocol& protocol) : protocol_(&protocol) {}

  /**
   * @brief Destroys the decoder.
   * @return No return value.
   */
  ~ProtocolDecoder() noexcept = default;

  /**
   * @brief Returns the compiled protocol used by this decoder.
   * @return Pointer to the compiled protocol metadata.
   */
  constexpr const CompiledProtocol* protocol() const noexcept { return protocol_; }

  /**
   * @brief Decodes a frame against the best matching compiled message.
   * @param frame Framed bytes to decode.
   * @param message Destination decoded message view.
   * @return Decode status for the operation.
   */
  DecodeStatus decode_any(ByteSpan frame, DecodedMessage* message) const;

  /**
   * @brief Decodes a frame as a named message.
   * @param message_name Target message name.
   * @param frame Framed bytes to decode.
   * @param message Destination decoded message view.
   * @return Decode status for the operation.
   */
  DecodeStatus decode_as(std::string_view message_name, ByteSpan frame, DecodedMessage* message) const;
  /**
   * @brief Decodes a frame as an already resolved compiled message.
   * @param compiled_message Target compiled message layout.
   * @param frame Framed bytes to decode.
   * @param message Destination decoded message view.
   * @return Decode status for the operation.
   */
  DecodeStatus decode_as(const CompiledMessage& compiled_message, ByteSpan frame, DecodedMessage* message) const;
  /**
   * @brief Decodes a named message using a field-selection mask.
   * @param message_name Target message name.
   * @param frame Framed bytes to decode.
   * @param message Destination decoded message view.
   * @param field_mask Field-selection mask.
   * @return Decode status for the operation.
   */
  DecodeStatus decode_as(std::string_view message_name,
                         ByteSpan frame,
                         DecodedMessage* message,
                         const DecodeFieldMask& field_mask) const;
  /**
   * @brief Decodes a compiled message using a field-selection mask.
   * @param compiled_message Target compiled message layout.
   * @param frame Framed bytes to decode.
   * @param message Destination decoded message view.
   * @param field_mask Field-selection mask.
   * @return Decode status for the operation.
   */
  DecodeStatus decode_as(const CompiledMessage& compiled_message,
                         ByteSpan frame,
                         DecodedMessage* message,
                         const DecodeFieldMask& field_mask) const;
  /**
   * @brief Decodes a frame using a previously prepared decode plan.
   * @param plan Reusable decode plan.
   * @param frame Framed bytes to decode.
   * @param message Destination decoded message view.
   * @return Decode status for the operation.
   */
  DecodeStatus decode_with_plan(const DecodePlan& plan, ByteSpan frame, DecodedMessage* message) const;
  /**
   * @brief Builds a reusable decode plan for a named message.
   * @param message_name Target message name.
   * @return Decode plan when the message exists.
   */
  std::optional<DecodePlan> make_plan(std::string_view message_name) const;
  /**
   * @brief Builds a reusable masked decode plan for a named message.
   * @param message_name Target message name.
   * @param field_mask Field-selection mask to embed in the plan.
   * @return Decode plan when the message exists.
   */
  std::optional<DecodePlan> make_plan(std::string_view message_name, const DecodeFieldMask& field_mask) const;

  /**
   * @brief Creates a mask with every field selected.
   * @return Field mask that selects all fields.
   */
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
