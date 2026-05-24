#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_COMPILER_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_COMPILER__COMPILED_PROTOCOL_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_COMPILER_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_COMPILER__COMPILED_PROTOCOL_HPP_
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "universal_protocol_runtime/compiler/checksum_registry.hpp"
#include "universal_protocol_runtime/core/types.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Transparent string hash for heterogeneous lookup in name maps.
 */
struct TransparentStringHash {
  using is_transparent = void;

  size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }

  size_t operator()(const std::string& value) const noexcept { return operator()(std::string_view(value)); }

  size_t operator()(const char* value) const noexcept { return operator()(std::string_view(value)); }
};

enum class ChecksumAnchorKind {
  kFrameStart,
  kFrameEnd,
  kFieldStart,
  kFieldEnd,
  kBeforeSelf,
  kAfterSelf,
};

/**
 * @brief Resolved checksum anchor used by compiled runtime metadata.
 */
struct CompiledChecksumAnchor {
  ChecksumAnchorKind kind = ChecksumAnchorKind::kFrameStart;
  FieldId field_id = 0;
};

/**
 * @brief Compiled bit-field metadata.
 */
struct CompiledBitField {
  BitFieldId id = 0;
  std::string name;
  FieldId container_field_id = 0;
  uint8_t shift_bits = 0;
  uint8_t width_bits = 0;
  uint64_t mask = 0;
  bool is_signed = false;
  std::vector<EnumValueDefinition> enum_values;
};

/**
 * @brief Compiled checksum metadata.
 */
struct CompiledChecksum {
  enum class BuiltinKind {
    kCustom,
    kXor8,
    kSum16,
    kCrc16Ccitt,
    kCrc32,
    kCrc32c,
  };

  FieldId field_id = 0;
  uint8_t result_width_bytes = 0;
  ChecksumFunction function = nullptr;
  std::string algorithm_name;
  BuiltinKind builtin_kind = BuiltinKind::kCustom;
  CompiledChecksumAnchor from;
  CompiledChecksumAnchor to;
};

/**
 * @brief One compiled tagged-variant case.
 */
struct CompiledVariantCase {
  uint64_t tag_value = 0;
  size_t struct_id = 0;
};

enum class CompiledValidationOperator {
  kEq,
  kNe,
  kLt,
  kLe,
  kGt,
  kGe,
};

/**
 * @brief Compiled validation rule metadata.
 */
struct CompiledValidationRule {
  FieldId field_id = 0;
  CompiledValidationOperator op = CompiledValidationOperator::kEq;
  FieldId other_field_id = 0;
  bool compare_to_field = false;
  uint64_t value = 0;
  uint64_t multiplier = 1;
  bool has_when = false;
  FieldId when_field_id = 0;
  uint64_t when_equals = 0;
};

/**
 * @brief Compiled field metadata used by decoders and encoders.
 */
struct CompiledField {
  static constexpr uint32_t kVariantLookupMissing = std::numeric_limits<uint32_t>::max();

  FieldId id = 0;
  std::string name;
  FieldKind kind = FieldKind::kUnsigned;
  uint8_t width_bytes = 0;
  ByteOrder byte_order = ByteOrder::kLittleEndian;
  StringEncoding string_encoding = StringEncoding::kAscii;
  size_t fixed_size = 0;
  bool dynamic_size = false;
  FieldId size_from_field = 0;
  size_t struct_id = 0;
  size_t alignment = 1;
  bool is_reserved = false;
  uint8_t reserved_fill_byte = 0;
  size_t element_minimum_size = 0;
  size_t fixed_count = 0;
  bool dynamic_count = false;
  FieldId count_from_field = 0;
  bool has_condition = false;
  FieldId condition_field = 0;
  uint64_t condition_equals = 0;
  bool has_presence = false;
  FieldId presence_field = 0;
  uint8_t presence_bit = 0;
  FieldId tag_from_field = 0;
  std::vector<CompiledVariantCase> variant_cases;
  uint64_t variant_lookup_base = 0;
  std::vector<uint32_t> variant_lookup_indices;
  bool has_expected_unsigned = false;
  uint64_t expected_unsigned = 0;
  std::vector<EnumValueDefinition> enum_values;

