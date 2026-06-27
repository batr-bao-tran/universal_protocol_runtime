#include "universal_protocol_runtime/codegen/bindings_generator.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <span>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "universal_protocol_runtime/core/types.hpp"

namespace universal_protocol_runtime {
namespace {

std::vector<std::string> tokenize_words(std::string_view value) {
  std::vector<std::string> tokens;
  std::string current;
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) != 0) {
      current.push_back(static_cast<char>(ch));
      continue;
    }
    if (!current.empty()) {
      tokens.push_back(std::move(current));
      current.clear();
    }
  }
  if (!current.empty()) {
    tokens.push_back(std::move(current));
  }
  return tokens;
}

std::string sanitize_namespace_segment(std::string_view value) {
  const std::vector<std::string> tokens = tokenize_words(value);
  if (tokens.empty()) {
    return "generated";
  }

  std::string result;
  for (size_t index = 0; index < tokens.size(); ++index) {
    if (index != 0) {
      result.push_back('_');
    }
    for (const unsigned char ch : tokens[index]) {
      result.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  if (std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
    result.insert(0, "generated_");
  }
  return result;
}

std::string sanitize_namespace_name(std::string_view value) {
  std::string result;
  size_t offset = 0;
  bool first_segment = true;
  while (offset <= value.size()) {
    const size_t delimiter = value.find("::", offset);
    const std::string_view segment =
        delimiter == std::string_view::npos ? value.substr(offset) : value.substr(offset, delimiter - offset);
    if (!first_segment) {
      result.append("::");
    }
    result.append(sanitize_namespace_segment(segment));
    if (delimiter == std::string_view::npos) {
      break;
    }
    offset = delimiter + 2U;
    first_segment = false;
  }
  if (result.empty()) {
    return "generated";
  }
  return result;
}

std::string sanitize_type_name(std::string_view value) {
  const std::vector<std::string> tokens = tokenize_words(value);
  if (tokens.empty()) {
    return "GeneratedBinding";
  }

  std::string result;
  for (const std::string& token : tokens) {
    if (std::isdigit(static_cast<unsigned char>(token.front())) != 0) {
      result.push_back('N');
    }
    result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(token.front()))));
    result.append(token.substr(1));
  }
  return result;
}

std::string sanitize_cpp_constant_name(std::string_view value) {
  const std::vector<std::string> tokens = tokenize_words(value);
  if (tokens.empty()) {
    return "kUnnamed";
  }

  std::string result = "k";
  for (const std::string& token : tokens) {
    if (std::isdigit(static_cast<unsigned char>(token.front())) != 0) {
      result.push_back('N');
    }
    result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(token.front()))));
    result.append(token.substr(1));
  }
  return result;
}

std::string sanitize_python_constant_name(std::string_view value) {
  const std::vector<std::string> tokens = tokenize_words(value);
  if (tokens.empty()) {
    return "UNNAMED";
  }

  std::string result;
  for (size_t index = 0; index < tokens.size(); ++index) {
    if (index != 0) {
      result.push_back('_');
    }
    for (const unsigned char ch : tokens[index]) {
      result.push_back(static_cast<char>(std::toupper(ch)));
    }
  }
  if (std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
    result.insert(0, "N_");
  }
  return result;
}

std::string sanitize_header_guard(std::string_view value) {
  const std::vector<std::string> tokens = tokenize_words(value);
  if (tokens.empty()) {
    return "UNIVERSAL_PROTOCOL_RUNTIME__GENERATED_BINDINGS_HPP_";
  }

  std::string result = "UNIVERSAL_PROTOCOL_RUNTIME__";
  for (size_t index = 0; index < tokens.size(); ++index) {
    if (index != 0) {
      result.push_back('_');
    }
    for (const unsigned char ch : tokens[index]) {
      result.push_back(static_cast<char>(std::toupper(ch)));
    }
  }
  result.append("_HPP_");
  return result;
}

bool is_valid_header_guard(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) == 0 && ch != '_') {
      return false;
    }
  }
  return std::isalpha(static_cast<unsigned char>(value.front())) != 0 || value.front() == '_';
}

std::string escape_cpp_string(std::string_view value) {
  std::ostringstream stream;
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        stream << "\\\\";
        break;
      case '"':
        stream << "\\\"";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        if (ch >= 0x20U && ch <= 0x7EU) {
          stream << static_cast<char>(ch);
        } else {
          stream << "\\x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(ch)
                 << std::nouppercase << std::dec;
        }
        break;
    }
  }
  return stream.str();
}

std::string escape_python_string(std::string_view value) { return escape_cpp_string(value); }

std::string cpp_field_kind_literal(FieldKind kind) {
  switch (kind) {
    case FieldKind::kUnsigned:
      return "universal_protocol_runtime::FieldKind::kUnsigned";
    case FieldKind::kSigned:
      return "universal_protocol_runtime::FieldKind::kSigned";
    case FieldKind::kFloat32:
      return "universal_protocol_runtime::FieldKind::kFloat32";
    case FieldKind::kFloat64:
      return "universal_protocol_runtime::FieldKind::kFloat64";
    case FieldKind::kBytes:
      return "universal_protocol_runtime::FieldKind::kBytes";
    case FieldKind::kString:
      return "universal_protocol_runtime::FieldKind::kString";
    case FieldKind::kStruct:
      return "universal_protocol_runtime::FieldKind::kStruct";
    case FieldKind::kEnum:
      return "universal_protocol_runtime::FieldKind::kEnum";
    case FieldKind::kCollection:
      return "universal_protocol_runtime::FieldKind::kCollection";
    case FieldKind::kVariant:
      return "universal_protocol_runtime::FieldKind::kVariant";
  }
  return "universal_protocol_runtime::FieldKind::kUnsigned";
}

std::string cpp_byte_order_literal(ByteOrder byte_order) {
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      return "universal_protocol_runtime::ByteOrder::kLittleEndian";
    case ByteOrder::kBigEndian:
      return "universal_protocol_runtime::ByteOrder::kBigEndian";
  }
  return "universal_protocol_runtime::ByteOrder::kLittleEndian";
}

std::string cpp_string_encoding_literal(StringEncoding encoding) {
  switch (encoding) {
    case StringEncoding::kAscii:
      return "universal_protocol_runtime::StringEncoding::kAscii";
    case StringEncoding::kUtf8:
      return "universal_protocol_runtime::StringEncoding::kUtf8";
  }
  return "universal_protocol_runtime::StringEncoding::kAscii";
}

std::string checksum_anchor_kind_name(ChecksumAnchorKind kind) {
  switch (kind) {
    case ChecksumAnchorKind::kFrameStart:
      return "frame_start";
    case ChecksumAnchorKind::kFrameEnd:
      return "frame_end";
    case ChecksumAnchorKind::kFieldStart:
      return "field_start";
    case ChecksumAnchorKind::kFieldEnd:
      return "field_end";
    case ChecksumAnchorKind::kBeforeSelf:
      return "before_self";
    case ChecksumAnchorKind::kAfterSelf:
      return "after_self";
  }
  return "frame_start";
}

std::string cpp_checksum_anchor_kind_literal(ChecksumAnchorKind kind) {
  switch (kind) {
    case ChecksumAnchorKind::kFrameStart:
      return "universal_protocol_runtime::ChecksumAnchorKind::kFrameStart";
    case ChecksumAnchorKind::kFrameEnd:
      return "universal_protocol_runtime::ChecksumAnchorKind::kFrameEnd";
    case ChecksumAnchorKind::kFieldStart:
      return "universal_protocol_runtime::ChecksumAnchorKind::kFieldStart";
    case ChecksumAnchorKind::kFieldEnd:
      return "universal_protocol_runtime::ChecksumAnchorKind::kFieldEnd";
    case ChecksumAnchorKind::kBeforeSelf:
      return "universal_protocol_runtime::ChecksumAnchorKind::kBeforeSelf";
    case ChecksumAnchorKind::kAfterSelf:
      return "universal_protocol_runtime::ChecksumAnchorKind::kAfterSelf";
  }
  return "universal_protocol_runtime::ChecksumAnchorKind::kFrameStart";
}

std::string cpp_byte_array_literal(std::span<const std::byte> bytes) {
  std::ostringstream stream;
  stream << "std::array<std::byte, " << bytes.size() << ">{";
  for (size_t index = 0; index < bytes.size(); ++index) {
    if (index != 0) {
      stream << ", ";
    }
    stream << "static_cast<std::byte>(0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(std::to_integer<unsigned char>(bytes[index])) << std::nouppercase << std::dec
           << ")";
  }
  stream << "}";
  return stream.str();
}

std::string python_bytes_literal(std::span<const std::byte> bytes) {
  std::ostringstream stream;
  stream << "b\"";
  for (const std::byte byte : bytes) {
    stream << "\\x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(std::to_integer<unsigned char>(byte)) << std::nouppercase << std::dec;
  }
  stream << "\"";
  return stream.str();
}

void append_indent(std::string* out, int indent) { out->append(static_cast<size_t>(indent) * 2U, ' '); }

void append_line(std::string* out) { out->push_back('\n'); }

void append_line(std::string* out, int indent, std::string_view line = {}) {
  append_indent(out, indent);
  out->append(line);
  out->push_back('\n');
}

template <typename... Parts>
void append_line_cat(std::string* out, int indent, const Parts&... parts) {
  std::string line;
  (line.append(std::string_view(parts)), ...);
  append_line(out, indent, line);
}

std::string cpp_field_binding_literal(const CompiledField& field) {
  std::ostringstream stream;
  stream << "FieldBinding{"
         << ".id = static_cast<universal_protocol_runtime::FieldId>(" << field.id << "), "
         << ".name = \"" << escape_cpp_string(field.name) << "\", "
         << ".kind = " << cpp_field_kind_literal(field.kind) << ", "
         << ".width_bytes = static_cast<uint8_t>(" << static_cast<unsigned>(field.width_bytes) << "), "
         << ".fixed_size = " << field.fixed_size << ", "
         << ".dynamic_size = " << (field.dynamic_size ? "true" : "false") << ", "
         << ".size_from_field = static_cast<universal_protocol_runtime::FieldId>(" << field.size_from_field << "), "
         << ".struct_id = " << field.struct_id << ", "
         << ".byte_order = " << cpp_byte_order_literal(field.byte_order) << ", "
         << ".string_encoding = " << cpp_string_encoding_literal(field.string_encoding) << ", "
         << ".has_expected_unsigned = " << (field.has_expected_unsigned ? "true" : "false") << ", "
         << ".expected_unsigned = " << field.expected_unsigned << "ULL"
         << "}";
  return stream.str();
}

std::string cpp_bit_field_binding_literal(const CompiledBitField& bit_field) {
  std::ostringstream stream;
  stream << "BitFieldBinding{"
         << ".id = static_cast<universal_protocol_runtime::BitFieldId>(" << bit_field.id << "), "
         << ".name = \"" << escape_cpp_string(bit_field.name) << "\", "
         << ".container_field_id = static_cast<universal_protocol_runtime::FieldId>(" << bit_field.container_field_id
         << "), "
         << ".shift_bits = static_cast<uint8_t>(" << static_cast<unsigned>(bit_field.shift_bits) << "), "
         << ".width_bits = static_cast<uint8_t>(" << static_cast<unsigned>(bit_field.width_bits) << "), "
         << ".mask = " << bit_field.mask << "ULL, "
         << ".is_signed = " << (bit_field.is_signed ? "true" : "false") << "}";
  return stream.str();
}

std::string cpp_checksum_binding_literal(const CompiledChecksum& checksum) {
  std::ostringstream stream;
  stream << "ChecksumBinding{"
         << ".field_id = static_cast<universal_protocol_runtime::FieldId>(" << checksum.field_id << "), "
         << ".result_width_bytes = static_cast<uint8_t>(" << static_cast<unsigned>(checksum.result_width_bytes) << "), "
         << ".algorithm_name = \"" << escape_cpp_string(checksum.algorithm_name) << "\", "
         << ".from = ChecksumAnchorBinding{.kind = " << cpp_checksum_anchor_kind_literal(checksum.from.kind)
         << ", .field_id = static_cast<universal_protocol_runtime::FieldId>(" << checksum.from.field_id << ")}, "
         << ".to = ChecksumAnchorBinding{.kind = " << cpp_checksum_anchor_kind_literal(checksum.to.kind)
         << ", .field_id = static_cast<universal_protocol_runtime::FieldId>(" << checksum.to.field_id << ")}"
         << "}";
  return stream.str();
}

void append_cpp_id_struct(std::string* out,
                          std::string_view struct_name,
                          std::string_view empty_name,
                          const auto& items,
                          std::string_view type_name,
                          auto name_accessor,
                          auto id_accessor,
                          int indent) {
  append_line(out, indent, std::string("struct ") + std::string(struct_name) + " {");
  if (items.empty()) {
    append_line(out, indent + 1, std::string("static constexpr bool ") + std::string(empty_name) + " = true;");
  } else {
    for (const auto& item : items) {
      append_line(out,
                  indent + 1,
                  "static constexpr " + std::string(type_name) + " " + sanitize_cpp_constant_name(name_accessor(item)) +
                      " = static_cast<" + std::string(type_name) + ">(" + std::to_string(id_accessor(item)) + ");");
    }
  }
  append_line(out, indent, "};");
}

void append_cpp_binding_array(
    std::string* out, std::string_view declaration, const auto& items, auto literal_fn, int indent) {
  append_line(out, indent, std::string(declaration) + "{");
  for (const auto& item : items) {
    append_line(out, indent + 1, literal_fn(item) + ",");
  }
  append_line(out, indent, "};");
}

std::string cpp_unsigned_storage_type(uint8_t width_bytes) {
  if (width_bytes <= sizeof(uint8_t)) {
    return "uint8_t";
  }
  if (width_bytes <= sizeof(uint16_t)) {
    return "uint16_t";
  }
  if (width_bytes <= sizeof(uint32_t)) {
    return "uint32_t";
  }
  return "uint64_t";
}

std::string cpp_signed_storage_type(uint8_t width_bytes) {
  if (width_bytes <= sizeof(int8_t)) {
    return "int8_t";
  }
  if (width_bytes <= sizeof(int16_t)) {
    return "int16_t";
  }
  if (width_bytes <= sizeof(int32_t)) {
    return "int32_t";
  }
  return "int64_t";
}

void append_cpp_view_accessor(std::string* out, const CompiledField& field, int indent) {
  const std::string constant_name = "Fields::" + sanitize_cpp_constant_name(field.name);
  const std::string method_name = sanitize_namespace_name(field.name);
  switch (field.kind) {
    case FieldKind::kUnsigned:
      append_line(out,
                  indent,
                  "std::optional<" + cpp_unsigned_storage_type(field.width_bytes) + "> " + method_name + "() const {");
      append_line(out,
                  indent + 1,
                  "return message_ == nullptr ? std::nullopt : message_->get<" +
                      cpp_unsigned_storage_type(field.width_bytes) + ">(" + constant_name + ");");
      append_line(out, indent, "}");
      return;
    case FieldKind::kSigned:
      append_line(out,
                  indent,
                  "std::optional<" + cpp_signed_storage_type(field.width_bytes) + "> " + method_name + "() const {");
      append_line(out,
                  indent + 1,
                  "return message_ == nullptr ? std::nullopt : message_->get<" +
                      cpp_signed_storage_type(field.width_bytes) + ">(" + constant_name + ");");
      append_line(out, indent, "}");
      return;
    case FieldKind::kFloat32:
      append_line(out, indent, "std::optional<float> " + method_name + "() const {");
      append_line(
          out, indent + 1, "return message_ == nullptr ? std::nullopt : message_->get<float>(" + constant_name + ");");
      append_line(out, indent, "}");
      return;
    case FieldKind::kFloat64:
      append_line(out, indent, "std::optional<double> " + method_name + "() const {");
      append_line(
          out, indent + 1, "return message_ == nullptr ? std::nullopt : message_->get<double>(" + constant_name + ");");
      append_line(out, indent, "}");
      return;
    case FieldKind::kBytes:
      if (field.dynamic_size) {
        append_line(out, indent, "std::optional<universal_protocol_runtime::ByteSpan> " + method_name + "() const {");
        append_line(
            out, indent + 1, "return message_ == nullptr ? std::nullopt : message_->get_bytes(" + constant_name + ");");
        append_line(out, indent, "}");
        return;
      }
      append_line(out,
                  indent,
                  "std::optional<std::span<const std::byte, " + std::to_string(field.fixed_size) + ">> " + method_name +
                      "() const {");
      append_line(out,
                  indent + 1,
                  "return message_ == nullptr ? std::nullopt : message_->get_fixed_bytes<" +
                      std::to_string(field.fixed_size) + ">(" + constant_name + ");");
      append_line(out, indent, "}");
      return;
    case FieldKind::kString:
      if (field.dynamic_size) {
        append_line(out, indent, "std::optional<std::string_view> " + method_name + "() const {");
        append_line(out,
                    indent + 1,
                    "return message_ == nullptr ? std::nullopt : message_->get_string_view(" + constant_name + ");");
        append_line(out, indent, "}");
        return;
      }
      append_line(out,
                  indent,
                  "std::optional<std::span<const char, " + std::to_string(field.fixed_size) + ">> " + method_name +
                      "() const {");
      append_line(out,
                  indent + 1,
                  "return message_ == nullptr ? std::nullopt : message_->get_fixed_string<" +
                      std::to_string(field.fixed_size) + ">(" + constant_name + ");");
      append_line(out, indent, "}");
      return;
    case FieldKind::kStruct:
      append_line(
          out, indent, "std::optional<universal_protocol_runtime::DecodedMessage> " + method_name + "() const {");
      append_line(
          out, indent + 1, "return message_ == nullptr ? std::nullopt : message_->get_struct(" + constant_name + ");");
      append_line(out, indent, "}");
      return;
    case FieldKind::kEnum:
      append_line(out,
                  indent,
                  "std::optional<" + cpp_unsigned_storage_type(field.width_bytes) + "> " + method_name + "() const {");
      append_line(out,
                  indent + 1,
                  "return message_ == nullptr ? std::nullopt : message_->get<" +
                      cpp_unsigned_storage_type(field.width_bytes) + ">(" + constant_name + ");");
      append_line(out, indent, "}");
      append_line(out, indent, "std::optional<std::string_view> " + method_name + "_name() const {");
      append_line(out,
                  indent + 1,
                  "return message_ == nullptr ? std::nullopt : message_->get_enum_name(" + constant_name + ");");
      append_line(out, indent, "}");
      return;
    case FieldKind::kCollection:
      append_line(out,
                  indent,
                  "std::optional<universal_protocol_runtime::DecodedCollectionView> " + method_name + "() const {");
      append_line(out,
                  indent + 1,
                  "return message_ == nullptr ? std::nullopt : message_->get_collection(" + constant_name + ");");
      append_line(out, indent, "}");
      return;
    case FieldKind::kVariant:
      append_line(
          out, indent, "std::optional<universal_protocol_runtime::DecodedMessage> " + method_name + "() const {");
      append_line(
          out, indent + 1, "return message_ == nullptr ? std::nullopt : message_->get_variant(" + constant_name + ");");
      append_line(out, indent, "}");
      return;
  }
}

