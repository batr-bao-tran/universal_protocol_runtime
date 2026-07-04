#include "universal_protocol_runtime/codegen/bindings_generator.hpp"

#include <cctype>
#include <cstddef>
#include <iomanip>
#include <span>
#include <sstream>
#include <string_view>
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

std::string sanitize_namespace_name(std::string_view value) {
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

void append_cpp_layout_binding(std::string* out, const CompiledMessage& layout, bool is_message, int indent) {
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
  append_line(out, indent, "};");
}

std::string python_bool(bool value) { return value ? "True" : "False"; }

std::string python_field_binding_literal(const CompiledField& field) {
  std::ostringstream stream;
  stream << "FieldBinding("
         << "id=" << field.id << ", "
         << "name=\"" << escape_python_string(field.name) << "\", "
         << "kind=\"" << to_string(field.kind) << "\", "
         << "width_bytes=" << static_cast<unsigned>(field.width_bytes) << ", "
         << "fixed_size=" << field.fixed_size << ", "
         << "dynamic_size=" << python_bool(field.dynamic_size) << ", "
         << "size_from_field=" << field.size_from_field << ", "
         << "struct_id=" << field.struct_id << ", "
         << "byte_order=\"" << to_string(field.byte_order) << "\", "
         << "string_encoding=\"" << to_string(field.string_encoding) << "\", "
         << "has_expected_unsigned=" << python_bool(field.has_expected_unsigned) << ", "
         << "expected_unsigned=" << field.expected_unsigned << ")";
  return stream.str();
}

std::string python_bit_field_binding_literal(const CompiledBitField& bit_field) {
  std::ostringstream stream;
  stream << "BitFieldBinding("
         << "id=" << bit_field.id << ", "
         << "name=\"" << escape_python_string(bit_field.name) << "\", "
         << "container_field_id=" << bit_field.container_field_id << ", "
         << "shift_bits=" << static_cast<unsigned>(bit_field.shift_bits) << ", "
         << "width_bits=" << static_cast<unsigned>(bit_field.width_bits) << ", "
         << "mask=" << bit_field.mask << ", "
         << "is_signed=" << python_bool(bit_field.is_signed) << ")";
  return stream.str();
}

std::string python_checksum_binding_literal(const CompiledChecksum& checksum) {
  std::ostringstream stream;
  stream << "ChecksumBinding("
         << "field_id=" << checksum.field_id << ", "
         << "result_width_bytes=" << static_cast<unsigned>(checksum.result_width_bytes) << ", "
         << "algorithm_name=\"" << escape_python_string(checksum.algorithm_name) << "\", "
         << "from_anchor=ChecksumAnchorBinding(kind=\"" << checksum_anchor_kind_name(checksum.from.kind)
         << "\", field_id=" << checksum.from.field_id << "), "
         << "to_anchor=ChecksumAnchorBinding(kind=\"" << checksum_anchor_kind_name(checksum.to.kind)
         << "\", field_id=" << checksum.to.field_id << "))";
  return stream.str();
}

void append_python_id_class(std::string* out,
                            std::string_view class_name,
                            const auto& items,
                            auto name_accessor,
                            auto id_accessor,
                            int indent) {
  append_line(out, indent, std::string("class ") + std::string(class_name) + ":");
  if (items.empty()) {
    append_line(out, indent + 1, "pass");
    return;
  }
  for (const auto& item : items) {
    append_line(out,
                indent + 1,
                sanitize_python_constant_name(name_accessor(item)) + " = " + std::to_string(id_accessor(item)));
  }
}

void append_python_binding_tuple(
    std::string* out, std::string_view declaration, const auto& items, auto literal_fn, int indent) {
  append_line(out, indent, std::string(declaration) + " = (");
  for (const auto& item : items) {
    append_line(out, indent + 1, literal_fn(item) + ",");
  }
  append_line(out, indent, ")");
}

void append_python_layout_binding(std::string* out, const CompiledMessage& layout, bool is_message, int indent) {
  append_line(out, indent, "class " + sanitize_type_name(layout.name()) + ":");
  append_line(out, indent + 1, "KIND = \"" + std::string(is_message ? "message" : "struct") + "\"");
  append_line(out, indent + 1, "NAME = \"" + escape_python_string(layout.name()) + "\"");
  append_line(out, indent + 1, "MINIMUM_SIZE = " + std::to_string(layout.minimum_size()));
  append_line(out, indent + 1, "ALLOW_TRAILING_BYTES = " + python_bool(is_message && layout.allow_trailing_bytes()));
  append_line(out,
              indent + 1,
              "DISPATCH_PREFIX = " +
                  python_bytes_literal(is_message ? layout.dispatch_prefix() : std::span<const std::byte>{}));
  append_python_id_class(
      out,
      "Fields",
      layout.fields(),
      [](const CompiledField& field) -> std::string_view { return field.name; },
      [](const CompiledField& field) -> FieldId { return field.id; },
      indent + 1);
  append_python_id_class(
      out,
      "BitFields",
      layout.bit_fields(),
      [](const CompiledBitField& bit_field) -> std::string_view { return bit_field.name; },
      [](const CompiledBitField& bit_field) -> BitFieldId { return bit_field.id; },
      indent + 1);
  append_python_binding_tuple(out, "FIELDS", layout.fields(), python_field_binding_literal, indent + 1);
  append_python_binding_tuple(out, "BIT_FIELDS", layout.bit_fields(), python_bit_field_binding_literal, indent + 1);
  append_python_binding_tuple(out, "CHECKSUMS", layout.checksums(), python_checksum_binding_literal, indent + 1);
}

}  // namespace