  /**
   * @brief Checks whether the field is represented by a scalar wire value.
   * @return `true` when the field kind is scalar.
   */
  constexpr bool is_scalar() const noexcept {
    return kind == FieldKind::kUnsigned || kind == FieldKind::kSigned || kind == FieldKind::kFloat32 ||
           kind == FieldKind::kFloat64 || kind == FieldKind::kEnum;
  }

  /**
   * @brief Checks whether the field may be omitted at runtime.
   * @return `true` when the field has a condition or presence bit.
   */
  constexpr bool is_conditionally_present() const noexcept { return has_condition || has_presence; }

  /**
   * @brief Returns the minimum byte contribution of the field.
   * @return Minimum bytes this field contributes to message size.
   */
  constexpr size_t minimum_size_contribution() const noexcept {
    if (is_conditionally_present()) {
      return 0;
    }
    if (kind == FieldKind::kCollection || kind == FieldKind::kVariant) {
      return fixed_size;
    }
    if (dynamic_size) {
      return 0;
    }
    if (kind == FieldKind::kBytes || kind == FieldKind::kString || kind == FieldKind::kStruct) {
      return fixed_size;
    }
    return width_bytes;
  }
};

/**
 * @brief Compiled message or struct layout metadata.
 */
class CompiledMessage {
 public:
  CompiledMessage() = default;
  ~CompiledMessage() noexcept = default;

  CompiledMessage(std::string name,
                  std::vector<CompiledField> fields,
                  std::vector<CompiledBitField> bit_fields,
                  std::vector<CompiledChecksum> checksums,
                  std::vector<CompiledValidationRule> validations,
                  size_t minimum_size,
                  bool has_fixed_size,
                  bool allow_trailing_bytes,
                  std::vector<std::byte> dispatch_prefix = {});
  CompiledMessage(std::string name,
                  std::vector<CompiledField> fields,
                  std::vector<CompiledBitField> bit_fields,
                  std::vector<CompiledChecksum> checksums,
                  std::vector<CompiledValidationRule> validations,
                  size_t minimum_size,
                  bool allow_trailing_bytes,
                  std::vector<std::byte> dispatch_prefix = {});
  CompiledMessage(std::string name,
                  std::vector<CompiledField> fields,
                  std::vector<CompiledBitField> bit_fields,
                  std::vector<CompiledChecksum> checksums,
                  size_t minimum_size,
                  bool has_fixed_size,
                  bool allow_trailing_bytes,
                  std::vector<std::byte> dispatch_prefix = {});
  CompiledMessage(std::string name,
                  std::vector<CompiledField> fields,
                  std::vector<CompiledBitField> bit_fields,
                  std::vector<CompiledChecksum> checksums,
                  size_t minimum_size,
                  bool allow_trailing_bytes,
                  std::vector<std::byte> dispatch_prefix = {});

  /**
   * @brief Returns the compiled layout name.
   * @return Layout name.
   */
  std::string_view name() const { return name_; }

  /**
   * @brief Returns the compiled field metadata list.
   * @return Field metadata list.
   */
  const std::vector<CompiledField>& fields() const { return fields_; }

  /**
   * @brief Returns the minimum valid byte size for the layout.
   * @return Minimum valid byte size.
   */
  size_t minimum_size() const { return minimum_size_; }

  /**
   * @brief Reports whether the layout size is fixed.
   * @return `true` when the layout has a fixed wire size.
   */
  bool has_fixed_size() const { return has_fixed_size_; }

  /**
   * @brief Reports whether extra trailing bytes are allowed.
   * @return `true` when trailing bytes are accepted.
   */
  bool allow_trailing_bytes() const { return allow_trailing_bytes_; }

  /**
   * @brief Returns the dispatch prefix used for fast message selection.
   * @return Dispatch prefix bytes.
   */
  std::span<const std::byte> dispatch_prefix() const { return dispatch_prefix_; }

  /**
   * @brief Returns compiled bit-field metadata.
   * @return Bit-field metadata list.
   */
  const std::vector<CompiledBitField>& bit_fields() const { return bit_fields_; }