std::string cpp_generated_value_type(const CompiledField& field) {
  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kEnum:
      return cpp_unsigned_storage_type(field.width_bytes);
    case FieldKind::kSigned:
      return cpp_signed_storage_type(field.width_bytes);
    case FieldKind::kFloat32:
      return "float";
    case FieldKind::kFloat64:
      return "double";
    case FieldKind::kBytes:
      return "universal_protocol_runtime::ByteSpan";
    case FieldKind::kString:
      return "std::string_view";
    case FieldKind::kStruct:
      return "universal_protocol_runtime::DecodedMessage";
    case FieldKind::kCollection:
      return "universal_protocol_runtime::DecodedCollectionView";
    case FieldKind::kVariant:
      return "universal_protocol_runtime::DecodedMessage";
  }
  return "uint64_t";
}

std::string struct_type_name(const CompiledProtocol& protocol, size_t struct_id);

// Member type for the owned Value struct. When `general` is true the nested
// kinds are materialized as owned values (vector / nested Value / variant);
// otherwise they remain borrowed views consumed by the dynamic fallback path.
std::string cpp_value_member_type(const CompiledProtocol& protocol, const CompiledField& field, bool general) {
  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kEnum:
      return cpp_unsigned_storage_type(field.width_bytes);
    case FieldKind::kSigned:
      return cpp_signed_storage_type(field.width_bytes);
    case FieldKind::kFloat32:
      return "float";
    case FieldKind::kFloat64:
      return "double";
    case FieldKind::kBytes:
      return "universal_protocol_runtime::ByteSpan";
    case FieldKind::kString:
      return "std::string_view";
    case FieldKind::kStruct:
      if (general) {
        return "structs::" + struct_type_name(protocol, field.struct_id) + "::Value";
      }
      return "universal_protocol_runtime::DecodedMessage";
    case FieldKind::kCollection:
      if (general) {
        return "std::vector<structs::" + struct_type_name(protocol, field.struct_id) + "::Value>";
      }
      return "universal_protocol_runtime::DecodedCollectionView";
    case FieldKind::kVariant:
      if (general) {
        std::string type = "std::variant<std::monostate";
        for (const CompiledVariantCase& variant_case : field.variant_cases) {
          type += ", structs::" + struct_type_name(protocol, variant_case.struct_id) + "::Value";
        }
        type += ">";
        return type;
      }
      return "universal_protocol_runtime::DecodedMessage";
  }
  return "uint64_t";
}

std::string cpp_generated_value_initializer(const CompiledField& field) {
  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kEnum:
    case FieldKind::kSigned:
      return " = 0;";
    case FieldKind::kFloat32:
    case FieldKind::kFloat64:
      return " = 0.0;";
    case FieldKind::kBytes:
    case FieldKind::kString:
    case FieldKind::kStruct:
    case FieldKind::kCollection:
    case FieldKind::kVariant:
      return ";";
  }
  return ";";
}

void append_cpp_value_field(
    std::string* out, const CompiledProtocol& protocol, const CompiledField& field, bool general, int indent) {
  append_line(out,
              indent,
              cpp_value_member_type(protocol, field, general) + " " + sanitize_namespace_name(field.name) +
                  cpp_generated_value_initializer(field));
}

std::string cpp_generated_extract_expression(const CompiledField& field) {
  const std::string constant_name = "Fields::" + sanitize_cpp_constant_name(field.name);
  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kEnum:
      return "message->get<" + cpp_unsigned_storage_type(field.width_bytes) + ">(" + constant_name + ")";
    case FieldKind::kSigned:
      return "message->get<" + cpp_signed_storage_type(field.width_bytes) + ">(" + constant_name + ")";
    case FieldKind::kFloat32:
      return "message->get<float>(" + constant_name + ")";
    case FieldKind::kFloat64:
      return "message->get<double>(" + constant_name + ")";
    case FieldKind::kBytes:
      return field.dynamic_size
                 ? "message->get_bytes(" + constant_name + ")"
                 : "message->get_fixed_bytes<" + std::to_string(field.fixed_size) + ">(" + constant_name + ")";
    case FieldKind::kString:
      return field.dynamic_size
                 ? "message->get_string_view(" + constant_name + ")"
                 : "message->get_fixed_string<" + std::to_string(field.fixed_size) + ">(" + constant_name + ")";
    case FieldKind::kStruct:
      return "message->get_struct(" + constant_name + ")";
    case FieldKind::kCollection:
      return "message->get_collection(" + constant_name + ")";
    case FieldKind::kVariant:
      return "message->get_variant(" + constant_name + ")";
  }
  return "std::nullopt";
}

std::string cpp_generated_extract_assignment(const CompiledField& field, std::string_view extracted_name) {
  switch (field.kind) {
    case FieldKind::kBytes:
      return field.dynamic_size ? "*" + std::string(extracted_name)
                                : "universal_protocol_runtime::ByteSpan(" + std::string(extracted_name) + "->data(), " +
                                      std::string(extracted_name) + "->size())";
    case FieldKind::kString:
      return field.dynamic_size ? "*" + std::string(extracted_name)
                                : "std::string_view(" + std::string(extracted_name) + "->data(), " +
                                      std::string(extracted_name) + "->size())";
    case FieldKind::kUnsigned:
    case FieldKind::kSigned:
    case FieldKind::kFloat32:
    case FieldKind::kFloat64:
    case FieldKind::kStruct:
    case FieldKind::kEnum:
    case FieldKind::kCollection:
    case FieldKind::kVariant:
      return "*" + std::string(extracted_name);
  }
  return "*" + std::string(extracted_name);
}

std::string cpp_field_constant_name(const CompiledField& field) {
  return "Fields::" + sanitize_cpp_constant_name(field.name);
}

bool checksum_algorithm_supports_direct_value_decode(std::string_view algorithm_name) {
  return algorithm_name == "xor8" || algorithm_name == "sum16" || algorithm_name == "crc16_ccitt" ||
         algorithm_name == "crc32" || algorithm_name == "crc32c";
}

bool checksums_support_direct(const CompiledMessage& layout) {
  return std::all_of(layout.checksums().begin(), layout.checksums().end(), [](const CompiledChecksum& checksum) {
    return checksum_algorithm_supports_direct_value_decode(checksum.algorithm_name);
  });
}

// "Simple" layouts contain only scalar/bytes/string fields with no presence,
// condition, or nested-type fields. They keep the original, tuned single-shot
// fast path codegen (and its fixed-width checksum specializations).
bool layout_supports_simple_direct_codec(const CompiledMessage& layout) {
  if (!layout.validations().empty() || !checksums_support_direct(layout)) {
    return false;
  }
  return std::all_of(layout.fields().begin(), layout.fields().end(), [](const CompiledField& field) {
    return field.kind != FieldKind::kStruct && field.kind != FieldKind::kCollection &&
           field.kind != FieldKind::kVariant && !field.has_condition && !field.has_presence && field.alignment <= 1U &&
           !field.is_reserved;
  });
}