StatusOr<std::string> generate_cpp_bindings_header(const CompiledProtocol& protocol,
                                                   const CppBindingsOptions& options) {
  const std::string protocol_namespace = options.protocol_namespace.empty()
                                             ? sanitize_namespace_name(protocol.name())
                                             : sanitize_namespace_name(options.protocol_namespace);
  const std::string namespace_prefix =
      options.namespace_prefix.empty() ? "upr_generated" : sanitize_namespace_name(options.namespace_prefix);
  const std::string header_guard =
      options.header_guard.empty()
          ? sanitize_header_guard(std::string(namespace_prefix) + "_" + std::string(protocol_namespace) + "_bindings")
          : sanitize_header_guard(options.header_guard);

  std::string out;
  append_line(&out, 0, "#ifndef " + header_guard);
  append_line(&out, 0, "#define " + header_guard);
  append_line(&out);
  append_line(&out, 0, "// Generated by Universal Protocol Runtime.");
  append_line(&out, 0, "#include <array>");
  append_line(&out, 0, "#include <cstddef>");
  append_line(&out, 0, "#include <cstdint>");
  append_line(&out, 0, "#include <string_view>");
  append_line(&out);
  append_line(&out, 0, "#include \"universal_protocol_runtime/compiler/compiled_protocol.hpp\"");
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
  for (const CompiledMessage& struct_layout : protocol.structs()) {
    append_cpp_layout_binding(&out, struct_layout, false, 1);
    append_line(&out);
  }
  append_line(&out, 0, "}  // namespace structs");
  append_line(&out);
  append_line(&out, 0, "namespace messages {");
  for (const CompiledMessage& message : protocol.messages()) {
    append_cpp_layout_binding(&out, message, true, 1);
    append_line(&out);
  }
  append_line(&out, 0, "}  // namespace messages");
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
  append_line(&out, 0, R"("""Generated by Universal Protocol Runtime for module ')" + module_name + R"('.""")");
  append_line(&out);
  append_line(&out, 0, "from dataclasses import dataclass");
  append_line(&out);
  append_line(&out, 0, "PROTOCOL_NAME = \"" + escape_python_string(protocol.name()) + "\"");
  append_line(&out, 0, "PROTOCOL_FINGERPRINT = " + std::to_string(protocol.fingerprint()));
  append_line(&out);
  append_line(&out, 0, "@dataclass(frozen=True)");
  append_line(&out, 0, "class FieldBinding:");
  append_line(&out, 1, "id: int");
  append_line(&out, 1, "name: str");
  append_line(&out, 1, "kind: str");
  append_line(&out, 1, "width_bytes: int");
  append_line(&out, 1, "fixed_size: int");
  append_line(&out, 1, "dynamic_size: bool");
  append_line(&out, 1, "size_from_field: int");
  append_line(&out, 1, "struct_id: int");
  append_line(&out, 1, "byte_order: str");
  append_line(&out, 1, "string_encoding: str");
  append_line(&out, 1, "has_expected_unsigned: bool");
  append_line(&out, 1, "expected_unsigned: int");
  append_line(&out);
  append_line(&out, 0, "@dataclass(frozen=True)");
  append_line(&out, 0, "class BitFieldBinding:");
  append_line(&out, 1, "id: int");
  append_line(&out, 1, "name: str");
  append_line(&out, 1, "container_field_id: int");
  append_line(&out, 1, "shift_bits: int");
  append_line(&out, 1, "width_bits: int");
  append_line(&out, 1, "mask: int");
  append_line(&out, 1, "is_signed: bool");
  append_line(&out);
  append_line(&out, 0, "@dataclass(frozen=True)");
  append_line(&out, 0, "class ChecksumAnchorBinding:");
  append_line(&out, 1, "kind: str");
  append_line(&out, 1, "field_id: int");
  append_line(&out);
  append_line(&out, 0, "@dataclass(frozen=True)");
  append_line(&out, 0, "class ChecksumBinding:");
  append_line(&out, 1, "field_id: int");
  append_line(&out, 1, "result_width_bytes: int");
  append_line(&out, 1, "algorithm_name: str");
  append_line(&out, 1, "from_anchor: ChecksumAnchorBinding");
  append_line(&out, 1, "to_anchor: ChecksumAnchorBinding");
  append_line(&out);

  for (const CompiledMessage& struct_layout : protocol.structs()) {
    append_python_layout_binding(&out, struct_layout, false, 0);
    append_line(&out);
  }
  for (const CompiledMessage& message : protocol.messages()) {
    append_python_layout_binding(&out, message, true, 0);
    append_line(&out);
  }

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

}  // namespace universal_protocol_runtime
