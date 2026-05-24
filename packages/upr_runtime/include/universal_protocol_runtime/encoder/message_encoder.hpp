#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__MESSAGE_ENCODER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__MESSAGE_ENCODER_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "universal_protocol_runtime/compiler/compiled_protocol.hpp"
#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/core/types.hpp"
#include "universal_protocol_runtime/encoder/encode_status.hpp"

namespace universal_protocol_runtime {

struct EncodePlan {
  const CompiledProtocol* protocol = nullptr;
  const CompiledMessage* layout = nullptr;

  /**
   * @brief Reports whether the plan references a compiled message layout.
   * @return `true` when the plan can be used for encode.
   */
  [[nodiscard]] bool valid() const noexcept { return protocol != nullptr && layout != nullptr; }
};

/**
 * @brief One contiguous encoded byte span produced by segmented encode.
 */
struct EncodedSegment {
  ByteSpan bytes;
};

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
  /**
   * @brief Constructs a contiguous message builder for one compiled layout.
   * @param protocol Compiled protocol metadata.
   * @param layout Compiled message layout to encode.
   * @param buffer Output buffer to fill.
   */
  MessageBuilder(const CompiledProtocol& protocol, const CompiledMessage& layout, MutableByteSpan buffer);

  /**
   * @brief Destroys the message builder.
   * @return No return value.
   */
  ~MessageBuilder() noexcept = default;

  // Returns false if the builder was default-constructed or construction failed.
  /**
   * @brief Reports whether the builder is ready to encode.
   * @return `true` when the builder references a valid layout and buffer.
   */
  [[nodiscard]] bool valid() const noexcept { return layout_ != nullptr; }

  /**
   * @brief Encodes an unsigned scalar field.
   * @param id Field identifier to write.
   * @param value Raw unsigned value to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_unsigned(FieldId id, uint64_t value);
  /**
   * @brief Encodes a signed scalar field.
   * @param id Field identifier to write.
   * @param value Signed value to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_signed(FieldId id, int64_t value);
  /**
   * @brief Encodes a `float32` field.
   * @param id Field identifier to write.
   * @param value Floating-point value to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_float32(FieldId id, float value);
  /**
   * @brief Encodes a `float64` field.
   * @param id Field identifier to write.
   * @param value Floating-point value to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_float64(FieldId id, double value);

  // The payload is copied into the output buffer.
  /**
   * @brief Encodes a bytes field by copying its payload into the output buffer.
   * @param id Field identifier to write.
   * @param bytes Payload bytes to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_bytes(FieldId id, ByteSpan bytes);
  /**
   * @brief Encodes a string field by copying its payload into the output buffer.
   * @param id Field identifier to write.
   * @param str String payload to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_string(FieldId id, std::string_view str);

  // Auto-advance past any remaining fixed-value / checksum fields, compute
  // and write all checksums, and report the total bytes written.
  /**
   * @brief Finalizes the message, computes checksums, and reports total bytes written.
   * @param bytes_written Optional output for total encoded size.
   * @return Encode status for finalization.
   */
  EncodeStatus finalize(std::size_t* bytes_written = nullptr);

  /**
   * @brief Reports whether the builder has been finalized.
   * @return `true` when no more fields can be written.
   */
  [[nodiscard]] bool finalized() const noexcept { return finalized_; }

  // Non-owning view over the bytes that have been committed so far.
  /**
   * @brief Returns the bytes committed so far.
   * @return Borrowed byte span over the encoded prefix.
   */
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

class SegmentedMessageBuilder {
 public:
  SegmentedMessageBuilder() = default;
  /**
   * @brief Constructs a segmented message builder for one compiled layout.
   * @param protocol Compiled protocol metadata.
   * @param layout Compiled message layout to encode.
   * @param scratch_buffer Scratch storage for owned segments.
   */
  SegmentedMessageBuilder(const CompiledProtocol& protocol,
                          const CompiledMessage& layout,
                          MutableByteSpan scratch_buffer);

  /**
   * @brief Destroys the segmented message builder.
   * @return No return value.
   */
  ~SegmentedMessageBuilder() noexcept = default;

  /**
   * @brief Reports whether the builder is ready to encode.
   * @return `true` when the builder references a valid layout and scratch buffer.
   */
  [[nodiscard]] bool valid() const noexcept { return layout_ != nullptr; }

  /**
   * @brief Encodes an unsigned scalar field into segmented output.
   * @param id Field identifier to write.
   * @param value Raw unsigned value to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_unsigned(FieldId id, uint64_t value);
  /**
   * @brief Encodes a signed scalar field into segmented output.
   * @param id Field identifier to write.
   * @param value Signed value to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_signed(FieldId id, int64_t value);
  /**
   * @brief Encodes a `float32` field into segmented output.
   * @param id Field identifier to write.
   * @param value Floating-point value to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_float32(FieldId id, float value);
  /**
   * @brief Encodes a `float64` field into segmented output.
   * @param id Field identifier to write.
   * @param value Floating-point value to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_float64(FieldId id, double value);
  /**
   * @brief Encodes a bytes field by copying it into owned segmented storage.
   * @param id Field identifier to write.
   * @param bytes Payload bytes to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_bytes(FieldId id, ByteSpan bytes);
  /**
   * @brief Encodes a string field by copying it into owned segmented storage.
   * @param id Field identifier to write.
   * @param str String payload to encode.
   * @return Encode status for the write.
   */
  EncodeStatus set_string(FieldId id, std::string_view str);
  /**
   * @brief Attaches an external bytes payload without copying it.
   * @param id Field identifier to write.
   * @param bytes Borrowed payload bytes to attach.
   * @return Encode status for the write.
   */
  EncodeStatus attach_bytes(FieldId id, ByteSpan bytes);
  /**
   * @brief Attaches an external string payload without copying it.
   * @param id Field identifier to write.
   * @param str Borrowed string payload to attach.
   * @return Encode status for the write.
   */
  EncodeStatus attach_string(FieldId id, std::string_view str);
  /**
   * @brief Finalizes segmented output, computes checksums, and reports total bytes written.
   * @param bytes_written Optional output for total encoded size.
   * @return Encode status for finalization.
   */
  EncodeStatus finalize(std::size_t* bytes_written = nullptr);
  /**
   * @brief Copies segmented output into one contiguous destination buffer.
   * @param output Destination buffer to fill.
   * @param bytes_written Optional output for total encoded size.
   * @return Encode status for the copy.
   */
  EncodeStatus copy_to(MutableByteSpan output, std::size_t* bytes_written = nullptr) const;