// "General" layouts additionally support nested structs, collections, tagged
// variants, and presence/condition-gated fields, decoded/encoded by an iterative
// path that mirrors the dynamic decoder byte-for-byte. Validation rules are not
// replicated in the direct path, so layouts carrying them fall back to the
// dynamic decoder to preserve identical accept/reject behavior.
bool layout_supports_general_direct_codec(const CompiledProtocol& protocol, const CompiledMessage& layout) {
  std::vector<const CompiledMessage*> pending{&layout};
  std::unordered_set<size_t> visited_structs;

  const auto enqueue_struct = [&](size_t struct_id) -> bool {
    if (visited_structs.contains(struct_id)) {
      return true;
    }
    const CompiledMessage* nested = protocol.struct_by_id(struct_id);
    if (nested == nullptr) {
      return false;
    }
    visited_structs.insert(struct_id);
    pending.push_back(nested);
    return true;
  };

  while (!pending.empty()) {
    const CompiledMessage& current = *pending.back();
    pending.pop_back();
    if (!current.validations().empty() || !checksums_support_direct(current)) {
      return false;
    }
    for (const CompiledField& field : current.fields()) {
      if (field.kind == FieldKind::kStruct || field.kind == FieldKind::kCollection) {
        if (!enqueue_struct(field.struct_id)) {
          return false;
        }
      } else if (field.kind == FieldKind::kVariant) {
        for (const CompiledVariantCase& variant_case : field.variant_cases) {
          if (!enqueue_struct(variant_case.struct_id)) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

// A layout has a direct codec when it qualifies for either the simple fast path
// or the general path.
bool layout_supports_direct_value_decode(const CompiledProtocol& protocol, const CompiledMessage& layout) {
  return layout_supports_simple_direct_codec(layout) || layout_supports_general_direct_codec(protocol, layout);
}

// True when the layout uses the general path (owned nested Value members) rather
// than the simple fast path.
bool layout_uses_general_direct_codec(const CompiledProtocol& protocol, const CompiledMessage& layout) {
  return !layout_supports_simple_direct_codec(layout) && layout_supports_general_direct_codec(protocol, layout);
}

std::string struct_type_name(const CompiledProtocol& protocol, size_t struct_id) {
  const CompiledMessage* nested = protocol.struct_by_id(struct_id);
  return nested == nullptr ? std::string("GeneratedBinding") : sanitize_type_name(nested->name());
}

// Returns struct emission order with referenced structs before their users so
// that owned nested `Value` members reference already-complete types.
std::vector<size_t> topological_struct_order(const CompiledProtocol& protocol) {
  const size_t count = protocol.structs().size();
  std::vector<size_t> order;
  order.reserve(count);
  std::vector<char> state(count, 0);  // 0=unvisited, 1=in-progress, 2=done

  const auto dependencies = [&](size_t struct_id) {
    std::vector<size_t> deps;
    const CompiledMessage* layout = protocol.struct_by_id(struct_id);
    if (layout == nullptr) {
      return deps;
    }
    for (const CompiledField& field : layout->fields()) {
      if (field.kind == FieldKind::kStruct || field.kind == FieldKind::kCollection) {
        deps.push_back(field.struct_id);
      } else if (field.kind == FieldKind::kVariant) {
        for (const CompiledVariantCase& variant_case : field.variant_cases) {
          deps.push_back(variant_case.struct_id);
        }
      }
    }
    return deps;
  };

  // Iterative post-order DFS to avoid deep recursion; cycles fall back to
  // declaration order for the nodes still in-progress.
  std::vector<std::pair<size_t, size_t>> stack;  // (struct_id, next dependency index)
  for (size_t root = 0; root < count; ++root) {
    if (state[root] != 0) {
      continue;
    }
    stack.emplace_back(root, 0U);
    state[root] = 1;
    while (!stack.empty()) {
      auto& [node, dep_index] = stack.back();
      const std::vector<size_t> deps = dependencies(node);
      if (dep_index < deps.size()) {
        const size_t next = deps[dep_index];
        ++dep_index;
        if (next < count && state[next] == 0) {
          state[next] = 1;
          stack.emplace_back(next, 0U);
        }
        continue;
      }
      state[node] = 2;
      order.push_back(node);
      stack.pop_back();
    }
  }
  return order;
}

std::string cpp_direct_checksum_call(std::string_view algorithm_name, std::string_view bytes_expression) {
  if (algorithm_name == "xor8") {
    return "universal_protocol_runtime::direct_decode_support::runtime_checksum_xor8(" + std::string(bytes_expression) +
           ")";
  }
  if (algorithm_name == "sum16") {
    return "universal_protocol_runtime::direct_decode_support::runtime_checksum_sum16(" +
           std::string(bytes_expression) + ")";
  }
  if (algorithm_name == "crc16_ccitt") {
    return "universal_protocol_runtime::direct_decode_support::runtime_checksum_crc16_ccitt(" +
           std::string(bytes_expression) + ")";
  }
  if (algorithm_name == "crc32") {
    return "universal_protocol_runtime::direct_decode_support::runtime_checksum_crc32(" +
           std::string(bytes_expression) + ")";
  }
  if (algorithm_name == "crc32c") {
    return "universal_protocol_runtime::direct_decode_support::runtime_checksum_crc32c(" +
           std::string(bytes_expression) + ")";
  }
  return {};
}

bool checksum_uses_prefix_shortcut(const CompiledChecksum& checksum) {
  return checksum.from.kind == ChecksumAnchorKind::kFrameStart && checksum.to.kind == ChecksumAnchorKind::kBeforeSelf &&
         checksum.to.field_id == checksum.field_id;
}

bool encode_layout_needs_checksum_field_arrays(const CompiledMessage& layout) {
  return std::any_of(layout.checksums().begin(), layout.checksums().end(), [](const CompiledChecksum& checksum) {
    return !checksum_uses_prefix_shortcut(checksum);
  });
}

std::optional<size_t> fixed_field_offset(const CompiledMessage& layout, FieldId field_id) {
  if (field_id > layout.fields().size()) {
    return std::nullopt;
  }
  size_t offset = 0;
  for (FieldId index = 0; index < field_id; ++index) {
    const CompiledField& field = layout.fields()[index];
    if (field.dynamic_size) {
      return std::nullopt;
    }
    offset += field.minimum_size_contribution();
  }
  return offset;
}

std::optional<size_t> checksum_static_span_size(const CompiledMessage& layout, const CompiledChecksum& checksum) {
  if (!checksum_uses_prefix_shortcut(checksum)) {
    return std::nullopt;
  }
  return fixed_field_offset(layout, checksum.field_id);
}

std::string checksum_field_offset_name(const CompiledField& field) {
  return sanitize_namespace_name(field.name) + "_offset";
}

std::string cpp_direct_fixed_checksum_call(std::string_view algorithm_name,
                                           std::string_view data_expression,
                                           size_t static_size) {
  if (algorithm_name == "xor8") {
    return "universal_protocol_runtime::direct_encode_support::checksum_xor8_fixed<" + std::to_string(static_size) +
           ">(" + std::string(data_expression) + ")";
  }
  if (algorithm_name == "sum16") {
    return "universal_protocol_runtime::direct_encode_support::checksum_sum16_fixed<" + std::to_string(static_size) +
           ">(" + std::string(data_expression) + ")";
  }
  return {};
}

std::string cpp_checksum_anchor_expression(const CompiledMessage& layout,
                                           const CompiledChecksumAnchor& anchor,
                                           std::string_view checksum_limit_expression) {
  switch (anchor.kind) {
    case ChecksumAnchorKind::kFrameStart:
      return "0U";
    case ChecksumAnchorKind::kFrameEnd:
      return std::string(checksum_limit_expression);
    case ChecksumAnchorKind::kFieldStart:
    case ChecksumAnchorKind::kBeforeSelf:
      return "field_starts[" + cpp_field_constant_name(layout.fields()[anchor.field_id]) + "]";
    case ChecksumAnchorKind::kFieldEnd:
    case ChecksumAnchorKind::kAfterSelf:
      return "field_ends[" + cpp_field_constant_name(layout.fields()[anchor.field_id]) + "]";
  }
  return "0U";
}

std::string cpp_presence_condition_expression(const CompiledMessage& layout,
                                              const CompiledField& field,
                                              std::string_view access = "value->") {
  const std::string acc(access);
  std::string expr;
  if (field.has_condition) {
    expr = "(static_cast<uint64_t>(" + acc + sanitize_namespace_name(layout.fields()[field.condition_field].name) +
           ") == " + std::to_string(field.condition_equals) + "ULL)";
  }
  if (field.has_presence) {
    std::string presence = "(((static_cast<uint64_t>(" + acc +
                           sanitize_namespace_name(layout.fields()[field.presence_field].name) + ") >> " +
                           std::to_string(static_cast<unsigned>(field.presence_bit)) + "U) & 1ULL) != 0U)";
    expr = expr.empty() ? presence : (expr + " && " + presence);
  }
  return expr.empty() ? "true" : expr;
}

void append_cpp_align_offset(std::string* out, const CompiledField& field, int indent) {
  if (field.alignment <= 1U) {
    return;
  }
  const std::string mask = std::to_string(field.alignment - 1U);
  append_line_cat(out, indent, "offset = (offset + ", mask, "U) & ~static_cast<std::size_t>(", mask, "U);");
  append_line(out, indent, "if (offset > frame.size()) {");
  append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
  append_line(out, indent, "}");
}

void append_cpp_decode_error_set(std::string* out,
                                 int indent,
                                 const std::string& status,
                                 const std::string& field_name_expr,
                                 const std::string& offset_expr);

// Emits the bytes-reading, offset-advancing decode core for one field of a
// general layout. Assumes `frame`, `offset`, and `value` are in scope.
void append_cpp_general_decode_field_body(std::string* out,
                                          const CompiledProtocol& protocol,
                                          const CompiledMessage& layout,
                                          const CompiledField& field,
                                          int indent) {
  const std::string fname = sanitize_namespace_name(field.name);
  const std::string size_const = sanitize_cpp_constant_name(field.name) + "Size";
  const auto fixed_check = [&](const std::string& size_expr) {
    append_line(out, indent, "if (" + size_expr + " > frame.size() - offset) {");
    append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
    append_line(out, indent, "}");
  };

  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kEnum: {
      append_line(out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.width_bytes) + ";");
      fixed_check(size_const);
      append_line_cat(out,
                      indent,
                      "const auto ",
                      fname,
                      "_raw = universal_protocol_runtime::direct_decode_support::read_unsigned_scalar<",
                      cpp_byte_order_literal(field.byte_order),
                      ", ",
                      std::to_string(field.width_bytes),
                      ">(frame.subspan(offset, ",
                      size_const,
                      "));");
      append_line(out, indent, "if (!" + fname + "_raw.has_value()) {");
      append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
      append_line(out, indent, "}");
      if (field.has_expected_unsigned) {
        append_line(out, indent, "if (*" + fname + "_raw != " + std::to_string(field.expected_unsigned) + "ULL) {");
        append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
        append_line(out, indent, "}");
      }
      append_line_cat(out,
                      indent,
                      "value->",
                      fname,
                      " = static_cast<",
                      cpp_unsigned_storage_type(field.width_bytes),
                      ">(*",
                      fname,
                      "_raw);");
      append_line(out, indent, "offset += " + size_const + ";");
      return;
    }
    case FieldKind::kSigned: {
      append_line(out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.width_bytes) + ";");
      fixed_check(size_const);
      append_line_cat(out,
                      indent,
                      "const auto ",
                      fname,
                      "_raw = universal_protocol_runtime::direct_decode_support::read_unsigned_scalar<",
                      cpp_byte_order_literal(field.byte_order),
                      ", ",
                      std::to_string(field.width_bytes),
                      ">(frame.subspan(offset, ",
                      size_const,
                      "));");
      append_line(out, indent, "if (!" + fname + "_raw.has_value()) {");
      append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
      append_line(out, indent, "}");
      append_line_cat(out,
                      indent,
                      "const auto ",
                      fname,
                      "_signed = universal_protocol_runtime::direct_decode_support::sign_extend(*",
                      fname,
                      "_raw, ",
                      std::to_string(field.width_bytes * kBitsPerByte),
                      "U);");
      append_line(out, indent, "if (!" + fname + "_signed.has_value()) {");
      append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
      append_line(out, indent, "}");
      append_line_cat(out,
                      indent,
                      "value->",
                      fname,
                      " = static_cast<",
                      cpp_signed_storage_type(field.width_bytes),
                      ">(*",
                      fname,
                      "_signed);");
      append_line(out, indent, "offset += " + size_const + ";");
      return;
    }
    case FieldKind::kFloat32:
    case FieldKind::kFloat64: {
      const bool is32 = field.kind == FieldKind::kFloat32;
      append_line(out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.width_bytes) + ";");
      fixed_check(size_const);
      append_line_cat(out,
                      indent,
                      "const auto ",
                      fname,
                      "_decoded = universal_protocol_runtime::direct_decode_support::",
                      is32 ? "read_float32<" : "read_float64<",
                      cpp_byte_order_literal(field.byte_order),
                      ">(frame.subspan(offset, ",
                      size_const,
                      "));");
      append_line(out, indent, "if (!" + fname + "_decoded.has_value()) {");
      append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
      append_line(out, indent, "}");
      append_line_cat(out, indent, "value->", fname, " = *", fname, "_decoded;");
      append_line(out, indent, "offset += " + size_const + ";");
      return;
    }
    case FieldKind::kBytes: {
      if (field.dynamic_size) {
        append_line_cat(out,
                        indent,
                        "const auto ",
                        fname,
                        "_size = static_cast<std::size_t>(static_cast<uint64_t>(value->",
                        sanitize_namespace_name(layout.fields()[field.size_from_field].name),
                        "));");
        fixed_check(fname + "_size");
      } else {
        append_line(
            out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.fixed_size) + ";");
        fixed_check(size_const);
        append_line_cat(out, indent, "const std::size_t ", fname, "_size = ", size_const, ";");
      }
      append_line_cat(out, indent, "value->", fname, " = frame.subspan(offset, ", fname, "_size);");
      if (field.is_reserved) {
        append_line_cat(out, indent, "for (const std::byte ", fname, "_reserved_byte : value->", fname, ") {");
        append_line_cat(out,
                        indent + 1,
                        "if (",
                        fname,
                        "_reserved_byte != std::byte{",
                        std::to_string(static_cast<unsigned>(field.reserved_fill_byte)),
                        "}) {");
        append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
        append_line(out, indent + 1, "}");
        append_line(out, indent, "}");
      }
      append_line(out, indent, "offset += " + fname + "_size;");
      return;
    }
    case FieldKind::kString: {
      if (field.dynamic_size) {
        append_line_cat(out,
                        indent,
                        "const auto ",
                        fname,
                        "_size = static_cast<std::size_t>(static_cast<uint64_t>(value->",
                        sanitize_namespace_name(layout.fields()[field.size_from_field].name),
                        "));");
        fixed_check(fname + "_size");
      } else {
        append_line(
            out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.fixed_size) + ";");
        fixed_check(size_const);
        append_line_cat(out, indent, "const std::size_t ", fname, "_size = ", size_const, ";");
      }
      append_line_cat(out, indent, "const auto ", fname, "_bytes = frame.subspan(offset, ", fname, "_size);");
      append_line_cat(out,
                      indent,
                      "if (!universal_protocol_runtime::direct_decode_support::runtime_validate_string<",
                      cpp_string_encoding_literal(field.string_encoding),
                      ">(",
                      fname,
                      "_bytes)) {");
      append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
      append_line(out, indent, "}");
      append_line_cat(out,
                      indent,
                      "value->",
                      fname,
                      " = std::string_view(reinterpret_cast<const char*>(",
                      fname,
                      "_bytes.data()), ",
                      fname,
                      "_bytes.size());");
      append_line(out, indent, "offset += " + fname + "_size;");
      return;
    }
    case FieldKind::kStruct: {
      append_line(out, indent, "std::size_t " + fname + "_consumed = 0;");
      append_line_cat(out,
                      indent,
                      "const auto ",
                      fname,
                      "_status = structs::",
                      struct_type_name(protocol, field.struct_id),
                      "::decode_value_direct(frame.subspan(offset), &value->",
                      fname,
                      ", &",
                      fname,
                      "_consumed, error);");
      append_line(out, indent, "if (" + fname + "_status != universal_protocol_runtime::DecodeStatus::kOk) {");
      append_line(out, indent + 1, "if (error != nullptr) { error->byte_offset += offset; }");
      append_line(out, indent + 1, "return " + fname + "_status;");
      append_line(out, indent, "}");
      append_line(out, indent, "offset += " + fname + "_consumed;");
      return;
    }
    case FieldKind::kCollection: {
      if (field.dynamic_count) {
        append_line_cat(out,
                        indent,
                        "const std::size_t ",
                        fname,
                        "_count = static_cast<std::size_t>(static_cast<uint64_t>(value->",
                        sanitize_namespace_name(layout.fields()[field.count_from_field].name),
                        "));");
      } else {
        append_line(out, indent, "const std::size_t " + fname + "_count = " + std::to_string(field.fixed_count) + "U;");
      }
      const std::string elem_type = "structs::" + struct_type_name(protocol, field.struct_id) + "::Value";
      append_line(out, indent, "value->" + fname + ".clear();");
      append_line(out,
                  indent,
                  "for (std::size_t " + fname + "_i = 0; " + fname + "_i < " + fname + "_count; ++" + fname + "_i) {");
      append_line(out, indent + 1, "if (offset > frame.size()) {");
      append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
      append_line(out, indent + 1, "}");
      append_line(out, indent + 1, elem_type + " " + fname + "_element{};");
      append_line(out, indent + 1, "std::size_t " + fname + "_element_consumed = 0;");
      append_line_cat(out,
                      indent + 1,
                      "const auto ",
                      fname,
                      "_element_status = structs::",
                      struct_type_name(protocol, field.struct_id),
                      "::decode_value_direct(frame.subspan(offset), &",
                      fname,
                      "_element, &",
                      fname,
                      "_element_consumed, error);");
      append_line(
          out, indent + 1, "if (" + fname + "_element_status != universal_protocol_runtime::DecodeStatus::kOk) {");
      append_line(out, indent + 2, "if (error != nullptr) { error->byte_offset += offset; }");
      append_line(out, indent + 2, "return " + fname + "_element_status;");
      append_line(out, indent + 1, "}");
      append_line(out, indent + 1, "if (" + fname + "_element_consumed == 0) {");
      append_cpp_decode_error_set(
          out, indent + 2, "kSchemaMismatch", "\"" + escape_cpp_string(field.name) + "\"", "offset");
      append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
      append_line(out, indent + 1, "}");
      append_line(out, indent + 1, "offset += " + fname + "_element_consumed;");
      append_line(out, indent + 1, "value->" + fname + ".push_back(std::move(" + fname + "_element));");
      append_line(out, indent, "}");
      return;
    }
    case FieldKind::kVariant: {
      append_line_cat(out,
                      indent,
                      "const uint64_t ",
                      fname,
                      "_tag = static_cast<uint64_t>(value->",
                      sanitize_namespace_name(layout.fields()[field.tag_from_field].name),
                      ");");
      append_line(out, indent, "std::size_t " + fname + "_consumed = 0;");
      append_line(out, indent, "switch (" + fname + "_tag) {");
      for (size_t case_index = 0; case_index < field.variant_cases.size(); ++case_index) {
        const CompiledVariantCase& variant_case = field.variant_cases[case_index];
        const std::string case_type = struct_type_name(protocol, variant_case.struct_id);
        append_line(out, indent + 1, "case " + std::to_string(variant_case.tag_value) + "ULL: {");
        append_line_cat(out, indent + 2, "structs::", case_type, "::Value ", fname, "_case{};");
        append_line_cat(out,
                        indent + 2,
                        "const auto ",
                        fname,
                        "_status = structs::",
                        case_type,
                        "::decode_value_direct(frame.subspan(offset), &",
                        fname,
                        "_case, &",
                        fname,
                        "_consumed, error);");
        append_line(out, indent + 2, "if (" + fname + "_status != universal_protocol_runtime::DecodeStatus::kOk) {");
        append_line(out, indent + 3, "if (error != nullptr) { error->byte_offset += offset; }");
        append_line(out, indent + 3, "return " + fname + "_status;");
        append_line(out, indent + 2, "}");
        append_line_cat(out,
                        indent + 2,
                        "value->",
                        fname,
                        ".emplace<",
                        std::to_string(case_index + 1U),
                        ">(std::move(",
                        fname,
                        "_case));");
        append_line(out, indent + 2, "break;");
        append_line(out, indent + 1, "}");
      }
      append_line(out, indent + 1, "default:");
      append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
      append_line(out, indent, "}");
      append_line(out, indent, "offset += " + fname + "_consumed;");
      return;
    }
  }
}

void append_cpp_decode_error_set(std::string* out,
                                 int indent,
                                 const std::string& status,
                                 const std::string& field_name_expr,
                                 const std::string& offset_expr);

