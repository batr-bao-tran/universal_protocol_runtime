#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_AUTHORING_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__PROTOCOL_DEFINITION_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_AUTHORING_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__PROTOCOL_DEFINITION_HPP_
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_protocol_runtime/core/types.hpp"

namespace universal_protocol_runtime {

struct ImportDefinition {
  std::string path;
};

struct BitFieldDefinition {
  std::string name;
  uint8_t offset_bits = 0;
  uint8_t width_bits = 0;
  bool is_signed = false;
  std::vector<EnumValueDefinition> enum_values;
};

struct ChecksumDefinition {
  std::string algorithm;
  std::string from = "frame_start";
  std::string to = "before_self";
};

struct EnumDefinition {
  std::string name;
  uint8_t width_bytes = 0;
  ByteOrder byte_order = ByteOrder::kLittleEndian;
  std::vector<EnumValueDefinition> values;
};

struct FieldDefinition {
  std::string name;
  FieldKind kind = FieldKind::kUnsigned;
  uint8_t width_bytes = 0;
  ByteOrder byte_order = ByteOrder::kLittleEndian;
  StringEncoding string_encoding = StringEncoding::kAscii;
  size_t fixed_size = 0;
  std::string size_from_field;
  std::string referenced_type;
  bool has_expected_unsigned = false;
  uint64_t expected_unsigned = 0;
  std::vector<EnumValueDefinition> enum_values;
  std::vector<BitFieldDefinition> bit_fields;
  std::optional<ChecksumDefinition> checksum;

  bool is_dynamic_size() const { return !size_from_field.empty(); }
};

struct StructDefinition {
  std::string name;
  std::vector<FieldDefinition> fields;
};

struct MessageDefinition {
  std::string name;
  std::vector<FieldDefinition> fields;
  bool allow_trailing_bytes = false;
};

struct ProtocolDefinition {
  std::string name;
  std::vector<ImportDefinition> imports;
  std::vector<EnumDefinition> enums;
  std::vector<StructDefinition> structs;
  std::vector<MessageDefinition> messages;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__PROTOCOL_DEFINITION_HPP_
