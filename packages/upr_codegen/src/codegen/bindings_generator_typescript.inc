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