void append_cpp_simple_direct_decode_function(std::string* out,
                                              const CompiledMessage& layout,
                                              bool is_message,
                                              int indent) {
  append_line(out, indent, "static universal_protocol_runtime::DecodeStatus decode_value_direct(");
  append_line(out, indent + 1, "universal_protocol_runtime::ByteSpan frame,");
  append_line(out, indent + 1, "Value* value,");
  append_line(out, indent + 1, "std::size_t* bytes_consumed = nullptr,");
  append_line(out, indent + 1, "universal_protocol_runtime::DecodeError* error = nullptr) {");
  append_line(out, indent + 1, "if (value == nullptr) {");
  append_cpp_decode_error_set(out, indent + 2, "kInvalidData", "\"\"", "0");
  append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
  append_line(out, indent + 1, "}");
  append_line(out, indent + 1, "if (frame.size() < kMinimumSize) {");
  append_cpp_decode_error_set(out, indent + 2, "kSchemaMismatch", "\"\"", "frame.size()");
  append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
  append_line(out, indent + 1, "}");
  if (is_message && !layout.dispatch_prefix().empty()) {
    append_line(out,
                indent + 1,
                "if (!universal_protocol_runtime::direct_decode_support::starts_with(frame, kDispatchPrefix)) {");
    append_cpp_decode_error_set(out, indent + 2, "kSchemaMismatch", "\"\"", "0");
    append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
    append_line(out, indent + 1, "}");
  }
  if (!layout.checksums().empty()) {
    append_line(
        out, indent + 1, "std::array<std::size_t, " + std::to_string(layout.fields().size()) + "> field_starts{};");
    append_line(
        out, indent + 1, "std::array<std::size_t, " + std::to_string(layout.fields().size()) + "> field_ends{};");
  }
  append_line(out, indent + 1, "std::size_t offset = 0;");

  for (const CompiledField& field : layout.fields()) {
    const std::string field_name = sanitize_namespace_name(field.name);
    const std::string field_constant_name = cpp_field_constant_name(field);
    const std::string field_bytes_name = field_name + "_bytes";
    const std::string field_size_name = field_name + "_size";
    const std::string field_size_constant_name = sanitize_cpp_constant_name(field.name) + "Size";
    if (!layout.checksums().empty()) {
      append_line(out, indent + 1, "field_starts[" + field_constant_name + "] = offset;");
    }
    append_line_cat(out,
                    indent + 1,
                    "if (error != nullptr) { error->field_name = \"",
                    escape_cpp_string(field.name),
                    "\"; error->byte_offset = offset; }");

    if (field.kind == FieldKind::kBytes || field.kind == FieldKind::kString) {
      if (field.dynamic_size) {
        const CompiledField& size_field = layout.fields()[field.size_from_field];
        append_line(out,
                    indent + 1,
                    "const auto " + field_size_name + "_u64 = static_cast<uint64_t>(value->" +
                        sanitize_namespace_name(size_field.name) + ");");
        append_line(out, indent + 1, "if (" + field_size_name + "_u64 > frame.size() - offset) {");
        append_cpp_decode_error_set(
            out, indent + 2, "kSchemaMismatch", "\"" + escape_cpp_string(field.name) + "\"", "offset");
        append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
        append_line(out, indent + 1, "}");
        append_line_cat(
            out, indent + 1, "const auto ", field_size_name, " = static_cast<std::size_t>(", field_size_name, "_u64);");
      } else {
        append_line(
            out,
            indent + 1,
            "constexpr std::size_t " + field_size_constant_name + " = " + std::to_string(field.fixed_size) + ";");
        append_line(out, indent + 1, "if (" + field_size_constant_name + " > frame.size() - offset) {");
        append_cpp_decode_error_set(
            out, indent + 2, "kSchemaMismatch", "\"" + escape_cpp_string(field.name) + "\"", "offset");
        append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
        append_line(out, indent + 1, "}");
        append_line_cat(out, indent + 1, "const auto ", field_size_name, " = ", field_size_constant_name, ";");
      }
      append_line_cat(
          out, indent + 1, "const auto ", field_bytes_name, " = frame.subspan(offset, ", field_size_name, ");");
      if (field.kind == FieldKind::kString) {
        append_line(out,
                    indent + 1,
                    "if (!universal_protocol_runtime::direct_decode_support::runtime_validate_string<" +
                        cpp_string_encoding_literal(field.string_encoding) + ">(" + field_bytes_name + ")) {");
        append_cpp_decode_error_set(
            out, indent + 2, "kInvalidData", "\"" + escape_cpp_string(field.name) + "\"", "offset");
        append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
        append_line(out, indent + 1, "}");
        append_line_cat(out,
                        indent + 1,
                        "value->",
                        field_name,
                        " = std::string_view(reinterpret_cast<const char*>(",
                        field_bytes_name,
                        ".data()), ",
                        field_bytes_name,
                        ".size());");
      } else {
        append_line_cat(out, indent + 1, "value->", field_name, " = ", field_bytes_name, ";");
      }
      append_line(out, indent + 1, "offset += " + field_size_name + ";");
    } else {
      append_line(
          out,
          indent + 1,
          "constexpr std::size_t " + field_size_constant_name + " = " + std::to_string(field.width_bytes) + ";");
      append_line(out, indent + 1, "if (" + field_size_constant_name + " > frame.size() - offset) {");
      append_cpp_decode_error_set(
          out, indent + 2, "kSchemaMismatch", "\"" + escape_cpp_string(field.name) + "\"", "offset");
      append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
      append_line(out, indent + 1, "}");
      append_line_cat(out, indent + 1, "const auto ", field_size_name, " = ", field_size_constant_name, ";");
      append_line_cat(
          out, indent + 1, "const auto ", field_bytes_name, " = frame.subspan(offset, ", field_size_name, ");");
      switch (field.kind) {
        case FieldKind::kUnsigned:
        case FieldKind::kEnum: {
          const std::string raw_name = field_name + "_raw";
          append_line_cat(out,
                          indent + 1,
                          "const auto ",
                          raw_name,
                          " = universal_protocol_runtime::direct_decode_support::read_unsigned_scalar<",
                          cpp_byte_order_literal(field.byte_order),
                          ", ",
                          std::to_string(field.width_bytes),
                          ">(",
                          field_bytes_name,
                          ");");
          append_line(out, indent + 1, "if (!" + raw_name + ".has_value()) {");
          append_cpp_decode_error_set(
              out, indent + 2, "kInvalidData", "\"" + escape_cpp_string(field.name) + "\"", "offset");
          append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
          append_line(out, indent + 1, "}");
          if (field.has_expected_unsigned) {
            append_line(
                out, indent + 1, "if (*" + raw_name + " != " + std::to_string(field.expected_unsigned) + "ULL) {");
            append_cpp_decode_error_set(
                out, indent + 2, "kSchemaMismatch", "\"" + escape_cpp_string(field.name) + "\"", "offset");
            append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
            append_line(out, indent + 1, "}");
          }
          append_line_cat(out,
                          indent + 1,
                          "value->",
                          field_name,
                          " = static_cast<",
                          cpp_generated_value_type(field),
                          ">(*",
                          raw_name,
                          ");");
          break;
        }
        case FieldKind::kSigned: {
          const std::string raw_name = field_name + "_raw";
          const std::string signed_name = field_name + "_signed";
          append_line_cat(out,
                          indent + 1,
                          "const auto ",
                          raw_name,
                          " = universal_protocol_runtime::direct_decode_support::read_unsigned_scalar<",
                          cpp_byte_order_literal(field.byte_order),
                          ", ",
                          std::to_string(field.width_bytes),
                          ">(",
                          field_bytes_name,
                          ");");
          append_line(out, indent + 1, "if (!" + raw_name + ".has_value()) {");
          append_cpp_decode_error_set(
              out, indent + 2, "kInvalidData", "\"" + escape_cpp_string(field.name) + "\"", "offset");
          append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
          append_line(out, indent + 1, "}");
          append_line_cat(out,
                          indent + 1,
                          "const auto ",
                          signed_name,
                          " = universal_protocol_runtime::direct_decode_support::sign_extend(*",
                          raw_name,
                          ", ",
                          std::to_string(field.width_bytes * kBitsPerByte),
                          "U);");
          append_line(out, indent + 1, "if (!" + signed_name + ".has_value()) {");
          append_cpp_decode_error_set(
              out, indent + 2, "kInvalidData", "\"" + escape_cpp_string(field.name) + "\"", "offset");
          append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
          append_line(out, indent + 1, "}");
          append_line_cat(out,
                          indent + 1,
                          "value->",
                          field_name,
                          " = static_cast<",
                          cpp_generated_value_type(field),
                          ">(*",
                          signed_name,
                          ");");
          break;
        }
        case FieldKind::kFloat32: {
          const std::string decoded_name = field_name + "_decoded";
          append_line_cat(out,
                          indent + 1,
                          "const auto ",
                          decoded_name,
                          " = universal_protocol_runtime::direct_decode_support::read_float32<",
                          cpp_byte_order_literal(field.byte_order),
                          ">(",
                          field_bytes_name,
                          ");");
          append_line(out, indent + 1, "if (!" + decoded_name + ".has_value()) {");
          append_cpp_decode_error_set(
              out, indent + 2, "kInvalidData", "\"" + escape_cpp_string(field.name) + "\"", "offset");
          append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
          append_line(out, indent + 1, "}");
          append_line_cat(out, indent + 1, "value->", field_name, " = *", decoded_name, ";");
          break;
        }
        case FieldKind::kFloat64: {
          const std::string decoded_name = field_name + "_decoded";
          append_line_cat(out,
                          indent + 1,
                          "const auto ",
                          decoded_name,
                          " = universal_protocol_runtime::direct_decode_support::read_float64<",
                          cpp_byte_order_literal(field.byte_order),
                          ">(",
                          field_bytes_name,
                          ");");
          append_line(out, indent + 1, "if (!" + decoded_name + ".has_value()) {");
          append_cpp_decode_error_set(
              out, indent + 2, "kInvalidData", "\"" + escape_cpp_string(field.name) + "\"", "offset");
          append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
          append_line(out, indent + 1, "}");
          append_line_cat(out, indent + 1, "value->", field_name, " = *", decoded_name, ";");
          break;
        }
        case FieldKind::kBytes:
        case FieldKind::kString:
        case FieldKind::kStruct:
        case FieldKind::kCollection:
        case FieldKind::kVariant:
          break;
      }
      append_line(out, indent + 1, "offset += " + field_size_name + ";");
    }

    if (!layout.checksums().empty()) {
      append_line(out, indent + 1, "field_ends[" + field_constant_name + "] = offset;");
    }
  }

  if (is_message) {
    if (!layout.allow_trailing_bytes()) {
      append_line(out, indent + 1, "if (offset != frame.size()) {");
      append_cpp_decode_error_set(out, indent + 2, "kSchemaMismatch", "\"\"", "offset");
      append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
      append_line(out, indent + 1, "}");
    }
    append_line(out,
                indent + 1,
                "const std::size_t checksum_limit = " +
                    std::string(layout.allow_trailing_bytes() ? "frame.size()" : "offset") + ";");
  } else {
    append_line(out, indent + 1, "if (bytes_consumed == nullptr && offset != frame.size()) {");
    append_cpp_decode_error_set(out, indent + 2, "kSchemaMismatch", "\"\"", "offset");
    append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
    append_line(out, indent + 1, "}");
    append_line(out, indent + 1, "const std::size_t checksum_limit = offset;");
  }
  if (layout.checksums().empty()) {
    append_line(out, indent + 1, "(void)checksum_limit;");
  }

  for (const CompiledChecksum& checksum : layout.checksums()) {
    const std::string field_name = sanitize_namespace_name(layout.fields()[checksum.field_id].name);
    const std::string checksum_field_literal = "\"" + escape_cpp_string(layout.fields()[checksum.field_id].name) + "\"";
    const std::string from_name = field_name + "_checksum_from";
    const std::string to_name = field_name + "_checksum_to";
    append_line_cat(out,
                    indent + 1,
                    "const std::size_t ",
                    from_name,
                    " = ",
                    cpp_checksum_anchor_expression(layout, checksum.from, "checksum_limit"),
                    ";");
    append_line_cat(out,
                    indent + 1,
                    "const std::size_t ",
                    to_name,
                    " = ",
                    cpp_checksum_anchor_expression(layout, checksum.to, "checksum_limit"),
                    ";");
    append_line_cat(out, indent + 1, "if (", from_name, " > ", to_name, " || ", to_name, " > checksum_limit) {");
    append_cpp_decode_error_set(out, indent + 2, "kSchemaMismatch", checksum_field_literal, from_name);
    append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
    append_line(out, indent + 1, "}");
    std::string checksum_slice_expression = "frame.subspan(";
    checksum_slice_expression += from_name;
    checksum_slice_expression += ", ";
    checksum_slice_expression += to_name;
    checksum_slice_expression += " - ";
    checksum_slice_expression += from_name;
    checksum_slice_expression += ")";
    append_line_cat(out,
                    indent + 1,
                    "if (static_cast<uint64_t>(value->",
                    field_name,
                    ") != ",
                    cpp_direct_checksum_call(checksum.algorithm_name, checksum_slice_expression),
                    ") {");
    append_cpp_decode_error_set(out, indent + 2, "kChecksumMismatch", checksum_field_literal, from_name);
    append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kChecksumMismatch;");
    append_line(out, indent + 1, "}");
  }

  append_line(out, indent + 1, "if (bytes_consumed != nullptr) {");
  append_line(out, indent + 2, "*bytes_consumed = offset;");
  append_line(out, indent + 1, "}");
  append_cpp_decode_error_set(out, indent + 1, "kOk", "\"\"", "offset");
  append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kOk;");
  append_line(out, indent, "}");
}

// Emits a guarded assignment of the rich DecodeError out-parameter.
void append_cpp_decode_error_set(std::string* out,
                                 int indent,
                                 const std::string& status,
                                 const std::string& field_name_expr,
                                 const std::string& offset_expr) {
  append_line_cat(out,
                  indent,
                  "if (error != nullptr) { *error = {universal_protocol_runtime::DecodeStatus::",
                  status,
                  ", ",
                  field_name_expr,
                  ", ",
                  offset_expr,
                  "}; }");
}