  /**
   * @brief Returns compiled checksum metadata.
   * @return Checksum metadata list.
   */
  const std::vector<CompiledChecksum>& checksums() const { return checksums_; }

  /**
   * @brief Returns compiled validation metadata.
   * @return Validation metadata list.
   */
  const std::vector<CompiledValidationRule>& validations() const { return validations_; }

  /**
   * @brief Resolves a field name to its field identifier.
   * @param field_name Field name to resolve.
   * @return Field identifier when the field exists.
   */
  std::optional<FieldId> find_field(std::string_view field_name) const;

  /**
   * @brief Resolves a bit-field name to its bit-field identifier.
   * @param bit_field_name Bit-field name to resolve.
   * @return Bit-field identifier when the bit field exists.
   */
  std::optional<BitFieldId> find_bit_field(std::string_view bit_field_name) const;

 private:
  std::string name_;
  std::vector<CompiledField> fields_;
  std::vector<CompiledBitField> bit_fields_;
  std::vector<CompiledChecksum> checksums_;
  std::vector<CompiledValidationRule> validations_;
  size_t minimum_size_ = 0;
  bool has_fixed_size_ = false;
  bool allow_trailing_bytes_ = false;
  std::vector<std::byte> dispatch_prefix_;
  std::unordered_map<std::string, FieldId, TransparentStringHash, std::equal_to<>> field_ids_;
  std::unordered_map<std::string, BitFieldId, TransparentStringHash, std::equal_to<>> bit_field_ids_;
};

/**
 * @brief Compiled protocol metadata used by runtime components.
 */
class CompiledProtocol {
 public:
  CompiledProtocol() = default;
  ~CompiledProtocol() noexcept = default;

  CompiledProtocol(std::string name,
                   uint64_t fingerprint,
                   std::vector<CompiledMessage> structs,
                   std::vector<CompiledMessage> messages);

  /**
   * @brief Returns the protocol name.
   * @return Protocol name.
   */
  std::string_view name() const { return name_; }

  /**
   * @brief Returns the protocol fingerprint.
   * @return Stable protocol fingerprint.
   */
  uint64_t fingerprint() const { return fingerprint_; }

  /**
   * @brief Returns compiled struct layouts.
   * @return Struct metadata list.
   */
  const std::vector<CompiledMessage>& structs() const { return structs_; }

  /**
   * @brief Returns compiled message layouts.
   * @return Message metadata list.
   */
  const std::vector<CompiledMessage>& messages() const { return messages_; }

  /**
   * @brief Finds a compiled message by name.
   * @param message_name Message name to resolve.
   * @return Pointer to the compiled message when it exists.
   */
  const CompiledMessage* find_message(std::string_view message_name) const;

  /**
   * @brief Finds a compiled struct by name.
   * @param struct_name Struct name to resolve.
   * @return Pointer to the compiled struct when it exists.
   */
  const CompiledMessage* find_struct(std::string_view struct_name) const;

  /**
   * @brief Resolves a compiled struct by numeric identifier.
   * @param struct_id Struct identifier.
   * @return Pointer to the compiled struct when the id is valid.
   */
  const CompiledMessage* struct_by_id(size_t struct_id) const;

  /**
   * @brief Returns dispatch candidates for a frame prefix.
   * @param frame Frame bytes to inspect.
   * @return Span of candidate message identifiers.
   */
  std::span<const size_t> dispatch_candidate_ids(ByteSpan frame) const noexcept;

  /**
   * @brief Returns the fallback message candidate list.
   * @return Span of fallback message identifiers.
   */
  std::span<const size_t> fallback_candidate_ids() const noexcept { return fallback_message_ids_; }

 private:
  static constexpr size_t kDispatchTableSize = 256U;

  std::string name_;
  uint64_t fingerprint_ = 0;
  std::vector<CompiledMessage> structs_;
  std::vector<CompiledMessage> messages_;
  std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> struct_ids_;
  std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> message_ids_;
  std::array<std::vector<size_t>, kDispatchTableSize> dispatch_message_ids_;
  std::vector<size_t> fallback_message_ids_;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_COMPILER__COMPILED_PROTOCOL_HPP_
