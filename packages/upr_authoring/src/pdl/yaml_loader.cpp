#include "universal_protocol_runtime/pdl/yaml_loader.hpp"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace universal_protocol_runtime {
namespace {

constexpr uint8_t kFloat32WidthBytes = sizeof(uint32_t);
constexpr uint8_t kFloat64WidthBytes = sizeof(uint64_t);
constexpr std::string_view kTypeBytes = "bytes";
constexpr std::string_view kTypeString = "string";
constexpr std::string_view kTypeEnum = "enum";
constexpr std::string_view kTypeUnsignedPrefix = "uint";
constexpr std::string_view kTypeSignedPrefix = "int";
constexpr std::string_view kTypeFloat32Prefix = "float32";
constexpr std::string_view kTypeFloat64Prefix = "float64";
constexpr std::string_view kBigEndianSuffix = "_be";
constexpr std::string_view kEncodingAscii = "ascii";
constexpr std::string_view kEncodingUtf8 = "utf8";
constexpr std::string_view kEncodingUtf8Dashed = "utf-8";
constexpr std::string_view kEndianLittle = "little";
constexpr std::string_view kEndianLittleShort = "le";
constexpr std::string_view kEndianLittleLong = "little_endian";
constexpr std::string_view kEndianBig = "big";
constexpr std::string_view kEndianBigShort = "be";
constexpr std::string_view kEndianBigLong = "big_endian";
constexpr uint32_t kBitsPerByte = 8U;

std::string to_lower(std::string value) {
  for (char& character : value) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return value;
}

StatusOr<uint8_t> parse_width_token(std::string_view token, std::string_view prefix) {
  if (token.size() <= prefix.size()) {
    return invalid_argument("Missing width for scalar type: " + std::string(token));
  }

  const size_t prefix_size = prefix.size();
  size_t end = token.size();
  const size_t underscore = token.find('_');
  if (underscore != std::string_view::npos) {
    end = underscore;
  }

  uint32_t width_bits = 0;
  for (size_t index = prefix_size; index < end; ++index) {
    const char character = token[index];
    if (!std::isdigit(static_cast<unsigned char>(character))) {
      return invalid_argument("Invalid scalar width token: " + std::string(token));
    }
    width_bits = (width_bits * 10U) + static_cast<uint32_t>(character - '0');
  }
  if (width_bits == 0 || (width_bits % kBitsPerByte) != 0U ||
      width_bits / kBitsPerByte > std::numeric_limits<uint8_t>::max()) {
    return invalid_argument("Unsupported scalar width token: " + std::string(token));
  }
  return static_cast<uint8_t>(width_bits / kBitsPerByte);
}

StatusOr<ByteOrder> parse_byte_order(std::string_view value) {
  const std::string normalized = to_lower(std::string(value));
  if (normalized == kEndianLittle || normalized == kEndianLittleShort || normalized == kEndianLittleLong) {
    return ByteOrder::kLittleEndian;
  }
  if (normalized == kEndianBig || normalized == kEndianBigShort || normalized == kEndianBigLong) {
    return ByteOrder::kBigEndian;
  }
  return invalid_argument("Unsupported endianness: " + std::string(value));
}

StatusOr<StringEncoding> parse_string_encoding(std::string_view value) {
  const std::string normalized = to_lower(std::string(value));
  if (normalized == kEncodingAscii) {
    return StringEncoding::kAscii;
  }
  if (normalized == kEncodingUtf8 || normalized == kEncodingUtf8Dashed) {
    return StringEncoding::kUtf8;
  }
  return invalid_argument("Unsupported string encoding: " + std::string(value));
}

StatusOr<std::vector<EnumValueDefinition>> parse_enum_values(const YAML::Node& node) {
  if (!node || !node.IsMap()) {
    return invalid_argument("Enum-style values require a mapping.");
  }
  std::vector<EnumValueDefinition> enum_values;
  for (const auto& item : node) {
    enum_values.push_back({.value = item.first.as<uint64_t>(), .name = item.second.as<std::string>()});
  }
  return enum_values;
}

StatusOr<BitFieldDefinition> parse_bit_field(const YAML::Node& node) {
  if (!node["name"] || !node["offset"] || !node["width"]) {
    return invalid_argument("Each bitfield requires 'name', 'offset', and 'width'.");
  }

  BitFieldDefinition bit_field;
  bit_field.name = node["name"].as<std::string>();
  bit_field.offset_bits = static_cast<uint8_t>(node["offset"].as<uint32_t>());
  bit_field.width_bits = static_cast<uint8_t>(node["width"].as<uint32_t>());
  if (node["signed"]) {
    bit_field.is_signed = node["signed"].as<bool>();
  }
  if (node["values"]) {
    const auto enum_values = parse_enum_values(node["values"]);
    if (!enum_values.ok()) {
      return enum_values.status();
    }
    bit_field.enum_values = enum_values.value();
  }
  return bit_field;
}

StatusOr<ChecksumDefinition> parse_checksum(const YAML::Node& node) {
  if (!node.IsMap() || !node["algorithm"]) {
    return invalid_argument("Checksum declarations require an 'algorithm'.");
  }

  ChecksumDefinition checksum;
  checksum.algorithm = node["algorithm"].as<std::string>();
  if (node["from"]) {
    checksum.from = node["from"].as<std::string>();
  }
  if (node["to"]) {
    checksum.to = node["to"].as<std::string>();
  }
  return checksum;
}

StatusOr<FieldDefinition> parse_field(const YAML::Node& node) {
  if (!node["name"] || !node["type"]) {
    return invalid_argument("Each field requires 'name' and 'type'.");
  }

  FieldDefinition field;
  field.name = node["name"].as<std::string>();
  const auto declared_type = node["type"].as<std::string>();
  const std::string type_name = to_lower(declared_type);

  if (type_name == kTypeBytes) {
    field.kind = FieldKind::kBytes;
  } else if (type_name == kTypeString) {
    field.kind = FieldKind::kString;
    if (node["encoding"]) {
      const auto encoding = parse_string_encoding(node["encoding"].as<std::string>());
      if (!encoding.ok()) {
        return encoding.status();
      }
      field.string_encoding = encoding.value();
    }
  } else if (type_name == kTypeEnum) {
    field.kind = FieldKind::kEnum;
    if (node["underlying"]) {
      const std::string underlying = to_lower(node["underlying"].as<std::string>());
      if (!underlying.starts_with(kTypeUnsignedPrefix)) {
        return invalid_argument("Enum underlying type must be an unsigned scalar.");
      }
      const auto width = parse_width_token(underlying, kTypeUnsignedPrefix);
      if (!width.ok()) {
        return width.status();
      }
      field.width_bytes = width.value();
      if (underlying.find(kBigEndianSuffix) != std::string::npos) {
        field.byte_order = ByteOrder::kBigEndian;
      }
    } else if (node["width"]) {
      field.width_bytes = static_cast<uint8_t>(node["width"].as<uint32_t>());
    } else {
      return invalid_argument("Enum fields require 'underlying' or 'width'.");
    }
    const auto enum_values = parse_enum_values(node["values"]);
    if (!enum_values.ok()) {
      return invalid_argument("Enum fields require a 'values' mapping.");
    }
    field.enum_values = enum_values.value();
  } else if (type_name.starts_with(kTypeUnsignedPrefix)) {
    field.kind = FieldKind::kUnsigned;
    const auto width = parse_width_token(type_name, kTypeUnsignedPrefix);
    if (!width.ok()) {
      return width.status();
    }
    field.width_bytes = width.value();
  } else if (type_name.starts_with(kTypeSignedPrefix)) {
    field.kind = FieldKind::kSigned;
    const auto width = parse_width_token(type_name, kTypeSignedPrefix);
    if (!width.ok()) {
      return width.status();
    }
    field.width_bytes = width.value();
  } else if (type_name.starts_with(kTypeFloat32Prefix)) {
    field.kind = FieldKind::kFloat32;
    field.width_bytes = kFloat32WidthBytes;
  } else if (type_name.starts_with(kTypeFloat64Prefix)) {
    field.kind = FieldKind::kFloat64;
    field.width_bytes = kFloat64WidthBytes;
  } else {
    field.kind = FieldKind::kStruct;
    field.referenced_type = declared_type;
  }

  if (type_name.find(kBigEndianSuffix) != std::string::npos) {
    field.byte_order = ByteOrder::kBigEndian;
  }
  if (node["endianness"]) {
    const auto byte_order = parse_byte_order(node["endianness"].as<std::string>());
    if (!byte_order.ok()) {
      return byte_order.status();
    }
    field.byte_order = byte_order.value();
  }
  if (node["size"]) {
    const auto size = node["size"].as<uint64_t>();
    if (size > std::numeric_limits<size_t>::max()) {
      return invalid_argument("Field size is too large.");
    }
    field.fixed_size = static_cast<size_t>(size);
  }
  if (node["size_from"]) {
    field.size_from_field = node["size_from"].as<std::string>();
  }
  if (node["expect"]) {
    field.has_expected_unsigned = true;
    field.expected_unsigned = node["expect"].as<uint64_t>();
  }
  if (node["bits"]) {
    if (!node["bits"].IsSequence()) {
      return invalid_argument("Bitfields must be declared as a sequence.");
    }
    for (const YAML::Node& bit_field_node : node["bits"]) {
      auto bit_field = parse_bit_field(bit_field_node);
      if (!bit_field.ok()) {
        return bit_field.status();
      }
      field.bit_fields.push_back(std::move(bit_field).value());
    }
  }
  if (node["checksum"]) {
    auto checksum = parse_checksum(node["checksum"]);
    if (!checksum.ok()) {
      return checksum.status();
    }
    field.checksum = std::move(checksum).value();
  }
  return field;
}

template <typename Definition>
StatusOr<std::vector<Definition>> parse_layout_sequence(const YAML::Node& node,
                                                        std::string_view node_name,
                                                        std::string_view kind_name) {
  std::vector<Definition> definitions;
  if (!node) {
    return definitions;
  }
  if (!node.IsSequence()) {
    return invalid_argument("Protocol YAML field '" + std::string(node_name) + "' must be a sequence.");
  }
  for (const YAML::Node& layout_node : node) {
    if (!layout_node["name"]) {
      return invalid_argument("Every " + std::string(kind_name) + " requires 'name'.");
    }
    Definition definition;
    definition.name = layout_node["name"].as<std::string>();
    if constexpr (std::is_same_v<Definition, MessageDefinition>) {
      if (layout_node["allow_trailing_bytes"]) {
        definition.allow_trailing_bytes = layout_node["allow_trailing_bytes"].as<bool>();
      }
    }
    if (!layout_node["fields"] || !layout_node["fields"].IsSequence()) {
      return invalid_argument("Every " + std::string(kind_name) + " requires a 'fields' sequence.");
    }
    for (const YAML::Node& field_node : layout_node["fields"]) {
      auto field = parse_field(field_node);
      if (!field.ok()) {
        return field.status();
      }
      definition.fields.push_back(std::move(field).value());
    }
    definitions.push_back(std::move(definition));
  }
  return definitions;
}

}  // namespace