// Recursive decode body for layouts containing nested structs, collections,
// tagged variants, or presence/condition-gated fields. Mirrors the dynamic
// decoder (assign_from_layout) byte-for-byte while populating owned Value
// members (nested Value, std::vector, std::variant).
void append_cpp_general_direct_decode_function(
    std::string* out, const CompiledProtocol& protocol, const CompiledMessage& layout, bool is_message, int indent) {
  const bool has_checksums = !layout.checksums().empty();
  const int b = indent + 1;

  append_line(out, indent, "static universal_protocol_runtime::DecodeStatus decode_value_direct(");
  append_line(out, indent + 1, "universal_protocol_runtime::ByteSpan frame,");
  append_line(out, indent + 1, "Value* value,");
  append_line(out, indent + 1, "std::size_t* bytes_consumed = nullptr,");
  append_line(out, indent + 1, "universal_protocol_runtime::DecodeError* error = nullptr) {");
  append_line(out, b, "if (value == nullptr) {");
  append_cpp_decode_error_set(out, b + 1, "kInvalidData", "\"\"", "0");
  append_line(out, b + 1, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
  append_line(out, b, "}");
  append_line(out, b, "if (frame.size() < kMinimumSize) {");
  append_cpp_decode_error_set(out, b + 1, "kSchemaMismatch", "\"\"", "frame.size()");
  append_line(out, b + 1, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
  append_line(out, b, "}");
  if (is_message && !layout.dispatch_prefix().empty()) {
    append_line(
        out, b, "if (!universal_protocol_runtime::direct_decode_support::starts_with(frame, kDispatchPrefix)) {");
    append_cpp_decode_error_set(out, b + 1, "kSchemaMismatch", "\"\"", "0");
    append_line(out, b + 1, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
    append_line(out, b, "}");
  }
  if (has_checksums) {
    append_line(out, b, "std::array<std::size_t, " + std::to_string(layout.fields().size()) + "> field_starts{};");
    append_line(out, b, "std::array<std::size_t, " + std::to_string(layout.fields().size()) + "> field_ends{};");
  }
  append_line(out, b, "std::size_t offset = 0;");

  for (const CompiledField& field : layout.fields()) {
    const std::string fname = sanitize_namespace_name(field.name);
    const std::string fconst = cpp_field_constant_name(field);
    const bool gated = field.has_condition || field.has_presence;
    append_line(out, b, "{");
    int fb = b + 1;
    if (gated) {
      append_line(
          out, fb, "const bool " + fname + "_present = " + cpp_presence_condition_expression(layout, field) + ";");
      append_line(out, fb, "if (" + fname + "_present) {");
      fb += 1;
    }
    append_cpp_align_offset(out, field, fb);
    if (has_checksums) {
      append_line(out, fb, "field_starts[" + fconst + "] = offset;");
    }
    append_cpp_decode_error_set(out, fb, "kInvalidData", "\"" + escape_cpp_string(field.name) + "\"", "offset");
    append_line_cat(out,
                    fb,
                    "const universal_protocol_runtime::DecodeStatus ",
                    fname,
                    "_field_status = [&]() -> universal_protocol_runtime::DecodeStatus {");
    append_cpp_general_decode_field_body(out, protocol, layout, field, fb + 1);
    append_line(out, fb + 1, "return universal_protocol_runtime::DecodeStatus::kOk;");
    append_line(out, fb, "}();");
    append_line(out, fb, "if (" + fname + "_field_status != universal_protocol_runtime::DecodeStatus::kOk) {");
    append_line(out, fb + 1, "if (error != nullptr) { error->status = " + fname + "_field_status; }");
    append_line(out, fb + 1, "return " + fname + "_field_status;");
    append_line(out, fb, "}");
    if (has_checksums) {
      append_line(out, fb, "field_ends[" + fconst + "] = offset;");
    }
    if (gated) {
      fb -= 1;
      append_line(out, fb, "}");
      if (has_checksums) {
        append_line(out, fb, "else {");
        append_line(out, fb + 1, "field_starts[" + fconst + "] = offset;");
        append_line(out, fb + 1, "field_ends[" + fconst + "] = offset;");
        append_line(out, fb, "}");
      }
    }
    append_line(out, b, "}");
  }

  if (is_message) {
    if (!layout.allow_trailing_bytes()) {
      append_line(out, b, "if (offset != frame.size()) {");
      append_cpp_decode_error_set(out, b + 1, "kSchemaMismatch", "\"\"", "offset");
      append_line(out, b + 1, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
      append_line(out, b, "}");
    }
    append_line(out,
                b,
                "const std::size_t checksum_limit = " +
                    std::string(layout.allow_trailing_bytes() ? "frame.size()" : "offset") + ";");
  } else {
    append_line(out, b, "if (bytes_consumed == nullptr && offset != frame.size()) {");
    append_cpp_decode_error_set(out, b + 1, "kSchemaMismatch", "\"\"", "offset");
    append_line(out, b + 1, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
    append_line(out, b, "}");
    append_line(out, b, "const std::size_t checksum_limit = offset;");
  }
  if (!has_checksums) {
    append_line(out, b, "(void)checksum_limit;");
  }

  for (const CompiledChecksum& checksum : layout.checksums()) {
    const std::string field_name = sanitize_namespace_name(layout.fields()[checksum.field_id].name);
    const std::string from_name = field_name + "_checksum_from";
    const std::string to_name = field_name + "_checksum_to";
    append_line_cat(out,
                    b,
                    "const std::size_t ",
                    from_name,
                    " = ",
                    cpp_checksum_anchor_expression(layout, checksum.from, "checksum_limit"),
                    ";");
    append_line_cat(out,
                    b,
                    "const std::size_t ",
                    to_name,
                    " = ",
                    cpp_checksum_anchor_expression(layout, checksum.to, "checksum_limit"),
                    ";");
    append_line_cat(out, b, "if (", from_name, " > ", to_name, " || ", to_name, " > checksum_limit) {");
    append_cpp_decode_error_set(out,
                                b + 1,
                                "kSchemaMismatch",
                                "\"" + escape_cpp_string(layout.fields()[checksum.field_id].name) + "\"",
                                from_name);
    append_line(out, b + 1, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
    append_line(out, b, "}");
    std::string slice = "frame.subspan(";
    slice += from_name;
    slice += ", ";
    slice += to_name;
    slice += " - ";
    slice += from_name;
    slice += ")";
    append_line_cat(out,
                    b,
                    "if (static_cast<uint64_t>(value->",
                    field_name,
                    ") != ",
                    cpp_direct_checksum_call(checksum.algorithm_name, slice),
                    ") {");
    append_cpp_decode_error_set(out,
                                b + 1,
                                "kChecksumMismatch",
                                "\"" + escape_cpp_string(layout.fields()[checksum.field_id].name) + "\"",
                                from_name);
    append_line(out, b + 1, "return universal_protocol_runtime::DecodeStatus::kChecksumMismatch;");
    append_line(out, b, "}");
  }

  append_line(out, b, "if (bytes_consumed != nullptr) {");
  append_line(out, b + 1, "*bytes_consumed = offset;");
  append_line(out, b, "}");
  append_cpp_decode_error_set(out, b, "kOk", "\"\"", "offset");
  append_line(out, b, "return universal_protocol_runtime::DecodeStatus::kOk;");
  append_line(out, indent, "}");
}

void append_cpp_direct_decode_function(std::string* out,
                                       const CompiledProtocol& protocol,
                                       const CompiledMessage& layout,
                                       bool is_message,
                                       bool supports_direct,
                                       int indent) {
  append_line(
      out,
      indent,
      "static constexpr bool kSupportsDirectValueDecode = " + std::string(supports_direct ? "true" : "false") + ";");
  if (!supports_direct) {
    append_line(out, indent, "static universal_protocol_runtime::DecodeStatus decode_value_direct(");
    append_line(out, indent + 1, "universal_protocol_runtime::ByteSpan /*frame*/,");
    append_line(out, indent + 1, "Value* /*value*/,");
    append_line(out, indent + 1, "std::size_t* /*bytes_consumed*/ = nullptr,");
    append_line(out, indent + 1, "universal_protocol_runtime::DecodeError* /*error*/ = nullptr) {");
    append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
    append_line(out, indent, "}");
    return;
  }
  if (layout_supports_simple_direct_codec(layout)) {
    append_cpp_simple_direct_decode_function(out, layout, is_message, indent);
  } else {
    append_cpp_general_direct_decode_function(out, protocol, layout, is_message, indent);
  }
}

void append_cpp_simple_direct_encode_function(std::string* out, const CompiledMessage& layout, int indent) {
  auto is_checksum_field_enc = [&](FieldId fid) -> bool {
    return std::any_of(layout.checksums().begin(), layout.checksums().end(), [fid](const CompiledChecksum& c) {
      return c.field_id == fid;
    });
  };
  const bool needs_checksum_field_arrays = encode_layout_needs_checksum_field_arrays(layout);

  append_line(out, indent, "static universal_protocol_runtime::EncodeStatus encode_value_direct(");
  append_line(out, indent + 1, "const Value& value,");
  append_line(out, indent + 1, "universal_protocol_runtime::MutableByteSpan frame,");
  append_line(out, indent + 1, "std::size_t* bytes_written = nullptr) {");
  append_line(out, indent + 1, "std::size_t total_size = 0;");
  for (const CompiledField& field : layout.fields()) {
    const std::string field_name = sanitize_namespace_name(field.name);
    const std::string field_size_constant_name = sanitize_cpp_constant_name(field.name) + "Size";
    if (field.kind == FieldKind::kBytes || field.kind == FieldKind::kString) {
      if (field.dynamic_size) {
        append_line_cat(out, indent + 1, "const auto ", field_name, "_size = value.", field_name, ".size();");
        append_line(out, indent + 1, "if (" + field_name + "_size > frame.size() - total_size) {");
        append_line(out, indent + 2, "return universal_protocol_runtime::EncodeStatus::kBufferTooSmall;");
        append_line(out, indent + 1, "}");
        append_line_cat(out, indent + 1, "total_size += ", field_name, "_size;");
      } else {
        append_line(
            out,
            indent + 1,
            "constexpr std::size_t " + field_size_constant_name + " = " + std::to_string(field.fixed_size) + ";");
        append_line(out, indent + 1, "total_size += " + field_size_constant_name + ";");
      }
    } else {
      append_line(
          out,
          indent + 1,
          "constexpr std::size_t " + field_size_constant_name + " = " + std::to_string(field.width_bytes) + ";");
      append_line(out, indent + 1, "total_size += " + field_size_constant_name + ";");
    }
  }
  append_line(out, indent + 1, "if (frame.size() < total_size) {");
  append_line(out, indent + 2, "return universal_protocol_runtime::EncodeStatus::kBufferTooSmall;");
  append_line(out, indent + 1, "}");
  if (!layout.checksums().empty() && needs_checksum_field_arrays) {
    append_line(
        out, indent + 1, "std::array<std::size_t, " + std::to_string(layout.fields().size()) + "> field_starts{};");
    append_line(
        out, indent + 1, "std::array<std::size_t, " + std::to_string(layout.fields().size()) + "> field_ends{};");
  }
  append_line(out, indent + 1, "std::byte* cursor = frame.data();");
  append_line(out, indent + 1, "std::size_t offset = 0;");

  for (const CompiledField& field : layout.fields()) {
    const std::string field_name = sanitize_namespace_name(field.name);
    const std::string field_constant_name = cpp_field_constant_name(field);
    const std::string field_size_constant_name = sanitize_cpp_constant_name(field.name) + "Size";
    if (!layout.checksums().empty() && needs_checksum_field_arrays) {
      append_line(out, indent + 1, "field_starts[" + field_constant_name + "] = offset;");
    } else if (is_checksum_field_enc(field.id)) {
      const auto checksum_it =
          std::find_if(layout.checksums().begin(), layout.checksums().end(), [&](const CompiledChecksum& checksum) {
            return checksum.field_id == field.id;
          });
      if (checksum_it != layout.checksums().end() && checksum_uses_prefix_shortcut(*checksum_it)) {
        append_line(out, indent + 1, "const std::size_t " + checksum_field_offset_name(field) + " = offset;");
      }
    }
    if (is_checksum_field_enc(field.id)) {
      append_line(out,
                  indent + 1,
                  "universal_protocol_runtime::direct_encode_support::fill_zeros("
                  "universal_protocol_runtime::MutableByteSpan(cursor, " +
                      field_size_constant_name + "));");
      append_line(out, indent + 1, "cursor += " + field_size_constant_name + ";");
      append_line(out, indent + 1, "offset += " + field_size_constant_name + ";");
    } else if (field.kind == FieldKind::kBytes || field.kind == FieldKind::kString) {
      if (field.dynamic_size) {
        append_line(out, indent + 1, "if (" + field_name + "_size > 0U) {");
        append_line_cat(out,
                        indent + 2,
                        "universal_protocol_runtime::direct_encode_support::write_bytes_unchecked(",
                        "universal_protocol_runtime::MutableByteSpan(cursor, ",
                        field_name,
                        "_size), universal_protocol_runtime::ByteSpan(",
                        "reinterpret_cast<const std::byte*>(value.",
                        field_name,
                        ".data()), ",
                        field_name,
                        "_size));");
        append_line(out, indent + 1, "}");
        append_line_cat(out, indent + 1, "cursor += ", field_name, "_size;");
        append_line(out, indent + 1, "offset += " + field_name + "_size;");
      } else {
        append_line(out, indent + 1, "if (" + field_size_constant_name + " > 0U) {");
        append_line_cat(out,
                        indent + 2,
                        "universal_protocol_runtime::direct_encode_support::write_bytes_unchecked(",
                        "universal_protocol_runtime::MutableByteSpan(cursor, ",
                        field_size_constant_name,
                        "), universal_protocol_runtime::ByteSpan(",
                        "reinterpret_cast<const std::byte*>(value.",
                        field_name,
                        ".data()), ",
                        field_size_constant_name,
                        "));");
        append_line(out, indent + 1, "}");
        append_line(out, indent + 1, "cursor += " + field_size_constant_name + ";");
        append_line(out, indent + 1, "offset += " + field_size_constant_name + ";");
      }
    } else {
      switch (field.kind) {
        case FieldKind::kUnsigned:
        case FieldKind::kEnum: {
          const std::string value_expr = field.has_expected_unsigned
                                             ? std::to_string(field.expected_unsigned) + "ULL"
                                             : "static_cast<uint64_t>(value." + field_name + ")";
          append_line_cat(out,
                          indent + 1,
                          "universal_protocol_runtime::direct_encode_support::write_unsigned_scalar_unchecked<",
                          cpp_byte_order_literal(field.byte_order),
                          ", ",
                          std::to_string(field.width_bytes),
                          ">(universal_protocol_runtime::MutableByteSpan(cursor, ",
                          std::to_string(field.width_bytes),
                          "), ",
                          value_expr,
                          ");");
          break;
        }
        case FieldKind::kSigned:
          append_line_cat(out,
                          indent + 1,
                          "universal_protocol_runtime::direct_encode_support::write_unsigned_scalar_unchecked<",
                          cpp_byte_order_literal(field.byte_order),
                          ", ",
                          std::to_string(field.width_bytes),
                          ">(universal_protocol_runtime::MutableByteSpan(cursor, ",
                          std::to_string(field.width_bytes),
                          "), static_cast<uint64_t>(value.",
                          field_name,
                          "));");
          break;
        case FieldKind::kFloat32:
          append_line_cat(out,
                          indent + 1,
                          "universal_protocol_runtime::direct_encode_support::write_float32_unchecked<",
                          cpp_byte_order_literal(field.byte_order),
                          ">(universal_protocol_runtime::MutableByteSpan(cursor, ",
                          std::to_string(field.width_bytes),
                          "), value.",
                          field_name,
                          ");");
          break;
        case FieldKind::kFloat64:
          append_line_cat(out,
                          indent + 1,
                          "universal_protocol_runtime::direct_encode_support::write_float64_unchecked<",
                          cpp_byte_order_literal(field.byte_order),
                          ">(universal_protocol_runtime::MutableByteSpan(cursor, ",
                          std::to_string(field.width_bytes),
                          "), value.",
                          field_name,
                          ");");
          break;
        case FieldKind::kBytes:
        case FieldKind::kString:
        case FieldKind::kStruct:
        case FieldKind::kCollection:
        case FieldKind::kVariant:
          break;
      }
      append_line(out, indent + 1, "cursor += " + field_size_constant_name + ";");
      append_line(out, indent + 1, "offset += " + field_size_constant_name + ";");
    }
    if (!layout.checksums().empty() && needs_checksum_field_arrays) {
      append_line(out, indent + 1, "field_ends[" + field_constant_name + "] = offset;");
    }
  }

  for (const CompiledChecksum& checksum : layout.checksums()) {
    const std::string chk_field_name = sanitize_namespace_name(layout.fields()[checksum.field_id].name);
    const std::string from_name = chk_field_name + "_checksum_from";
    const std::string to_name = chk_field_name + "_checksum_to";
    std::string checksum_value_expr;
    std::string destination_offset_expr;
    if (checksum_uses_prefix_shortcut(checksum)) {
      destination_offset_expr = checksum_field_offset_name(layout.fields()[checksum.field_id]);
      append_line_cat(out, indent + 1, "const std::size_t ", from_name, " = 0U;");
      if (const auto static_span = checksum_static_span_size(layout, checksum); static_span.has_value()) {
        const std::string fixed_call =
            cpp_direct_fixed_checksum_call(checksum.algorithm_name, "frame.data() + " + from_name, *static_span);
        if (!fixed_call.empty()) {
          checksum_value_expr = fixed_call;
        }
      }
      if (checksum_value_expr.empty()) {
        append_line_cat(out, indent + 1, "const std::size_t ", to_name, " = ", destination_offset_expr, ";");
      }
    } else {
      destination_offset_expr = "field_starts[" + cpp_field_constant_name(layout.fields()[checksum.field_id]) + "]";
      append_line_cat(out,
                      indent + 1,
                      "const std::size_t ",
                      from_name,
                      " = ",
                      cpp_checksum_anchor_expression(layout, checksum.from, "offset"),
                      ";");
      append_line_cat(out,
                      indent + 1,
                      "const std::size_t ",
                      to_name,
                      " = ",
                      cpp_checksum_anchor_expression(layout, checksum.to, "offset"),
                      ";");
    }
    if (checksum_value_expr.empty()) {
      std::string chk_slice_expr;
      chk_slice_expr.reserve(64U + (from_name.size() * 2U) + to_name.size());
      chk_slice_expr += "universal_protocol_runtime::ByteSpan(frame.data() + ";
      chk_slice_expr += from_name;
      chk_slice_expr += ", ";
      chk_slice_expr += to_name;
      chk_slice_expr += " - ";
      chk_slice_expr += from_name;
      chk_slice_expr += ")";
      checksum_value_expr = cpp_direct_checksum_call(checksum.algorithm_name, chk_slice_expr);
    }
    append_line_cat(out,
                    indent + 1,
                    "universal_protocol_runtime::direct_encode_support::write_unsigned_scalar_unchecked<",
                    cpp_byte_order_literal(layout.fields()[checksum.field_id].byte_order),
                    ", ",
                    std::to_string(checksum.result_width_bytes),
                    ">(universal_protocol_runtime::MutableByteSpan(frame.data() + ",
                    destination_offset_expr,
                    ", ",
                    std::to_string(checksum.result_width_bytes),
                    "), ",
                    checksum_value_expr,
                    ");");
  }

  append_line(out, indent + 1, "if (bytes_written != nullptr) {");
  append_line(out, indent + 2, "*bytes_written = total_size;");
  append_line(out, indent + 1, "}");
  append_line(out, indent + 1, "return universal_protocol_runtime::EncodeStatus::kOk;");
  append_line(out, indent, "}");
}

void append_cpp_encode_align_offset(std::string* out, const CompiledField& field, int indent) {
  if (field.alignment <= 1U) {
    return;
  }
  const std::string mask = std::to_string(field.alignment - 1U);
  append_line_cat(out,
                  indent,
                  "const std::size_t aligned_offset = (offset + ",
                  mask,
                  "U) & ~static_cast<std::size_t>(",
                  mask,
                  "U);");
  append_line(out, indent, "if (aligned_offset > frame.size()) {");
  append_line(out, indent + 1, "return universal_protocol_runtime::EncodeStatus::kBufferTooSmall;");
  append_line(out, indent, "}");
  append_line(out, indent, "if (aligned_offset != offset) {");
  append_line(out,
              indent + 1,
              "universal_protocol_runtime::direct_encode_support::fill_zeros("
              "universal_protocol_runtime::MutableByteSpan(frame.data() + offset, aligned_offset - offset));");
  append_line(out, indent + 1, "offset = aligned_offset;");
  append_line(out, indent, "}");
}

// Emits the encode core for one field of a general layout. Assumes `frame`,
// `offset`, and `value` are in scope. Checksum fields are zero-filled here and
// resolved after the field loop.
void append_cpp_general_encode_field_body(std::string* out,
                                          const CompiledProtocol& protocol,
                                          const CompiledMessage& layout,
                                          const CompiledField& field,
                                          bool is_checksum_field,
                                          int indent) {
  const std::string fname = sanitize_namespace_name(field.name);
  const std::string size_const = sanitize_cpp_constant_name(field.name) + "Size";
  const std::string dst = "universal_protocol_runtime::MutableByteSpan(frame.data() + offset, ";
  const auto buffer_check = [&](const std::string& size_expr) {
    append_line(out, indent, "if (" + size_expr + " > frame.size() - offset) {");
    append_line(out, indent + 1, "return universal_protocol_runtime::EncodeStatus::kBufferTooSmall;");
    append_line(out, indent, "}");
  };

  if (is_checksum_field) {
    append_line(out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.width_bytes) + ";");
    buffer_check(size_const);
    append_line_cat(
        out, indent, "universal_protocol_runtime::direct_encode_support::fill_zeros(", dst, size_const, "));");
    append_line(out, indent, "offset += " + size_const + ";");
    return;
  }

  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kEnum: {
      append_line(out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.width_bytes) + ";");
      buffer_check(size_const);
      const std::string value_expr = field.has_expected_unsigned ? std::to_string(field.expected_unsigned) + "ULL"
                                                                 : "static_cast<uint64_t>(value." + fname + ")";
      append_line_cat(out,
                      indent,
                      "universal_protocol_runtime::direct_encode_support::write_unsigned_scalar_unchecked<",
                      cpp_byte_order_literal(field.byte_order),
                      ", ",
                      std::to_string(field.width_bytes),
                      ">(",
                      dst,
                      std::to_string(field.width_bytes),
                      "), ",
                      value_expr,
                      ");");
      append_line(out, indent, "offset += " + size_const + ";");
      return;
    }
    case FieldKind::kSigned: {
      append_line(out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.width_bytes) + ";");
      buffer_check(size_const);
      append_line_cat(out,
                      indent,
                      "universal_protocol_runtime::direct_encode_support::write_unsigned_scalar_unchecked<",
                      cpp_byte_order_literal(field.byte_order),
                      ", ",
                      std::to_string(field.width_bytes),
                      ">(",
                      dst,
                      std::to_string(field.width_bytes),
                      "), static_cast<uint64_t>(value.",
                      fname,
                      "));");
      append_line(out, indent, "offset += " + size_const + ";");
      return;
    }
    case FieldKind::kFloat32:
    case FieldKind::kFloat64: {
      const bool is32 = field.kind == FieldKind::kFloat32;
      append_line(out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.width_bytes) + ";");
      buffer_check(size_const);
      append_line_cat(out,
                      indent,
                      "universal_protocol_runtime::direct_encode_support::",
                      is32 ? "write_float32_unchecked<" : "write_float64_unchecked<",
                      cpp_byte_order_literal(field.byte_order),
                      ">(",
                      dst,
                      std::to_string(field.width_bytes),
                      "), value.",
                      fname,
                      ");");
      append_line(out, indent, "offset += " + size_const + ";");
      return;
    }
    case FieldKind::kBytes: {
      if (field.dynamic_size) {
        append_line_cat(out, indent, "const std::size_t ", fname, "_size = value.", fname, ".size();");
        buffer_check(fname + "_size");
        append_line_cat(out,
                        indent,
                        "universal_protocol_runtime::direct_encode_support::write_bytes_unchecked(",
                        dst,
                        fname,
                        "_size), value.",
                        fname,
                        ");");
        append_line(out, indent, "offset += " + fname + "_size;");
      } else {
        append_line(
            out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.fixed_size) + ";");
        buffer_check(size_const);
        if (field.is_reserved) {
          append_line_cat(out,
                          indent,
                          "std::memset(frame.data() + offset, ",
                          std::to_string(static_cast<unsigned>(field.reserved_fill_byte)),
                          ", ",
                          size_const,
                          ");");
        } else {
          append_line_cat(out,
                          indent,
                          "universal_protocol_runtime::direct_encode_support::write_bytes_unchecked(",
                          dst,
                          size_const,
                          "), universal_protocol_runtime::ByteSpan(value.",
                          fname,
                          ".data(), ",
                          size_const,
                          "));");
        }
        append_line(out, indent, "offset += " + size_const + ";");
      }
      return;
    }
    case FieldKind::kString: {
      const std::string size_expr = field.dynamic_size ? (fname + "_size") : size_const;
      if (field.dynamic_size) {
        append_line_cat(out, indent, "const std::size_t ", fname, "_size = value.", fname, ".size();");
      } else {
        append_line(
            out, indent, "constexpr std::size_t " + size_const + " = " + std::to_string(field.fixed_size) + ";");
      }
      buffer_check(size_expr);
      append_line_cat(out,
                      indent,
                      "universal_protocol_runtime::direct_encode_support::write_bytes_unchecked(",
                      dst,
                      size_expr,
                      "), universal_protocol_runtime::ByteSpan(reinterpret_cast<const std::byte*>(value.",
                      fname,
                      ".data()), ",
                      size_expr,
                      "));");
      append_line(out, indent, "offset += " + size_expr + ";");
      return;
    }
    case FieldKind::kStruct: {
      append_line(out, indent, "std::size_t " + fname + "_written = 0;");
      append_line_cat(out,
                      indent,
                      "const auto ",
                      fname,
                      "_status = structs::",
                      struct_type_name(protocol, field.struct_id),
                      "::encode_value_direct(value.",
                      fname,
                      ", universal_protocol_runtime::MutableByteSpan(frame.data() + offset, frame.size() - offset), &",
                      fname,
                      "_written);");
      append_line(out, indent, "if (" + fname + "_status != universal_protocol_runtime::EncodeStatus::kOk) {");
      append_line(out, indent + 1, "return " + fname + "_status;");
      append_line(out, indent, "}");
      append_line(out, indent, "offset += " + fname + "_written;");
      return;
    }
    case FieldKind::kCollection: {
      append_line(out, indent, "for (const auto& " + fname + "_element : value." + fname + ") {");
      append_line(out, indent + 1, "std::size_t " + fname + "_written = 0;");
      append_line_cat(out,
                      indent + 1,
                      "const auto ",
                      fname,
                      "_status = structs::",
                      struct_type_name(protocol, field.struct_id),
                      "::encode_value_direct(",
                      fname,
                      "_element, universal_protocol_runtime::MutableByteSpan(frame.data() + offset, frame.size() - "
                      "offset), &",
                      fname,
                      "_written);");
      append_line(out, indent + 1, "if (" + fname + "_status != universal_protocol_runtime::EncodeStatus::kOk) {");
      append_line(out, indent + 2, "return " + fname + "_status;");
      append_line(out, indent + 1, "}");
      append_line(out, indent + 1, "offset += " + fname + "_written;");
      append_line(out, indent, "}");
      return;
    }
    case FieldKind::kVariant: {
      append_line_cat(out,
                      indent,
                      "const uint64_t ",
                      fname,
                      "_tag = static_cast<uint64_t>(value.",
                      sanitize_namespace_name(layout.fields()[field.tag_from_field].name),
                      ");");
      append_line(out, indent, "std::size_t " + fname + "_written = 0;");
      append_line(out, indent, "switch (" + fname + "_tag) {");
      for (size_t case_index = 0; case_index < field.variant_cases.size(); ++case_index) {
        const CompiledVariantCase& variant_case = field.variant_cases[case_index];
        const std::string case_type = struct_type_name(protocol, variant_case.struct_id);
        append_line(out, indent + 1, "case " + std::to_string(variant_case.tag_value) + "ULL: {");
        append_line_cat(out,
                        indent + 2,
                        "const auto* ",
                        fname,
                        "_case = std::get_if<",
                        std::to_string(case_index + 1U),
                        ">(&value.",
                        fname,
                        ");");
        append_line(out, indent + 2, "if (" + fname + "_case == nullptr) {");
        append_line(out, indent + 3, "return universal_protocol_runtime::EncodeStatus::kInvalidData;");
        append_line(out, indent + 2, "}");
        append_line_cat(out,
                        indent + 2,
                        "const auto ",
                        fname,
                        "_status = structs::",
                        case_type,
                        "::encode_value_direct(*",
                        fname,
                        "_case, universal_protocol_runtime::MutableByteSpan(frame.data() + offset, frame.size() - "
                        "offset), &",
                        fname,
                        "_written);");
        append_line(out, indent + 2, "if (" + fname + "_status != universal_protocol_runtime::EncodeStatus::kOk) {");
        append_line(out, indent + 3, "return " + fname + "_status;");
        append_line(out, indent + 2, "}");
        append_line(out, indent + 2, "break;");
        append_line(out, indent + 1, "}");
      }
      append_line(out, indent + 1, "default:");
      append_line(out, indent + 2, "return universal_protocol_runtime::EncodeStatus::kInvalidData;");
      append_line(out, indent, "}");
      append_line(out, indent, "offset += " + fname + "_written;");
      return;
    }
  }
}

