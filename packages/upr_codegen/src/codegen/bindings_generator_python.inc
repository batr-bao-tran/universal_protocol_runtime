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

std::string python_native_module_name(const CompiledProtocol& protocol, const PythonBindingsOptions& options) {
  if (!options.native_module_name.empty()) {
    return sanitize_namespace_name(options.native_module_name);
  }
  const std::string module_name = options.module_name.empty() ? sanitize_namespace_name(protocol.name())
                                                              : sanitize_namespace_name(options.module_name);
  return "_" + module_name + "_native";
}

std::string python_native_header_include(const CompiledProtocol& protocol, const PythonBindingsOptions& options) {
  if (!options.native_header_include.empty()) {
    return options.native_header_include;
  }
  const std::string module_name = options.module_name.empty() ? sanitize_namespace_name(protocol.name())
                                                              : sanitize_namespace_name(options.module_name);
  return module_name + "_native.hpp";
}

bool native_layout_is_message(const CompiledProtocol& protocol, const CompiledMessage& layout) {
  return std::any_of(protocol.messages().begin(), protocol.messages().end(), [&](const CompiledMessage& message) {
    return message.name() == layout.name();
  });
}

std::string native_layout_namespace(const CompiledProtocol& protocol, const CompiledMessage& layout) {
  return std::string(native_layout_is_message(protocol, layout) ? "gen::messages::" : "gen::structs::") +
         sanitize_type_name(layout.name());
}

std::string native_value_type(const CompiledProtocol& protocol, const CompiledMessage& layout) {
  return native_layout_namespace(protocol, layout) + "::Value";
}

std::string native_from_py_name(const CompiledMessage& layout) {
  return "value_from_py_" + sanitize_namespace_name(layout.name());
}

std::string native_to_py_name(const CompiledMessage& layout) { return "value_to_py_" + sanitize_namespace_name(layout.name()); }

Status validate_python_native_protocol(const CompiledProtocol& protocol) {
  for (const CompiledMessage& layout : protocol.structs()) {
    for (const CompiledField& field : layout.fields()) {
      if ((field.kind == FieldKind::kStruct || field.kind == FieldKind::kCollection) &&
          protocol.struct_by_id(field.struct_id) == nullptr) {
        return schema_error("python native generation references an unknown struct");
      }
      if (field.kind == FieldKind::kVariant) {
        for (const CompiledVariantCase& variant_case : field.variant_cases) {
          if (protocol.struct_by_id(variant_case.struct_id) == nullptr) {
            return schema_error("python native generation references an unknown variant struct");
          }
        }
      }
    }
  }
  for (const CompiledMessage& layout : protocol.messages()) {
    for (const CompiledField& field : layout.fields()) {
      if ((field.kind == FieldKind::kStruct || field.kind == FieldKind::kCollection) &&
          protocol.struct_by_id(field.struct_id) == nullptr) {
        return schema_error("python native generation references an unknown struct");
      }
      if (field.kind == FieldKind::kVariant) {
        for (const CompiledVariantCase& variant_case : field.variant_cases) {
          if (protocol.struct_by_id(variant_case.struct_id) == nullptr) {
            return schema_error("python native generation references an unknown variant struct");
          }
        }
      }
    }
  }
  return Status::ok_status();
}

const CompiledMessage* native_struct_layout(const CompiledProtocol& protocol, std::size_t struct_id) {
  return protocol.struct_by_id(struct_id);
}

void append_python_native_scalar_from_py(std::string* out, const CompiledProtocol& protocol, const CompiledField& field) {
  const std::string name = sanitize_namespace_name(field.name);
  append_line(out, 2, "if (!" + name + "_object.is_none()) {");
  append_line(out,
              3,
              "value." + name + " = " + name + "_object.cast<" + cpp_value_member_type(protocol, field, true) + ">();");
  append_line(out, 2, "}");
}

void append_python_native_bytes_from_py(std::string* out, const CompiledField& field) {
  const std::string name = sanitize_namespace_name(field.name);
  append_line(out, 2, "if (!" + name + "_object.is_none()) {");
  append_line(out, 3, "storage.emplace_back(py::bytes(" + name + "_object));");
  append_line(out, 3, "const std::string& " + name + "_storage = storage.back();");
  append_line(out,
              3,
              "value." + name + " = universal_protocol_runtime::ByteSpan(reinterpret_cast<const std::byte*>(" + name +
                  "_storage.data()), " + name + "_storage.size());");
  append_line(out, 2, "}");
}

void append_python_native_string_from_py(std::string* out, const CompiledField& field) {
  const std::string name = sanitize_namespace_name(field.name);
  append_line(out, 2, "if (!" + name + "_object.is_none()) {");
  append_line(out, 3, "storage.emplace_back(py::str(" + name + "_object));");
  append_line(out, 3, "const std::string& " + name + "_storage = storage.back();");
  append_line(out, 3, "value." + name + " = std::string_view(" + name + "_storage.data(), " + name + "_storage.size());");
  append_line(out, 2, "}");
}