  /**
   * @brief Reports whether the builder has been finalized.
   * @return `true` when no more fields can be written.
   */
  [[nodiscard]] bool finalized() const noexcept { return finalized_; }
  /**
   * @brief Returns the encoded segment list after segmented writes.
   * @return Borrowed span of encoded segments.
   */
  [[nodiscard]] std::span<const EncodedSegment> segments() const noexcept { return {segments_.data(), segment_count_}; }

 private:
  static constexpr size_t kMaxSegments = (kMaxFieldsPerMessage * 2U) + 1U;

  EncodeStatus advance_to(FieldId target_id);
  EncodeStatus append_owned_bytes(FieldId id, ByteSpan bytes);
  EncodeStatus append_borrowed_bytes(FieldId id, ByteSpan bytes);
  EncodeStatus append_filled_bytes(FieldId id, size_t size, std::byte fill_byte);
  EncodeStatus append_scalar(FieldId id, uint64_t raw_value);
  EncodeStatus emit_alignment_gap(FieldId next_id);

  bool is_checksum_field(FieldId fid) const noexcept { return (checksum_field_mask_ >> fid) & 1U; }

  const CompiledProtocol* protocol_ = nullptr;
  const CompiledMessage* layout_ = nullptr;
  MutableByteSpan scratch_buffer_;
  std::array<EncodedSegment, kMaxSegments> segments_{};
  std::array<std::size_t, kMaxSegments> segment_offsets_{};
  std::array<std::size_t, kMaxSegments> segment_lengths_{};
  std::array<std::size_t, kMaxFieldsPerMessage> field_starts_{};
  std::array<std::size_t, kMaxFieldsPerMessage> field_ends_{};
  std::array<std::size_t, kMaxFieldsPerMessage> field_segment_indices_{};
  std::array<uint64_t, kMaxFieldsPerMessage> written_scalars_{};
  std::size_t scratch_offset_ = 0;
  std::size_t total_size_ = 0;
  size_t segment_count_ = 0;
  FieldId next_field_id_ = 0;
  bool finalized_ = false;
  bool failed_ = false;
  uint64_t checksum_field_mask_ = 0;
};

/**
 * @brief ProtocolEncoder is a factory for MessageBuilders. It holds a reference to the compiled protocol and
 * looks up message definitions by name.
 */
class ProtocolEncoder {
 public:
  /**
   * @brief Constructs an encoder bound to one compiled protocol.
   * @param protocol Compiled protocol metadata used for message lookup.
   */
  explicit ProtocolEncoder(const CompiledProtocol& protocol) : protocol_(&protocol) {}

  /**
   * @brief Destroys the protocol encoder.
   * @return No return value.
   */
  ~ProtocolEncoder() noexcept = default;

  /**
   * @brief Returns the compiled protocol backing this encoder.
   * @return Pointer to compiled protocol metadata.
   */
  const CompiledProtocol* protocol() const noexcept { return protocol_; }

  // Create a MessageBuilder targeting `message_name` and writing into `buffer`.
  // Returns std::nullopt if the message is not found in the protocol.
  /**
   * @brief Builds a contiguous message builder by message name.
   * @param message_name Message name to encode.
   * @param buffer Output buffer to fill.
   * @return Message builder when the message exists.
   */
  std::optional<MessageBuilder> build(std::string_view message_name, MutableByteSpan buffer) const;
  /**
   * @brief Builds a contiguous message builder from a cached encode plan.
   * @param plan Cached encode plan.
   * @param buffer Output buffer to fill.
   * @return Message builder when the plan is valid.
   */
  std::optional<MessageBuilder> build(const EncodePlan& plan, MutableByteSpan buffer) const;
  /**
   * @brief Builds a segmented message builder by message name.
   * @param message_name Message name to encode.
   * @param scratch_buffer Scratch storage for owned segments.
   * @return Segmented message builder when the message exists.
   */
  std::optional<SegmentedMessageBuilder> build_segmented(std::string_view message_name,
                                                         MutableByteSpan scratch_buffer) const;
  /**
   * @brief Builds a segmented message builder from a cached encode plan.
   * @param plan Cached encode plan.
   * @param scratch_buffer Scratch storage for owned segments.
   * @return Segmented message builder when the plan is valid.
   */
  std::optional<SegmentedMessageBuilder> build_segmented(const EncodePlan& plan, MutableByteSpan scratch_buffer) const;
  /**
   * @brief Creates a reusable encode plan for one message.
   * @param message_name Message name to resolve.
   * @return Encode plan when the message exists.
   */
  std::optional<EncodePlan> make_plan(std::string_view message_name) const;

  /**
   * @brief Finds a compiled message by name.
   * @param message_name Message name to resolve.
   * @return Pointer to the compiled message layout when found.
   */
  const CompiledMessage* find_message(std::string_view message_name) const;

 private:
  const CompiledProtocol* protocol_ = nullptr;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__MESSAGE_ENCODER_HPP_