void append_cpp_general_direct_encode_function(std::string* out,
                                               const CompiledProtocol& protocol,
                                               const CompiledMessage& layout,
                                               int indent) {
  const bool has_checksums = !layout.checksums().empty();
  const int b = indent + 1;
  const auto is_checksum_field = [&](FieldId fid) -> bool {
    return std::any_of(layout.checksums().begin(), layout.checksums().end(), [fid](const CompiledChecksum& c) {
      return c.field_id == fid;
    });
  };

  append_line(out, indent, "static universal_protocol_runtime::EncodeStatus encode_value_direct(");
  append_line(out, indent + 1, "const Value& value,");
  append_line(out, indent + 1, "universal_protocol_runtime::MutableByteSpan frame,");
  append_line(out, indent + 1, "std::size_t* bytes_written = nullptr) {");
  append_line(out, b, "std::size_t offset = 0;");
  if (has_checksums) {
    append_line(out, b, "std::array<std::size_t, " + std::to_string(layout.fields().size()) + "> field_starts{};");
    append_line(out, b, "std::array<std::size_t, " + std::to_string(layout.fields().size()) + "> field_ends{};");
  }

  for (const CompiledField& field : layout.fields()) {
    const std::string fname = sanitize_namespace_name(field.name);
    const std::string fconst = cpp_field_constant_name(field);
    const bool gated = field.has_condition || field.has_presence;
    append_line(out, b, "{");
    int fb = b + 1;
    if (gated) {
      append_line(
          out,
          fb,
          "const bool " + fname + "_present = " + cpp_presence_condition_expression(layout, field, "value.") + ";");
      append_line(out, fb, "if (" + fname + "_present) {");
      fb += 1;
    }
    append_cpp_encode_align_offset(out, field, fb);
    if (has_checksums) {
      append_line(out, fb, "field_starts[" + fconst + "] = offset;");
    }
    append_cpp_general_encode_field_body(out, protocol, layout, field, is_checksum_field(field.id), fb);
    if (has_checksums) {
      append_line(out, fb, "field_ends[" + fconst + "] = offset;");
    }
    if (gated) {
      fb -= 1;
      append_line(out, fb, "}");
      if (has_checksums) {
        append_line(out, fb, "else {");
        append_line(out, fb + 1, "field_starts[" + fconst + "] = offset;");
        append_line(out, fb + 1, "field_ends[" + fconst + "] = offset;");
        append_line(out, fb, "}");
      }
    }
    append_line(out, b, "}");
  }

  for (const CompiledChecksum& checksum : layout.checksums()) {
    const std::string field_name = sanitize_namespace_name(layout.fields()[checksum.field_id].name);
    const std::string from_name = field_name + "_checksum_from";
    const std::string to_name = field_name + "_checksum_to";
    append_line_cat(out,
                    b,
                    "const std::size_t ",
                    from_name,
                    " = ",
                    cpp_checksum_anchor_expression(layout, checksum.from, "offset"),
                    ";");
    append_line_cat(out,
                    b,
                    "const std::size_t ",
                    to_name,
                    " = ",
                    cpp_checksum_anchor_expression(layout, checksum.to, "offset"),
                    ";");
    std::string slice = "universal_protocol_runtime::ByteSpan(frame.data() + ";
    slice += from_name;
    slice += ", ";
    slice += to_name;
    slice += " - ";
    slice += from_name;
    slice += ")";
    append_line_cat(out,
                    b,
                    "universal_protocol_runtime::direct_encode_support::write_unsigned_scalar_unchecked<",
                    cpp_byte_order_literal(layout.fields()[checksum.field_id].byte_order),
                    ", ",
                    std::to_string(checksum.result_width_bytes),
                    ">(universal_protocol_runtime::MutableByteSpan(frame.data() + field_starts[",
                    cpp_field_constant_name(layout.fields()[checksum.field_id]),
                    "], ",
                    std::to_string(checksum.result_width_bytes),
                    "), ",
                    cpp_direct_checksum_call(checksum.algorithm_name, slice),
                    ");");
  }

  append_line(out, b, "if (bytes_written != nullptr) {");
  append_line(out, b + 1, "*bytes_written = offset;");
  append_line(out, b, "}");
  append_line(out, b, "return universal_protocol_runtime::EncodeStatus::kOk;");
  append_line(out, indent, "}");
}

void append_cpp_direct_encode_function(std::string* out,
                                       const CompiledProtocol& protocol,
                                       const CompiledMessage& layout,
                                       bool supports_direct,
                                       int indent) {
  append_line(
      out,
      indent,
      "static constexpr bool kSupportsDirectValueEncode = " + std::string(supports_direct ? "true" : "false") + ";");
  if (!supports_direct) {
    append_line(out, indent, "static universal_protocol_runtime::EncodeStatus encode_value_direct(");
    append_line(out, indent + 1, "const Value& /*value*/,");
    append_line(out, indent + 1, "universal_protocol_runtime::MutableByteSpan /*frame*/,");
    append_line(out, indent + 1, "std::size_t* /*bytes_written*/ = nullptr) {");
    append_line(out, indent + 1, "return universal_protocol_runtime::EncodeStatus::kInvalidData;");
    append_line(out, indent, "}");
    return;
  }
  if (layout_supports_simple_direct_codec(layout)) {
    append_cpp_simple_direct_encode_function(out, layout, indent);
  } else {
    append_cpp_general_direct_encode_function(out, protocol, layout, indent);
  }
}

void append_cpp_encoder_binding_class(std::string* out, const CompiledMessage& /* layout */, int indent) {
  append_line(out, indent, "class Encoder final {");
  append_line(out, indent, " public:");
  append_line(out, indent + 1, "explicit Encoder(const universal_protocol_runtime::ProtocolEncoder& encoder)");
  append_line(out, indent + 2, ": layout_(encoder.find_message(kName)) {}");
  append_line(out, indent + 1, "bool available() const { return layout_ != nullptr; }");
  append_line(out, indent + 1, "universal_protocol_runtime::EncodeStatus encode(const Value& value,");
  append_line(out, indent + 2, "universal_protocol_runtime::MutableByteSpan frame,");
  append_line(out, indent + 2, "std::size_t* bytes_written = nullptr) const {");
  append_line(out, indent + 2, "if (!available()) {");
  append_line(out, indent + 3, "return universal_protocol_runtime::EncodeStatus::kSchemaMismatch;");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "if constexpr (kSupportsDirectValueEncode) {");
  append_line(out, indent + 3, "return encode_value_direct(value, frame, bytes_written);");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "return universal_protocol_runtime::EncodeStatus::kInvalidData;");
  append_line(out, indent + 1, "}");
  append_line(out, indent, " private:");
  append_line(out, indent + 1, "const universal_protocol_runtime::CompiledMessage* layout_ = nullptr;");
  append_line(out, indent, "};");
}

// Emits a first-class helper that decodes a packed sequence of records from a
// frame, tracking byte consumption internally so callers no longer have to
// thread bytes_consumed by hand. Most useful for struct records (length-bounded
// per element); message layouts that forbid trailing bytes decode a single
// record. On failure the rich DecodeError pinpoints the failing record.
void append_cpp_decode_sequence_function(std::string* out, int indent) {
  append_line(out, indent, "static universal_protocol_runtime::DecodeStatus decode_sequence(");
  append_line(out, indent + 1, "universal_protocol_runtime::ByteSpan frame,");
  append_line(out, indent + 1, "std::vector<Value>* out,");
  append_line(out, indent + 1, "std::size_t* records_decoded = nullptr,");
  append_line(out, indent + 1, "universal_protocol_runtime::DecodeError* error = nullptr) {");
  append_line(out, indent + 1, "if (out == nullptr) {");
  append_line(out,
              indent + 2,
              "if (error != nullptr) { *error = {universal_protocol_runtime::DecodeStatus::kInvalidData, \"\", 0}; }");
  append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
  append_line(out, indent + 1, "}");
  append_line(out, indent + 1, "std::size_t offset = 0;");
  append_line(out, indent + 1, "std::size_t count = 0;");
  append_line(out, indent + 1, "while (offset < frame.size()) {");
  append_line(out, indent + 2, "Value record{};");
  append_line(out, indent + 2, "std::size_t consumed = 0;");
  append_line(
      out, indent + 2, "const auto status = decode_value_direct(frame.subspan(offset), &record, &consumed, error);");
  append_line(out, indent + 2, "if (status != universal_protocol_runtime::DecodeStatus::kOk) {");
  append_line(out, indent + 3, "if (error != nullptr) { error->byte_offset += offset; }");
  append_line(out, indent + 3, "return status;");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "if (consumed == 0) {");
  append_line(
      out,
      indent + 3,
      "if (error != nullptr) { *error = {universal_protocol_runtime::DecodeStatus::kSchemaMismatch, \"\", offset}; }");
  append_line(out, indent + 3, "return universal_protocol_runtime::DecodeStatus::kSchemaMismatch;");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "offset += consumed;");
  append_line(out, indent + 2, "out->push_back(std::move(record));");
  append_line(out, indent + 2, "++count;");
  append_line(out, indent + 1, "}");
  append_line(out, indent + 1, "if (records_decoded != nullptr) {");
  append_line(out, indent + 2, "*records_decoded = count;");
  append_line(out, indent + 1, "}");
  append_line(out,
              indent + 1,
              "if (error != nullptr) { *error = {universal_protocol_runtime::DecodeStatus::kOk, \"\", offset}; }");
  append_line(out, indent + 1, "return universal_protocol_runtime::DecodeStatus::kOk;");
  append_line(out, indent, "}");
}