void append_python_native_struct_from_py(std::string* out, const CompiledProtocol& protocol, const CompiledField& field) {
  const std::string name = sanitize_namespace_name(field.name);
  const CompiledMessage* nested = native_struct_layout(protocol, field.struct_id);
  if (nested == nullptr) {
    return;
  }
  append_line(out, 2, "if (!" + name + "_object.is_none()) {");
  append_line(out, 3, "value." + name + " = " + native_from_py_name(*nested) + "(" + name + "_object, storage);");
  append_line(out, 2, "}");
}

void append_python_native_collection_from_py(std::string* out,
                                             const CompiledProtocol& protocol,
                                             const CompiledField& field) {
  const std::string name = sanitize_namespace_name(field.name);
  const CompiledMessage* nested = native_struct_layout(protocol, field.struct_id);
  if (nested == nullptr) {
    return;
  }
  append_line(out, 2, "if (!" + name + "_object.is_none()) {");
  append_line(out, 3, "for (const py::handle item : " + name + "_object) {");
  append_line(out, 4, "value." + name + ".push_back(" + native_from_py_name(*nested) + "(py::reinterpret_borrow<py::object>(item), storage));");
  append_line(out, 3, "}");
  append_line(out, 2, "}");
}

void append_python_native_variant_from_py(std::string* out, const CompiledProtocol& protocol, const CompiledMessage& layout,
                                          const CompiledField& field) {
  const std::string name = sanitize_namespace_name(field.name);
  const std::string tag_field = sanitize_namespace_name(layout.fields()[field.tag_from_field].name);
  append_line(out, 2, "if (!" + name + "_object.is_none()) {");
  append_line(out, 3, "switch (static_cast<uint64_t>(value." + tag_field + ")) {");
  std::size_t variant_index = 1;
  for (const CompiledVariantCase& variant_case : field.variant_cases) {
    const CompiledMessage* nested = native_struct_layout(protocol, variant_case.struct_id);
    if (nested == nullptr) {
      ++variant_index;
      continue;
    }
    append_line(out, 4, "case " + std::to_string(variant_case.tag_value) + "ULL:");
    append_line_cat(out,
                    5,
                    "value.",
                    name,
                    ".emplace<",
                    std::to_string(variant_index),
                    ">(",
                    native_from_py_name(*nested),
                    "(",
                    name,
                    "_object, storage));");
    append_line(out, 5, "break;");
    ++variant_index;
  }
  append_line(out, 4, "default:");
  append_line(out, 5, "break;");
  append_line(out, 3, "}");
  append_line(out, 2, "}");
}

void append_python_native_from_py_function(std::string* out, const CompiledProtocol& protocol, const CompiledMessage& layout) {
  append_line(out,
              0,
              native_value_type(protocol, layout) + " " + native_from_py_name(layout) +
                  "(py::object source, std::deque<std::string>& storage) {");
  append_line(out, 1, "(void)storage;");
  append_line(out, 1, native_value_type(protocol, layout) + " value{};");
  for (const CompiledField& field : layout.fields()) {
    if (field.is_reserved) {
      continue;
    }
    const std::string name = sanitize_namespace_name(field.name);
    append_line(out, 1, "py::object " + name + "_object = get_field(source, \"" + escape_cpp_string(name) + "\");");
    switch (field.kind) {
      case FieldKind::kUnsigned:
      case FieldKind::kSigned:
      case FieldKind::kEnum:
      case FieldKind::kFloat32:
      case FieldKind::kFloat64:
        append_python_native_scalar_from_py(out, protocol, field);
        break;
      case FieldKind::kBytes:
        append_python_native_bytes_from_py(out, field);
        break;
      case FieldKind::kString:
        append_python_native_string_from_py(out, field);
        break;
      case FieldKind::kStruct:
        append_python_native_struct_from_py(out, protocol, field);
        break;
      case FieldKind::kCollection:
        append_python_native_collection_from_py(out, protocol, field);
        break;
      case FieldKind::kVariant:
        append_python_native_variant_from_py(out, protocol, layout, field);
        break;
    }
  }
  for (const CompiledField& field : layout.fields()) {
    if (field.is_reserved || !field.dynamic_size) {
      continue;
    }
    const std::string field_name = sanitize_namespace_name(field.name);
    const std::string size_name = sanitize_namespace_name(layout.fields()[field.size_from_field].name);
    append_line_cat(out,
                    1,
                    "value.",
                    size_name,
                    " = static_cast<decltype(value.",
                    size_name,
                    ")>(value.",
                    field_name,
                    ".size());");
  }
  for (const CompiledField& field : layout.fields()) {
    if (field.is_reserved || !field.dynamic_count) {
      continue;
    }
    const std::string field_name = sanitize_namespace_name(field.name);
    const std::string count_name = sanitize_namespace_name(layout.fields()[field.count_from_field].name);
    append_line_cat(out,
                    1,
                    "value.",
                    count_name,
                    " = static_cast<decltype(value.",
                    count_name,
                    ")>(value.",
                    field_name,
                    ".size());");
  }
  append_line(out, 1, "return value;");
  append_line(out, 0, "}");
  append_line(out);
}

