#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_AUTHORING_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__PROTOCOL_DEFINITION_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_AUTHORING_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__PROTOCOL_DEFINITION_HPP_
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_protocol_runtime/core/types.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Workspace-relative or file-relative schema import declaration.
 */
struct ImportDefinition {
  std::string path;
};

/**
 * @brief Authoring-time bit-field declaration nested inside a scalar field.
 */
struct BitFieldDefinition {
  std::string name;
  uint8_t offset_bits = 0;
  uint8_t width_bits = 0;
  bool is_signed = false;
  std::vector<EnumValueDefinition> enum_values;
};

/**
 * @brief Authoring-time checksum declaration for one field.
 */
struct ChecksumDefinition {
  std::string algorithm;
  std::string from = "frame_start";
  std::string to = "before_self";
};

/**
 * @brief Equality condition used for conditional fields and validations.
 */
struct ConditionDefinition {
  std::string field;
  uint64_t equals_unsigned = 0;
};

/**
 * @brief Presence-bit selector for sparse optional fields.
 */
struct PresenceDefinition {
  std::string field;
  uint8_t bit_index = 0;
};

enum class ValidationOperator {
  kEq,
  kNe,
  kLt,
  kLe,
  kGt,
  kGe,
};

/**
 * @brief Authoring-time validation rule declaration.
 */
struct ValidationRuleDefinition {
  std::string field;
  ValidationOperator op = ValidationOperator::kEq;
  std::string other_field;
  bool compare_to_field = false;
  uint64_t value = 0;
  uint64_t multiplier = 1;
  std::optional<ConditionDefinition> when;
};

/**
 * @brief One tagged variant case mapping.
 */
struct VariantCaseDefinition {
  uint64_t tag_value = 0;
  std::string referenced_type;
};

/**
 * @brief Authoring-time enum declaration.
 */
struct EnumDefinition {
  std::string name;
  uint8_t width_bytes = 0;
  ByteOrder byte_order = ByteOrder::kLittleEndian;
  std::vector<EnumValueDefinition> values;
};

/**
 * @brief Authoring-time field declaration.
 */
struct FieldDefinition {
  std::string name;
  FieldKind kind = FieldKind::kUnsigned;
  uint8_t width_bytes = 0;
  ByteOrder byte_order = ByteOrder::kLittleEndian;
  StringEncoding string_encoding = StringEncoding::kAscii;
  size_t fixed_size = 0;
  std::string size_from_field;
  std::string referenced_type;
  size_t alignment = 1;
  bool is_reserved = false;
  uint8_t reserved_fill_byte = 0;
  size_t fixed_count = 0;
  std::string count_from_field;
  std::string tag_from_field;
  bool has_expected_unsigned = false;
  uint64_t expected_unsigned = 0;
  std::vector<EnumValueDefinition> enum_values;
  std::vector<BitFieldDefinition> bit_fields;
  std::optional<ChecksumDefinition> checksum;
  std::optional<ConditionDefinition> condition;
  std::optional<PresenceDefinition> presence;
  std::vector<VariantCaseDefinition> variant_cases;

  /**
   * @brief Checks whether the field size depends on another field.
   * @return `true` when `size_from_field` is populated.
   */
  bool is_dynamic_size() const { return !size_from_field.empty(); }
  /**
   * @brief Checks whether the collection count depends on another field.
   * @return `true` when `count_from_field` is populated.
   */
  bool is_dynamic_count() const { return !count_from_field.empty(); }
};

/**
 * @brief Authoring-time struct declaration.
 */
struct StructDefinition {
  std::string name;
  std::vector<FieldDefinition> fields;
  std::vector<ValidationRuleDefinition> validations;
};

/**
 * @brief Authoring-time message declaration.
 */
struct MessageDefinition {
  std::string name;
  std::vector<FieldDefinition> fields;
  std::vector<ValidationRuleDefinition> validations;
  bool allow_trailing_bytes = false;
};

/**
 * @brief Root authoring-time protocol definition.
 */
struct ProtocolDefinition {
  std::string name;
  std::vector<ImportDefinition> imports;
  std::vector<EnumDefinition> enums;
  std::vector<StructDefinition> structs;
  std::vector<MessageDefinition> messages;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__PROTOCOL_DEFINITION_HPP_
