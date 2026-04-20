#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__MESSAGE_ENCODER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__MESSAGE_ENCODER_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "universal_protocol_runtime/compiler/compiled_protocol.hpp"
#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/core/types.hpp"
#include "universal_protocol_runtime/encoder/encode_status.hpp"

namespace universal_protocol_runtime {

/**
 * @brief MessageBuilder is a zero-copy encoder for a single message instance. It enforces correct field ordering
 * and handles fixed-value fields and checksums automatically.
 * Usage pattern:
 *   auto builder = encoder.build("Order", output_buffer);
 *   builder->set_unsigned(Order::Fields::kMessageType, 1);
 *   builder->set_unsigned(Order::Fields::kPayloadLength, payload.size());
 *   builder->set_bytes(Order::Fields::kPayload, payload);
 *   std::size_t written = 0;
 *   builder->finalize(&written);  // computes checksums
 * Rules:
 *   Fields must be set in ascending FieldId (= wire) order.
 *   Fixed-value fields (has_expected_unsigned) are written automatically
 *   when the cursor advances past them; you may still set them explicitly
 *   if you want, but the value must match the schema constraint.
 *   Checksum fields are filled with a zero placeholder during the write pass; finalize() computes and overwrites them.
 *   The buffer is written directly - no intermediate copies.
 */
class MessageBuilder {
 public:
  MessageBuilder() = default;
  MessageBuilder(const CompiledProtocol& protocol, const CompiledMessage& layout, MutableByteSpan buffer);

  ~MessageBuilder() noexcept = default;

  // Returns false if the builder was default-constructed or construction failed.
  [[nodiscard]] bool valid() const noexcept { return layout_ != nullptr; }

  EncodeStatus set_unsigned(FieldId id, uint64_t value);
  EncodeStatus set_signed(FieldId id, int64_t value);
  EncodeStatus set_float32(FieldId id, float value);
  EncodeStatus set_float64(FieldId id, double value);

  // The payload is copied into the output buffer.
  EncodeStatus set_bytes(FieldId id, ByteSpan bytes);
  EncodeStatus set_string(FieldId id, std::string_view str);

  // Auto-advance past any remaining fixed-value / checksum fields, compute
  // and write all checksums, and report the total bytes written.
  EncodeStatus finalize(std::size_t* bytes_written = nullptr);

  [[nodiscard]] bool finalized() const noexcept { return finalized_; }

  // Non-owning view over the bytes that have been committed so far.
  [[nodiscard]] ByteSpan view() const noexcept;

 private:
  // Auto-advance the cursor to target_id, writing any interleaved
  // fixed-value and checksum-placeholder fields.
  EncodeStatus advance_to(FieldId target_id);

  // Write a raw uint64 as the scalar value for field `fid` at the current
  // cursor; record field_starts_/field_ends_ and advance offset_.
  EncodeStatus write_scalar_field(FieldId fid, uint64_t raw_value);

  bool is_checksum_field(FieldId fid) const noexcept { return (checksum_field_mask_ >> fid) & 1U; }

  const CompiledProtocol* protocol_ = nullptr;
  const CompiledMessage* layout_ = nullptr;
  MutableByteSpan buffer_;

  // Per-field byte ranges for checksum anchor resolution.
  std::array<std::size_t, kMaxFieldsPerMessage> field_starts_{};
  std::array<std::size_t, kMaxFieldsPerMessage> field_ends_{};

  // Raw scalar values that were committed; used to determine dynamic field
  // sizes when the builder auto-advances.
  std::array<uint64_t, kMaxFieldsPerMessage> written_scalars_{};

  std::size_t offset_ = 0;
  FieldId next_field_id_ = 0;
  bool finalized_ = false;
  bool failed_ = false;

  // Bitmask of which field IDs correspond to checksum fields.
  uint64_t checksum_field_mask_ = 0;
};

/**
 * @brief ProtocolEncoder is a factory for MessageBuilders. It holds a reference to the compiled protocol and
 * looks up message definitions by name.
 */
class ProtocolEncoder {
 public:
  explicit ProtocolEncoder(const CompiledProtocol& protocol) : protocol_(&protocol) {}

  ~ProtocolEncoder() noexcept = default;

  const CompiledProtocol* protocol() const noexcept { return protocol_; }

  // Create a MessageBuilder targeting `message_name` and writing into `buffer`.
  // Returns std::nullopt if the message is not found in the protocol.
  std::optional<MessageBuilder> build(std::string_view message_name, MutableByteSpan buffer) const;

  const CompiledMessage* find_message(std::string_view message_name) const;

 private:
  const CompiledProtocol* protocol_ = nullptr;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__MESSAGE_ENCODER_HPP_