void append_python_native_to_py_assignment(std::string* out,
                                           const CompiledProtocol& protocol,
                                           const CompiledField& field,
                                           int indent) {
  const std::string name = sanitize_namespace_name(field.name);
  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kSigned:
    case FieldKind::kEnum:
    case FieldKind::kFloat32:
    case FieldKind::kFloat64:
      append_line(out, indent, "result[\"" + escape_cpp_string(name) + "\"] = py::cast(value." + name + ");");
      break;
    case FieldKind::kBytes:
      append_line(out,
                  indent,
                  "result[\"" + escape_cpp_string(name) + "\"] = py::bytes(reinterpret_cast<const char*>(value." + name +
                      ".data()), value." + name + ".size());");
      break;
    case FieldKind::kString:
      append_line(out,
                  indent,
                  "result[\"" + escape_cpp_string(name) + "\"] = py::str(std::string(value." + name + ".data(), value." +
                      name + ".size()));");
      break;
    case FieldKind::kStruct: {
      const CompiledMessage* nested = native_struct_layout(protocol, field.struct_id);
      if (nested == nullptr) {
        break;
      }
      append_line(out,
                  indent,
                  "result[\"" + escape_cpp_string(name) + "\"] = " + native_to_py_name(*nested) + "(value." + name +
                      ");");
      break;
    }
    case FieldKind::kCollection: {
      const CompiledMessage* nested = native_struct_layout(protocol, field.struct_id);
      if (nested == nullptr) {
        break;
      }
      append_line(out, indent, "py::list " + name + "_items;");
      append_line(out, indent, "for (const auto& item : value." + name + ") {");
      append_line(out, indent + 1, name + "_items.append(" + native_to_py_name(*nested) + "(item));");
      append_line(out, indent, "}");
      append_line(out, indent, "result[\"" + escape_cpp_string(name) + "\"] = std::move(" + name + "_items);");
      break;
    }
    case FieldKind::kVariant: {
      const std::string tag_field = sanitize_namespace_name(field.name);
      (void)tag_field;
      append_line(out, indent, "switch (value." + name + ".index()) {");
      append_line(out, indent + 1, "case 0:");
      append_line(out, indent + 2, "break;");
      std::size_t variant_index = 1;
      for (const CompiledVariantCase& variant_case : field.variant_cases) {
        const CompiledMessage* nested = native_struct_layout(protocol, variant_case.struct_id);
        if (nested == nullptr) {
          ++variant_index;
          continue;
        }
        append_line(out, indent + 1, "case " + std::to_string(variant_index) + ":");
        append_line(out,
                    indent + 2,
                    "result[\"" + escape_cpp_string(name) + "\"] = " + native_to_py_name(*nested) + "(std::get<" +
                        std::to_string(variant_index) + ">(value." + name + "));");
        append_line(out, indent + 2, "break;");
        ++variant_index;
      }
      append_line(out, indent + 1, "default:");
      append_line(out, indent + 2, "break;");
      append_line(out, indent, "}");
      break;
    }
  }
}

void append_python_native_to_py_function(std::string* out, const CompiledProtocol& protocol, const CompiledMessage& layout) {
  append_line(out,
              0,
              "py::dict " + native_to_py_name(layout) + "(const " + native_value_type(protocol, layout) + "& value) {");
  append_line(out, 1, "py::dict result;");
  for (const CompiledField& field : layout.fields()) {
    if (field.is_reserved) {
      continue;
    }
    const std::string present = cpp_presence_condition_expression(layout, field, "value.");
    if (present != "true") {
      append_line(out, 1, "if (" + present + ") {");
      append_python_native_to_py_assignment(out, protocol, field, 2);
      append_line(out, 1, "}");
    } else {
      append_python_native_to_py_assignment(out, protocol, field, 1);
    }
  }
  append_line(out, 1, "return result;");
  append_line(out, 0, "}");
  append_line(out);
}

void append_python_native_dispatch_branch(std::string* out,
                                          const CompiledProtocol& protocol,
                                          const CompiledMessage& layout,
                                          std::string_view operation,
                                          int indent) {
  const std::string class_name = sanitize_type_name(layout.name());
  const std::string ns = native_layout_namespace(protocol, layout);
  append_line(out, indent, "if (name == \"" + escape_cpp_string(class_name) + "\") {");
  if (operation == "encode") {
    append_line(out, indent + 1, "std::deque<std::string> storage;");
    append_line(out, indent + 1, "return encode_layout<" + ns + ">(" + native_from_py_name(layout) + "(values, storage));");
  } else if (operation == "decode") {
    append_line(out,
                indent + 1,
                "return decode_layout<" + ns + ", " + native_value_type(protocol, layout) + ">(frame, " +
                    native_to_py_name(layout) + ");");
  } else {
    append_line(out,
                indent + 1,
                "return decode_sequence_layout<" + ns + ", " + native_value_type(protocol, layout) + ">(frame, " +
                    native_to_py_name(layout) + ");");
  }
  append_line(out, indent, "}");
}
