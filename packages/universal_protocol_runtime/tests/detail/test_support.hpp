#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UNIVERSAL_PROTOCOL_RUNTIME_TESTS_DETAIL__TEST_SUPPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UNIVERSAL_PROTOCOL_RUNTIME_TESTS_DETAIL__TEST_SUPPORT_HPP_
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "universal_protocol_runtime/compiler/schema_compiler.hpp"

namespace upr_test_support {

namespace upr = universal_protocol_runtime;

inline upr::FieldDefinition make_scalar_field(std::string name,
                                              upr::FieldKind kind,
                                              uint8_t width_bytes,
                                              upr::ByteOrder byte_order = upr::ByteOrder::kLittleEndian,
                                              bool has_expected_unsigned = false,
                                              uint64_t expected_unsigned = 0) {
  upr::FieldDefinition field;
  field.name = std::move(name);
  field.kind = kind;
  field.width_bytes = width_bytes;
  field.byte_order = byte_order;
  field.fixed_size = 0;
  field.size_from_field.clear();
  field.has_expected_unsigned = has_expected_unsigned;
  field.expected_unsigned = expected_unsigned;
  field.enum_values.clear();
  return field;
}

inline upr::FieldDefinition make_enum_field(std::string name,
                                            uint8_t width_bytes,
                                            std::vector<upr::EnumValueDefinition> enum_values,
                                            upr::ByteOrder byte_order = upr::ByteOrder::kLittleEndian,
                                            bool has_expected_unsigned = false,
                                            uint64_t expected_unsigned = 0) {
  upr::FieldDefinition field = make_scalar_field(
      std::move(name), upr::FieldKind::kEnum, width_bytes, byte_order, has_expected_unsigned, expected_unsigned);
  field.enum_values = std::move(enum_values);
  return field;
}

inline upr::FieldDefinition make_fixed_bytes_field(std::string name, size_t size) {
  upr::FieldDefinition field;
  field.name = std::move(name);
  field.kind = upr::FieldKind::kBytes;
  field.width_bytes = 0;
  field.byte_order = upr::ByteOrder::kLittleEndian;
  field.fixed_size = size;
  field.size_from_field.clear();
  field.has_expected_unsigned = false;
  field.expected_unsigned = 0;
  field.enum_values.clear();
  return field;
}

inline upr::FieldDefinition make_string_field(std::string name,
                                              size_t size,
                                              upr::StringEncoding encoding = upr::StringEncoding::kAscii) {
  upr::FieldDefinition field = make_fixed_bytes_field(std::move(name), size);
  field.kind = upr::FieldKind::kString;
  field.string_encoding = encoding;
  return field;
}

inline upr::FieldDefinition make_dynamic_bytes_field(std::string name, std::string size_from_field) {
  upr::FieldDefinition field = make_fixed_bytes_field(std::move(name), 0);
  field.size_from_field = std::move(size_from_field);
  return field;
}

inline upr::FieldDefinition make_dynamic_string_field(std::string name,
                                                      std::string size_from_field,
                                                      upr::StringEncoding encoding = upr::StringEncoding::kAscii) {
  upr::FieldDefinition field = make_dynamic_bytes_field(std::move(name), std::move(size_from_field));
  field.kind = upr::FieldKind::kString;
  field.string_encoding = encoding;
  return field;
}

inline upr::BitFieldDefinition make_bit_field(std::string name,
                                              uint8_t offset_bits,
                                              uint8_t width_bits,
                                              bool is_signed = false,
                                              std::vector<upr::EnumValueDefinition> enum_values = {}) {
  upr::BitFieldDefinition bit_field;
  bit_field.name = std::move(name);
  bit_field.offset_bits = offset_bits;
  bit_field.width_bits = width_bits;
  bit_field.is_signed = is_signed;
  bit_field.enum_values = std::move(enum_values);
  return bit_field;
}

inline upr::FieldDefinition make_struct_field(std::string name, std::string referenced_type) {
  upr::FieldDefinition field;
  field.name = std::move(name);
  field.kind = upr::FieldKind::kStruct;
  field.referenced_type = std::move(referenced_type);
  return field;
}

inline void add_bit_field(upr::FieldDefinition* field, upr::BitFieldDefinition bit_field) {
  field->bit_fields.push_back(std::move(bit_field));
}

inline void add_checksum(upr::FieldDefinition* field,
                         std::string algorithm,
                         std::string from = "frame_start",
                         std::string to = "before_self") {
  field->checksum = upr::ChecksumDefinition{
      .algorithm = std::move(algorithm),
      .from = std::move(from),
      .to = std::move(to),
  };
}

inline upr::StructDefinition make_struct(std::string name, std::vector<upr::FieldDefinition> fields) {
  upr::StructDefinition definition;
  definition.name = std::move(name);
  definition.fields = std::move(fields);
  return definition;
}

inline upr::MessageDefinition make_message(std::string name,
                                           std::vector<upr::FieldDefinition> fields,
                                           bool allow_trailing_bytes = false) {
  upr::MessageDefinition message;
  message.name = std::move(name);
  message.fields = std::move(fields);
  message.allow_trailing_bytes = allow_trailing_bytes;
  return message;
}

inline upr::ProtocolDefinition make_protocol(std::string name,
                                             std::vector<upr::MessageDefinition> messages,
                                             std::vector<upr::StructDefinition> structs = {}) {
  upr::ProtocolDefinition protocol;
  protocol.name = std::move(name);
  protocol.structs = std::move(structs);
  protocol.messages = std::move(messages);
  return protocol;
}

inline upr::CompiledProtocol compile_protocol_or_throw(const upr::ProtocolDefinition& definition) {
  upr::StatusOr<upr::CompiledProtocol> compiled = upr::compile_protocol(definition);
  if (!compiled.ok()) {
    std::fprintf(stderr, "compile_protocol failed: %s\n", std::string(compiled.status().message()).c_str());
    std::abort();
  }
  return std::move(compiled).value();
}

template <typename T>
void append_integral(std::vector<std::byte>& out, T value, upr::ByteOrder byte_order) {
  static_assert(std::is_integral_v<T>);
  using Unsigned = std::make_unsigned_t<T>;
  const auto normalized = static_cast<Unsigned>(value);
  if (byte_order == upr::ByteOrder::kLittleEndian) {
    for (size_t index = 0; index < sizeof(T); ++index) {
      out.push_back(std::byte{static_cast<uint8_t>((normalized >> (index * 8U)) & 0xFFU)});
    }
    return;
  }
  for (size_t index = 0; index < sizeof(T); ++index) {
    const size_t shift = (sizeof(T) - 1U - index) * 8U;
    out.push_back(std::byte{static_cast<uint8_t>((normalized >> shift) & 0xFFU)});
  }
}

inline void append_float32(std::vector<std::byte>& out, float value, upr::ByteOrder byte_order) {
  append_integral(out, std::bit_cast<uint32_t>(value), byte_order);
}

inline void append_float64(std::vector<std::byte>& out, double value, upr::ByteOrder byte_order) {
  append_integral(out, std::bit_cast<uint64_t>(value), byte_order);
}

inline std::vector<std::byte> make_bytes(std::initializer_list<uint8_t> values) {
  std::vector<std::byte> bytes;
  bytes.reserve(values.size());
  for (const uint8_t value : values) {
    bytes.push_back(std::byte{value});
  }
  return bytes;
}

}  // namespace upr_test_support

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UNIVERSAL_PROTOCOL_RUNTIME_TESTS_UNIT__TEST_SUPPORT_HPP_
