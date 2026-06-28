
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