StatusOr<ProtocolDefinition> load_protocol_definition_from_yaml(std::string_view yaml_text) {
  try {
    const YAML::Node root = YAML::Load(std::string(yaml_text));
    ProtocolDefinition definition;
    if (root["protocol"]) {
      definition.name = root["protocol"].as<std::string>();
    } else if (root["name"]) {
      definition.name = root["name"].as<std::string>();
    } else {
      return invalid_argument("Protocol YAML requires 'protocol' or 'name'.");
    }

    const auto structs = parse_layout_sequence<StructDefinition>(root["structs"], "structs", "struct");
    if (!structs.ok()) {
      return structs.status();
    }
    definition.structs = structs.value();

    const auto messages = parse_layout_sequence<MessageDefinition>(root["messages"], "messages", "message");
    if (!messages.ok()) {
      return messages.status();
    }
    if (messages.value().empty()) {
      return invalid_argument("Protocol YAML requires a 'messages' sequence.");
    }
    definition.messages = messages.value();
    return definition;
  } catch (const std::exception& exception) {
    return schema_error(exception.what());
  }
}

StatusOr<ProtocolDefinition> load_protocol_definition_from_file(const std::string& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return io_error("Unable to open protocol file: " + path);
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return load_protocol_definition_from_yaml(buffer.str());
}

}  // namespace universal_protocol_runtime