void append_cpp_view_binding(std::string* out,
                             const CompiledProtocol& protocol,
                             const CompiledMessage& layout,
                             bool is_message,
                             bool supports_direct_value_decode,
                             int indent) {
  const bool general = layout_uses_general_direct_codec(protocol, layout);
  append_line(
      out,
      indent,
      "// Zero-copy bytes/string fields in Value borrow from the decoded frame; keep that frame alive or copy them.");
  append_line(out, indent, "struct Value final {");
  for (const CompiledField& field : layout.fields()) {
    append_cpp_value_field(out, protocol, field, general, indent + 1);
  }
  append_line(out, indent, "};");
  append_cpp_direct_decode_function(out, protocol, layout, is_message, supports_direct_value_decode, indent);
  append_cpp_direct_encode_function(out, protocol, layout, supports_direct_value_decode, indent);
  if (supports_direct_value_decode) {
    append_cpp_decode_sequence_function(out, indent);
  }
  append_line(out);
  append_line(out, indent, "class View final {");
  append_line(out, indent, " public:");
  append_line(out, indent + 1, "View() = default;");
  append_line(out,
              indent + 1,
              "explicit View(const universal_protocol_runtime::DecodedMessage& message) : message_(&message) {}");
  append_line(
      out, indent + 1, "static std::optional<View> bind(const universal_protocol_runtime::DecodedMessage& message) {");
  append_line(out, indent + 2, "if (!message.valid() || message.message_name() != kName) {");
  append_line(out, indent + 3, "return std::nullopt;");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "return View(message);");
  append_line(out, indent + 1, "}");
  append_line(
      out,
      indent + 1,
      "bool valid() const { return message_ != nullptr && message_->valid() && message_->message_name() == kName; }");
  append_line(
      out, indent + 1, "const universal_protocol_runtime::DecodedMessage& decoded() const { return *message_; }");
  for (const CompiledField& field : layout.fields()) {
    append_cpp_view_accessor(out, field, indent + 1);
  }
  append_line(out, indent, " private:");
  append_line(out, indent + 1, "const universal_protocol_runtime::DecodedMessage* message_ = nullptr;");
  append_line(out, indent, "};");

  if (!is_message) {
    return;
  }

  append_line(out, indent, "class Decoder final {");
  append_line(out, indent, " public:");
  append_line(out, indent + 1, "explicit Decoder(const universal_protocol_runtime::ProtocolDecoder& decoder)");
  append_line(out, indent + 2, ": decoder_(&decoder),");
  append_line(out, indent + 2, "layout_(decoder.protocol()->find_message(kName)) {}");
  append_line(out, indent + 1, "bool available() const { return layout_ != nullptr; }");
  append_line(
      out, indent + 1, "universal_protocol_runtime::DecodeStatus decode(universal_protocol_runtime::ByteSpan frame,");
  append_line(out, indent + 2, "universal_protocol_runtime::DecodedMessage* message) const {");
  append_line(out, indent + 2, "if (message == nullptr || !available()) {");
  append_line(out, indent + 3, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "return decoder_->decode_as(*layout_, frame, message);");
  append_line(out, indent + 1, "}");
  append_line(out, indent + 1, "std::optional<View> decode_view(universal_protocol_runtime::ByteSpan frame,");
  append_line(out, indent + 2, "universal_protocol_runtime::DecodedMessage* message) const {");
  append_line(out, indent + 2, "if (decode(frame, message) != universal_protocol_runtime::DecodeStatus::kOk) {");
  append_line(out, indent + 3, "return std::nullopt;");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "return View(*message);");
  append_line(out, indent + 1, "}");
  append_line(out,
              indent + 1,
              "universal_protocol_runtime::DecodeStatus decode_value(universal_protocol_runtime::ByteSpan frame,");
  append_line(out, indent + 2, "Value* value,");
  append_line(out, indent + 2, "universal_protocol_runtime::DecodeError* error = nullptr) const {");
  append_line(out, indent + 2, "if (value == nullptr) {");
  append_line(out,
              indent + 3,
              "if (error != nullptr) { *error = {universal_protocol_runtime::DecodeStatus::kInvalidData, \"\", 0}; }");
  append_line(out, indent + 3, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "if constexpr (kSupportsDirectValueDecode) {");
  append_line(out, indent + 3, "return decode_value_direct(frame, value, nullptr, error);");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "universal_protocol_runtime::DecodedMessage decoded;");
  append_line(out, indent + 2, "return decode_value(frame, &decoded, value);");
  append_line(out, indent + 1, "}");
  append_line(out,
              indent + 1,
              "universal_protocol_runtime::DecodeStatus decode_value(universal_protocol_runtime::ByteSpan frame,");
  append_line(out, indent + 2, "universal_protocol_runtime::DecodedMessage* message,");
  append_line(out, indent + 2, "Value* value) const {");
  append_line(out, indent + 2, "if (value == nullptr) {");
  append_line(out, indent + 3, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "universal_protocol_runtime::DecodedMessage decoded_storage;");
  append_line(out, indent + 2, "auto* source_message = message;");
  append_line(out, indent + 2, "if (source_message == nullptr) {");
  append_line(out, indent + 3, "source_message = &decoded_storage;");
  append_line(out, indent + 2, "}");
  append_line(out, indent + 2, "const auto status = decode(frame, source_message);");
  append_line(out, indent + 2, "if (status != universal_protocol_runtime::DecodeStatus::kOk) {");
  append_line(out, indent + 3, "return status;");
  append_line(out, indent + 2, "}");
  if (general) {
    // The owned Value carries nested/collection/variant members that cannot be
    // produced by the borrowed-view extraction below, so fill it via the
    // byte-for-byte direct path (always available for general layouts).
    append_line(out, indent + 2, "return decode_value_direct(frame, value);");
  } else {
    for (const CompiledField& field : layout.fields()) {
      const std::string field_name = sanitize_namespace_name(field.name);
      const std::string value_name = field_name + "_value";
      std::string extract_expression = cpp_generated_extract_expression(field);
      constexpr std::string_view kMessageAccess = "message->";
      const size_t access_position = extract_expression.find(kMessageAccess);
      if (access_position != std::string::npos) {
        extract_expression.replace(access_position, kMessageAccess.size(), "source_message->");
      }
      append_line_cat(out, indent + 2, "const auto ", value_name, " = ", extract_expression, ";");
      append_line(out, indent + 2, "if (!" + value_name + ".has_value()) {");
      append_line(out, indent + 3, "return universal_protocol_runtime::DecodeStatus::kInvalidData;");
      append_line(out, indent + 2, "}");
      append_line(
          out, indent + 2, "value->" + field_name + " = " + cpp_generated_extract_assignment(field, value_name) + ";");
    }
    append_line(out, indent + 2, "return universal_protocol_runtime::DecodeStatus::kOk;");
  }
  append_line(out, indent + 1, "}");
  append_line(out, indent, " private:");
  append_line(out, indent + 1, "const universal_protocol_runtime::ProtocolDecoder* decoder_ = nullptr;");
  append_line(out, indent + 1, "const universal_protocol_runtime::CompiledMessage* layout_ = nullptr;");
  append_line(out, indent, "};");
  append_cpp_encoder_binding_class(out, layout, indent);
}

void append_cpp_layout_binding(std::string* out,
                               const CompiledProtocol& protocol,
                               const CompiledMessage& layout,
                               bool is_message,
                               bool supports_direct_value_decode,
                               int indent) {
  append_line(out, indent, "struct " + sanitize_type_name(layout.name()) + " final {");
  append_line(
      out, indent + 1, "static constexpr std::string_view kName = \"" + escape_cpp_string(layout.name()) + "\";");
  append_line(
      out, indent + 1, "static constexpr std::size_t kMinimumSize = " + std::to_string(layout.minimum_size()) + ";");
  if (is_message) {
    append_line(out,
                indent + 1,
                "static constexpr bool kAllowTrailingBytes = " +
                    std::string(layout.allow_trailing_bytes() ? "true" : "false") + ";");
    append_line(out,
                indent + 1,
                "static constexpr auto kDispatchPrefix = " + cpp_byte_array_literal(layout.dispatch_prefix()) + ";");
  }

  append_cpp_id_struct(
      out,
      "Fields",
      "kEmpty",
      layout.fields(),
      "universal_protocol_runtime::FieldId",
      [](const CompiledField& field) -> std::string_view { return field.name; },
      [](const CompiledField& field) -> FieldId { return field.id; },
      indent + 1);
  append_cpp_id_struct(
      out,
      "BitFields",
      "kEmpty",
      layout.bit_fields(),
      "universal_protocol_runtime::BitFieldId",
      [](const CompiledBitField& bit_field) -> std::string_view { return bit_field.name; },
      [](const CompiledBitField& bit_field) -> BitFieldId { return bit_field.id; },
      indent + 1);

  append_cpp_binding_array(
      out,
      "static constexpr auto kFields = std::array<FieldBinding, " + std::to_string(layout.fields().size()) + ">",
      layout.fields(),
      cpp_field_binding_literal,
      indent + 1);
  append_cpp_binding_array(out,
                           "static constexpr auto kBitFields = std::array<BitFieldBinding, " +
                               std::to_string(layout.bit_fields().size()) + ">",
                           layout.bit_fields(),
                           cpp_bit_field_binding_literal,
                           indent + 1);
  append_cpp_binding_array(out,
                           "static constexpr auto kChecksums = std::array<ChecksumBinding, " +
                               std::to_string(layout.checksums().size()) + ">",
                           layout.checksums(),
                           cpp_checksum_binding_literal,
                           indent + 1);
  append_cpp_view_binding(out, protocol, layout, is_message, supports_direct_value_decode, indent + 1);
  append_line(out, indent, "};");
}

std::string python_bool(bool value) { return value ? "True" : "False"; }

std::string python_variant_cases_literal(const CompiledField& field) {
  std::string out = "(";
  for (const CompiledVariantCase& variant_case : field.variant_cases) {
    out += "_md.VariantCase(tag_value=" + std::to_string(variant_case.tag_value) +
           ", struct_id=" + std::to_string(variant_case.struct_id) + "), ";
  }
  out += ")";
  return out;
}

// Emits a metadata.Field(...) literal carrying every wire-relevant property.
// The field name is the sanitized Python identifier so it doubles as the dict
// key and the dataclass attribute name.
std::string python_field_literal(const CompiledField& field) {
  std::ostringstream stream;
  stream << "_md.Field(id=" << field.id << ", name=\"" << escape_python_string(sanitize_namespace_name(field.name))
         << "\", kind=\"" << to_string(field.kind) << "\", width_bytes=" << static_cast<unsigned>(field.width_bytes)
         << ", byte_order=\"" << to_string(field.byte_order) << "\", string_encoding=\""
         << to_string(field.string_encoding) << "\", fixed_size=" << field.fixed_size
         << ", dynamic_size=" << python_bool(field.dynamic_size) << ", size_from_field=" << field.size_from_field
         << ", struct_id=" << field.struct_id << ", alignment=" << field.alignment
         << ", is_reserved=" << python_bool(field.is_reserved)
         << ", reserved_fill_byte=" << static_cast<unsigned>(field.reserved_fill_byte)
         << ", fixed_count=" << field.fixed_count << ", dynamic_count=" << python_bool(field.dynamic_count)
         << ", count_from_field=" << field.count_from_field << ", has_condition=" << python_bool(field.has_condition)
         << ", condition_field=" << field.condition_field << ", condition_equals=" << field.condition_equals
         << ", has_presence=" << python_bool(field.has_presence) << ", presence_field=" << field.presence_field
         << ", presence_bit=" << static_cast<unsigned>(field.presence_bit)
         << ", tag_from_field=" << field.tag_from_field << ", variant_cases=" << python_variant_cases_literal(field)
         << ", has_expected_unsigned=" << python_bool(field.has_expected_unsigned)
         << ", expected_unsigned=" << field.expected_unsigned << ")";
  return stream.str();
}

std::string python_checksum_literal(const CompiledChecksum& checksum) {
  std::ostringstream stream;
  stream << "_md.Checksum(field_id=" << checksum.field_id
         << ", result_width_bytes=" << static_cast<unsigned>(checksum.result_width_bytes) << ", algorithm_name=\""
         << escape_python_string(checksum.algorithm_name) << "\", from_anchor=_md.ChecksumAnchor(kind=\""
         << checksum_anchor_kind_name(checksum.from.kind) << "\", field_id=" << checksum.from.field_id
         << "), to_anchor=_md.ChecksumAnchor(kind=\"" << checksum_anchor_kind_name(checksum.to.kind)
         << "\", field_id=" << checksum.to.field_id << "))";
  return stream.str();
}

void append_python_layout_literal(std::string* out, const CompiledMessage& layout, bool is_message, int indent) {
  append_line(out, indent, "_md.Layout(");
  append_line(out, indent + 1, "name=\"" + escape_python_string(sanitize_type_name(layout.name())) + "\",");
  append_line(out, indent + 1, "is_message=" + python_bool(is_message) + ",");
  append_line(out, indent + 1, "minimum_size=" + std::to_string(layout.minimum_size()) + ",");
  append_line(
      out, indent + 1, "allow_trailing_bytes=" + python_bool(is_message && layout.allow_trailing_bytes()) + ",");
  append_line(out,
              indent + 1,
              "dispatch_prefix=" +
                  python_bytes_literal(is_message ? layout.dispatch_prefix() : std::span<const std::byte>{}) + ",");
  append_line(out, indent + 1, "fields=(");
  for (const CompiledField& field : layout.fields()) {
    append_line(out, indent + 2, python_field_literal(field) + ",");
  }
  append_line(out, indent + 1, "),");
  append_line(out, indent + 1, "checksums=(");
  for (const CompiledChecksum& checksum : layout.checksums()) {
    append_line(out, indent + 2, python_checksum_literal(checksum) + ",");
  }
  append_line(out, indent + 1, "),");
  append_line(out, indent, "),");
}

std::string python_field_annotation(const CompiledProtocol& protocol, const CompiledField& field) {
  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kSigned:
    case FieldKind::kEnum:
      return "int";
    case FieldKind::kFloat32:
    case FieldKind::kFloat64:
      return "float";
    case FieldKind::kBytes:
      return "bytes";
    case FieldKind::kString:
      return "str";
    case FieldKind::kStruct:
      return "Optional[\"" + struct_type_name(protocol, field.struct_id) + "\"]";
    case FieldKind::kCollection:
      return "List[\"" + struct_type_name(protocol, field.struct_id) + "\"]";
    case FieldKind::kVariant:
      return "Optional[Any]";
  }
  return "Any";
}

std::string python_field_default(const CompiledField& field) {
  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kSigned:
    case FieldKind::kEnum:
      return "0";
    case FieldKind::kFloat32:
    case FieldKind::kFloat64:
      return "0.0";
    case FieldKind::kBytes:
      return "b\"\"";
    case FieldKind::kString:
      return "\"\"";
    case FieldKind::kCollection:
      return "_field(default_factory=list)";
    case FieldKind::kStruct:
    case FieldKind::kVariant:
      return "None";
  }
  return "None";
}

// Emits a typed @dataclass mirror with encode()/decode() convenience methods
// plus a nested Fields id table.
void append_python_dataclass(std::string* out,
                             const CompiledProtocol& protocol,
                             const CompiledMessage& layout,
                             int indent) {
  const std::string class_name = sanitize_type_name(layout.name());
  append_line(out, indent, "@dataclass");
  append_line(out, indent, "class " + class_name + ":");
  bool emitted_member = false;
  for (const CompiledField& field : layout.fields()) {
    if (field.is_reserved) {
      continue;
    }
    emitted_member = true;
    append_line(out,
                indent + 1,
                sanitize_namespace_name(field.name) + ": " + python_field_annotation(protocol, field) + " = " +
                    python_field_default(field));
  }
  if (!emitted_member) {
    append_line(out, indent + 1, "pass");
  }
  append_line(out, indent + 1, "class Fields:");
  if (layout.fields().empty()) {
    append_line(out, indent + 2, "pass");
  }
  for (const CompiledField& field : layout.fields()) {
    append_line(out, indent + 2, sanitize_python_constant_name(field.name) + " = " + std::to_string(field.id));
  }
  append_line(out, indent + 1, "def encode(self) -> bytes:");
  append_line(out, indent + 2, "return CODEC.encode(\"" + escape_python_string(class_name) + "\", self)");
  append_line(out, indent + 1, "@classmethod");
  append_line(out, indent + 1, "def decode(cls, frame: bytes) -> \"" + class_name + "\":");
  append_line(out, indent + 2, "return CODEC.decode_typed(\"" + escape_python_string(class_name) + "\", frame)");
}

}  // namespace

StatusOr<std::string> generate_cpp_bindings_header(const CompiledProtocol& protocol,
                                                   const CppBindingsOptions& options) {
  const std::string protocol_namespace = options.protocol_namespace.empty()
                                             ? sanitize_namespace_name(protocol.name())
                                             : sanitize_namespace_name(options.protocol_namespace);
  const std::string namespace_prefix =
      options.namespace_prefix.empty() ? "upr_generated" : sanitize_namespace_name(options.namespace_prefix);
  std::string header_guard;
  if (options.header_guard.empty()) {
    header_guard =
        sanitize_header_guard(std::string(namespace_prefix) + "_" + std::string(protocol_namespace) + "_bindings");
  } else if (is_valid_header_guard(options.header_guard)) {
    header_guard = options.header_guard;
  } else {
    header_guard = sanitize_header_guard(options.header_guard);
  }

  std::string out;
  append_line(&out, 0, "#ifndef " + header_guard);
  append_line(&out, 0, "#define " + header_guard);
  append_line(&out);
  append_line(&out, 0, "// Generated by Universal Protocol Runtime.");
  append_line(&out, 0, "#include <array>");
  append_line(&out, 0, "#include <cstddef>");
  append_line(&out, 0, "#include <cstdint>");
  append_line(&out, 0, "#include <cstring>");
  append_line(&out, 0, "#include <optional>");
  append_line(&out, 0, "#include <span>");
  append_line(&out, 0, "#include <string_view>");
  append_line(&out, 0, "#include <utility>");
  append_line(&out, 0, "#include <variant>");
  append_line(&out, 0, "#include <vector>");
  append_line(&out);
  append_line(&out, 0, "#include \"universal_protocol_runtime/compiler/compiled_protocol.hpp\"");
  append_line(&out, 0, "#include \"universal_protocol_runtime/decoder/decode_status.hpp\"");
  append_line(&out, 0, "#include \"universal_protocol_runtime/decoder/decoded_message.hpp\"");
  append_line(&out, 0, "#include \"universal_protocol_runtime/decoder/direct_decode_support.hpp\"");
  append_line(&out, 0, "#include \"universal_protocol_runtime/decoder/protocol_decoder.hpp\"");
  append_line(&out, 0, "#include \"universal_protocol_runtime/encoder/direct_encode_support.hpp\"");
  append_line(&out, 0, "#include \"universal_protocol_runtime/encoder/encode_status.hpp\"");
  append_line(&out, 0, "#include \"universal_protocol_runtime/encoder/message_encoder.hpp\"");
  append_line(&out);
  append_line(&out, 0, "namespace " + namespace_prefix + " {");
  append_line(&out, 0, "namespace " + protocol_namespace + " {");
  append_line(&out);
  append_line(
      &out, 0, "inline constexpr std::string_view kProtocolName = \"" + escape_cpp_string(protocol.name()) + "\";");
  append_line(
      &out, 0, "inline constexpr uint64_t kProtocolFingerprint = " + std::to_string(protocol.fingerprint()) + "ULL;");
  append_line(&out);
  append_line(&out, 0, "struct FieldBinding {");
  append_line(&out, 1, "universal_protocol_runtime::FieldId id;");
  append_line(&out, 1, "std::string_view name;");
  append_line(&out, 1, "universal_protocol_runtime::FieldKind kind;");
  append_line(&out, 1, "uint8_t width_bytes;");
  append_line(&out, 1, "std::size_t fixed_size;");
  append_line(&out, 1, "bool dynamic_size;");
  append_line(&out, 1, "universal_protocol_runtime::FieldId size_from_field;");
  append_line(&out, 1, "std::size_t struct_id;");
  append_line(&out, 1, "universal_protocol_runtime::ByteOrder byte_order;");
  append_line(&out, 1, "universal_protocol_runtime::StringEncoding string_encoding;");
  append_line(&out, 1, "bool has_expected_unsigned;");
  append_line(&out, 1, "uint64_t expected_unsigned;");
  append_line(&out, 0, "};");
  append_line(&out);
  append_line(&out, 0, "struct BitFieldBinding {");
  append_line(&out, 1, "universal_protocol_runtime::BitFieldId id;");
  append_line(&out, 1, "std::string_view name;");
  append_line(&out, 1, "universal_protocol_runtime::FieldId container_field_id;");
  append_line(&out, 1, "uint8_t shift_bits;");
  append_line(&out, 1, "uint8_t width_bits;");
  append_line(&out, 1, "uint64_t mask;");
  append_line(&out, 1, "bool is_signed;");
  append_line(&out, 0, "};");
  append_line(&out);
  append_line(&out, 0, "struct ChecksumAnchorBinding {");
  append_line(&out, 1, "universal_protocol_runtime::ChecksumAnchorKind kind;");
  append_line(&out, 1, "universal_protocol_runtime::FieldId field_id;");
  append_line(&out, 0, "};");
  append_line(&out);
  append_line(&out, 0, "struct ChecksumBinding {");
  append_line(&out, 1, "universal_protocol_runtime::FieldId field_id;");
  append_line(&out, 1, "uint8_t result_width_bytes;");
  append_line(&out, 1, "std::string_view algorithm_name;");
  append_line(&out, 1, "ChecksumAnchorBinding from;");
  append_line(&out, 1, "ChecksumAnchorBinding to;");
  append_line(&out, 0, "};");
  append_line(&out);
  append_line(&out, 0, "namespace structs {");
  for (const size_t struct_id : topological_struct_order(protocol)) {
    const CompiledMessage* struct_layout = protocol.struct_by_id(struct_id);
    if (struct_layout == nullptr) {
      continue;
    }
    append_cpp_layout_binding(
        &out, protocol, *struct_layout, false, layout_supports_direct_value_decode(protocol, *struct_layout), 1);
    append_line(&out);
  }
  append_line(&out, 0, "}  // namespace structs");
  append_line(&out);
  append_line(&out, 0, "namespace messages {");
  for (const CompiledMessage& message : protocol.messages()) {
    append_cpp_layout_binding(&out, protocol, message, true, layout_supports_direct_value_decode(protocol, message), 1);
    append_line(&out);
  }
  append_line(&out, 0, "}  // namespace messages");
  append_line(&out);
  for (const CompiledMessage& struct_layout : protocol.structs()) {
    append_line(&out,
                0,
                "using " + sanitize_type_name(struct_layout.name()) +
                    " = structs::" + sanitize_type_name(struct_layout.name()) + ";");
  }
  for (const CompiledMessage& message : protocol.messages()) {
    append_line(
        &out,
        0,
        "using " + sanitize_type_name(message.name()) + " = messages::" + sanitize_type_name(message.name()) + ";");
  }
  append_line(&out);
  append_line(&out, 0, "}  // namespace " + protocol_namespace);
  append_line(&out, 0, "}  // namespace " + namespace_prefix);
  append_line(&out);
  append_line(&out, 0, "#endif  // " + header_guard);
  return out;
}

StatusOr<std::string> generate_python_bindings_module(const CompiledProtocol& protocol,
                                                      const PythonBindingsOptions& options) {
  const std::string module_name = options.module_name.empty() ? sanitize_namespace_name(protocol.name())
                                                              : sanitize_namespace_name(options.module_name);

  std::string out;
  append_line(&out, 0, R"("""Generated by Universal Protocol Runtime for module ')" + module_name + R"('.)");
  append_line(&out);
  append_line(&out, 0, "Encode/decode is provided by the dependency-free 'universal_protocol_runtime'");
  append_line(&out, 0, "package, driven by the schema descriptors below. Do not edit by hand.");
  append_line(&out, 0, R"(""")");
  append_line(&out);
  append_line(&out, 0, "from __future__ import annotations");
  append_line(&out);
  append_line(&out, 0, "from dataclasses import dataclass, field as _field");
  append_line(&out, 0, "from typing import Any, List, Optional");
  append_line(&out);
  append_line(&out, 0, "from universal_protocol_runtime import Codec as _Codec");
  append_line(&out, 0, "from universal_protocol_runtime import metadata as _md");
  append_line(&out);
  append_line(&out, 0, "PROTOCOL_NAME = \"" + escape_python_string(protocol.name()) + "\"");
  append_line(&out, 0, "PROTOCOL_FINGERPRINT = " + std::to_string(protocol.fingerprint()));
  append_line(&out);

  append_line(&out, 0, "PROTOCOL = _md.Protocol(");
  append_line(&out, 1, "name=\"" + escape_python_string(protocol.name()) + "\",");
  append_line(&out, 1, "fingerprint=" + std::to_string(protocol.fingerprint()) + ",");
  append_line(&out, 1, "structs=(");
  for (const CompiledMessage& struct_layout : protocol.structs()) {
    append_python_layout_literal(&out, struct_layout, false, 2);
  }
  append_line(&out, 1, "),");
  append_line(&out, 1, "messages=(");
  for (const CompiledMessage& message : protocol.messages()) {
    append_python_layout_literal(&out, message, true, 2);
  }
  append_line(&out, 1, "),");
  append_line(&out, 0, ")");
  append_line(&out);
  append_line(&out, 0, "CODEC = _Codec(PROTOCOL)");
  append_line(&out);

  for (const CompiledMessage& struct_layout : protocol.structs()) {
    append_python_dataclass(&out, protocol, struct_layout, 0);
    append_line(&out);
  }
  for (const CompiledMessage& message : protocol.messages()) {
    append_python_dataclass(&out, protocol, message, 0);
    append_line(&out);
  }

  for (const CompiledMessage& struct_layout : protocol.structs()) {
    append_line(&out,
                0,
                "CODEC.register_dataclass(\"" + escape_python_string(sanitize_type_name(struct_layout.name())) +
                    "\", " + sanitize_type_name(struct_layout.name()) + ")");
  }
  for (const CompiledMessage& message : protocol.messages()) {
    append_line(&out,
                0,
                "CODEC.register_dataclass(\"" + escape_python_string(sanitize_type_name(message.name())) + "\", " +
                    sanitize_type_name(message.name()) + ")");
  }
  append_line(&out);

  append_line(&out, 0, "STRUCTS = (");
  for (const CompiledMessage& struct_layout : protocol.structs()) {
    append_line(&out, 1, sanitize_type_name(struct_layout.name()) + ",");
  }
  append_line(&out, 0, ")");
  append_line(&out, 0, "MESSAGES = (");
  for (const CompiledMessage& message : protocol.messages()) {
    append_line(&out, 1, sanitize_type_name(message.name()) + ",");
  }
  append_line(&out, 0, ")");

  return out;
}

namespace {

std::string typescript_bool(bool value) { return value ? "true" : "false"; }

std::string typescript_bytes_literal(std::span<const std::byte> bytes) {
  std::ostringstream stream;
  stream << "new Uint8Array([";
  for (size_t index = 0; index < bytes.size(); ++index) {
    if (index != 0) {
      stream << ", ";
    }
    stream << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(std::to_integer<unsigned char>(bytes[index])) << std::nouppercase << std::dec;
  }
  stream << "])";
  return stream.str();
}

std::string typescript_variant_cases_literal(const CompiledField& field) {
  std::string out = "[";
  bool first = true;
  for (const CompiledVariantCase& variant_case : field.variant_cases) {
    if (!first) {
      out += ", ";
    }
    first = false;
    out += "{ tagValue: " + std::to_string(variant_case.tag_value) +
           ", structId: " + std::to_string(variant_case.struct_id) + " }";
  }
  out += "]";
  return out;
}

// Emits a metadata Field object literal carrying every wire-relevant property.
std::string typescript_field_literal(const CompiledField& field) {
  std::ostringstream stream;
  stream << "{ id: " << field.id << ", name: \"" << escape_cpp_string(sanitize_namespace_name(field.name))
         << "\", kind: \"" << to_string(field.kind) << "\", widthBytes: " << static_cast<unsigned>(field.width_bytes)
         << ", byteOrder: \"" << to_string(field.byte_order) << "\", stringEncoding: \""
         << to_string(field.string_encoding) << "\", fixedSize: " << field.fixed_size
         << ", dynamicSize: " << typescript_bool(field.dynamic_size) << ", sizeFromField: " << field.size_from_field
         << ", structId: " << field.struct_id << ", alignment: " << field.alignment
         << ", isReserved: " << typescript_bool(field.is_reserved)
         << ", reservedFillByte: " << static_cast<unsigned>(field.reserved_fill_byte)
         << ", fixedCount: " << field.fixed_count << ", dynamicCount: " << typescript_bool(field.dynamic_count)
         << ", countFromField: " << field.count_from_field << ", hasCondition: " << typescript_bool(field.has_condition)
         << ", conditionField: " << field.condition_field << ", conditionEquals: " << field.condition_equals
         << ", hasPresence: " << typescript_bool(field.has_presence) << ", presenceField: " << field.presence_field
         << ", presenceBit: " << static_cast<unsigned>(field.presence_bit) << ", tagFromField: " << field.tag_from_field
         << ", variantCases: " << typescript_variant_cases_literal(field)
         << ", hasExpectedUnsigned: " << typescript_bool(field.has_expected_unsigned)
         << ", expectedUnsigned: " << field.expected_unsigned << " }";
  return stream.str();
}

std::string typescript_checksum_literal(const CompiledChecksum& checksum) {
  std::ostringstream stream;
  stream << "{ fieldId: " << checksum.field_id
         << ", resultWidthBytes: " << static_cast<unsigned>(checksum.result_width_bytes) << ", algorithmName: \""
         << escape_cpp_string(checksum.algorithm_name) << "\", fromAnchor: { kind: \""
         << checksum_anchor_kind_name(checksum.from.kind) << "\", fieldId: " << checksum.from.field_id
         << " }, toAnchor: { kind: \"" << checksum_anchor_kind_name(checksum.to.kind)
         << "\", fieldId: " << checksum.to.field_id << " } }";
  return stream.str();
}

void append_typescript_layout_literal(std::string* out, const CompiledMessage& layout, bool is_message, int indent) {
  append_line(out, indent, "{");
  append_line(out, indent + 1, "name: \"" + escape_cpp_string(sanitize_type_name(layout.name())) + "\",");
  append_line(out, indent + 1, "isMessage: " + typescript_bool(is_message) + ",");
  append_line(out, indent + 1, "minimumSize: " + std::to_string(layout.minimum_size()) + ",");
  append_line(
      out, indent + 1, "allowTrailingBytes: " + typescript_bool(is_message && layout.allow_trailing_bytes()) + ",");
  append_line(out,
              indent + 1,
              "dispatchPrefix: " +
                  typescript_bytes_literal(is_message ? layout.dispatch_prefix() : std::span<const std::byte>{}) + ",");
  append_line(out, indent + 1, "fields: [");
  for (const CompiledField& field : layout.fields()) {
    append_line(out, indent + 2, typescript_field_literal(field) + ",");
  }
  append_line(out, indent + 1, "],");
  append_line(out, indent + 1, "checksums: [");
  for (const CompiledChecksum& checksum : layout.checksums()) {
    append_line(out, indent + 2, typescript_checksum_literal(checksum) + ",");
  }
  append_line(out, indent + 1, "],");
  append_line(out, indent, "},");
}

std::string typescript_field_annotation(const CompiledProtocol& protocol, const CompiledField& field) {
  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kSigned:
    case FieldKind::kEnum:
      return field.width_bytes > 6 ? "number | bigint" : "number";
    case FieldKind::kFloat32:
    case FieldKind::kFloat64:
      return "number";
    case FieldKind::kBytes:
      return "Uint8Array";
    case FieldKind::kString:
      return "string";
    case FieldKind::kStruct:
      return struct_type_name(protocol, field.struct_id);
    case FieldKind::kCollection:
      return struct_type_name(protocol, field.struct_id) + "[]";
    case FieldKind::kVariant:
      if (field.variant_cases.empty()) {
        return "Record<string, unknown>";
      }
      {
        std::string type;
        for (const CompiledVariantCase& variant_case : field.variant_cases) {
          if (!type.empty()) {
            type += " | ";
          }
          type += struct_type_name(protocol, variant_case.struct_id);
        }
        return type;
      }
  }
  return "unknown";
}

// Emits a typed interface mirror plus a same-named const carrying the field-id
// table and encode/decode convenience methods (declaration merging).
void append_typescript_interface(std::string* out,
                                 const CompiledProtocol& protocol,
                                 const CompiledMessage& layout,
                                 int indent) {
  const std::string class_name = sanitize_type_name(layout.name());
  append_line(out, indent, "export interface " + class_name + " {");
  for (const CompiledField& field : layout.fields()) {
    if (field.is_reserved) {
      continue;
    }
    append_line(out,
                indent + 1,
                sanitize_namespace_name(field.name) + "?: " + typescript_field_annotation(protocol, field) + ";");
  }
  append_line(out, indent, "}");
  append_line(out);
  append_line(out, indent, "export const " + class_name + " = {");
  append_line(out, indent + 1, "fields: {");
  for (const CompiledField& field : layout.fields()) {
    append_line(out, indent + 2, sanitize_namespace_name(field.name) + ": " + std::to_string(field.id) + ",");
  }
  append_line(out, indent + 1, "} as const,");
  append_line(out,
              indent + 1,
              "encode(value: " + class_name + "): Uint8Array { return CODEC.encode(\"" + escape_cpp_string(class_name) +
                  "\", value); },");
  append_line(out,
              indent + 1,
              "decode(frame: Uint8Array): " + class_name + " { return CODEC.decode(\"" + escape_cpp_string(class_name) +
                  "\", frame) as unknown as " + class_name + "; },");
  append_line(out,
              indent + 1,
              "decodeSequence(frame: Uint8Array): " + class_name + "[] { return CODEC.decodeSequence(\"" +
                  escape_cpp_string(class_name) + "\", frame) as unknown as " + class_name + "[]; },");
  append_line(out, indent, "};");
}

}  // namespace

StatusOr<std::string> generate_typescript_bindings_module(const CompiledProtocol& protocol,
                                                          const TypescriptBindingsOptions& options) {
  const std::string runtime_import =
      options.runtime_import.empty() ? "universal-protocol-runtime" : options.runtime_import;

  std::string out;
  append_line(&out, 0, "// Generated by Universal Protocol Runtime. Do not edit by hand.");
  append_line(&out, 0, "//");
  append_line(&out, 0, "// Encode/decode is provided by the dependency-free runtime package, driven by");
  append_line(&out, 0, "// the schema descriptors below. Frames are byte-compatible with the C++ and");
  append_line(&out, 0, "// Python runtimes.");
  append_line(&out);
  append_line(&out, 0, "import { Codec } from \"" + escape_cpp_string(runtime_import) + "\";");
  append_line(&out, 0, "import type { Protocol } from \"" + escape_cpp_string(runtime_import) + "\";");
  append_line(&out);
  append_line(&out, 0, "export const PROTOCOL_NAME = \"" + escape_cpp_string(protocol.name()) + "\";");
  append_line(&out, 0, "export const PROTOCOL_FINGERPRINT = \"" + std::to_string(protocol.fingerprint()) + "\";");
  append_line(&out);

  append_line(&out, 0, "export const PROTOCOL: Protocol = {");
  append_line(&out, 1, "name: \"" + escape_cpp_string(protocol.name()) + "\",");
  append_line(&out, 1, "fingerprint: \"" + std::to_string(protocol.fingerprint()) + "\",");
  append_line(&out, 1, "structs: [");
  for (const CompiledMessage& struct_layout : protocol.structs()) {
    append_typescript_layout_literal(&out, struct_layout, false, 2);
  }
  append_line(&out, 1, "],");
  append_line(&out, 1, "messages: [");
  for (const CompiledMessage& message : protocol.messages()) {
    append_typescript_layout_literal(&out, message, true, 2);
  }
  append_line(&out, 1, "],");
  append_line(&out, 0, "};");
  append_line(&out);
  append_line(&out, 0, "export const CODEC = new Codec(PROTOCOL);");
  append_line(&out);

  for (const CompiledMessage& struct_layout : protocol.structs()) {
    append_typescript_interface(&out, protocol, struct_layout, 0);
    append_line(&out);
  }
  for (const CompiledMessage& message : protocol.messages()) {
    append_typescript_interface(&out, protocol, message, 0);
    append_line(&out);
  }

  return out;
}

}  // namespace universal_protocol_runtime
