#include "universal_protocol_runtime/pdl/yaml_loader.hpp"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace universal_protocol_runtime {
namespace {

constexpr uint8_t kFloat32WidthBytes = sizeof(uint32_t);
constexpr uint8_t kFloat64WidthBytes = sizeof(uint64_t);
constexpr std::string_view kTypeBytes = "bytes";
constexpr std::string_view kTypeString = "string";
constexpr std::string_view kTypeAscii = "ascii";
constexpr std::string_view kTypeUtf8 = "utf8";
constexpr std::string_view kTypeEnum = "enum";
constexpr std::string_view kTypeVariant = "variant";
constexpr std::string_view kTypeReserved = "reserved";
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
constexpr std::string_view kFormatYaml = "yaml";
constexpr std::string_view kFormatYml = "yml";
constexpr std::string_view kFormatUpr = "upr";
constexpr uint32_t kBitsPerByte = 8U;

std::string to_lower(std::string value) {
  for (char& character : value) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return value;
}

StatusOr<ValidationOperator> parse_validation_operator(std::string_view value) {
  const std::string normalized = to_lower(std::string(value));
  if (normalized == "eq" || normalized == "==") {
    return ValidationOperator::kEq;
  }
  if (normalized == "ne" || normalized == "!=") {
    return ValidationOperator::kNe;
  }
  if (normalized == "lt" || normalized == "<") {
    return ValidationOperator::kLt;
  }
  if (normalized == "le" || normalized == "<=") {
    return ValidationOperator::kLe;
  }
  if (normalized == "gt" || normalized == ">") {
    return ValidationOperator::kGt;
  }
  if (normalized == "ge" || normalized == ">=") {
    return ValidationOperator::kGe;
  }
  return invalid_argument("Unsupported validation operator: " + std::string(value));
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
  enum_values.reserve(node.size());
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

StatusOr<ConditionDefinition> parse_condition(const YAML::Node& node) {
  if (!node.IsMap() || !node["field"] || !node["equals"]) {
    return invalid_argument("Conditional fields require 'field' and 'equals'.");
  }
  return ConditionDefinition{
      .field = node["field"].as<std::string>(),
      .equals_unsigned = node["equals"].as<uint64_t>(),
  };
}

StatusOr<PresenceDefinition> parse_presence(const YAML::Node& node) {
  if (!node.IsMap() || !node["field"] || !node["bit"]) {
    return invalid_argument("Presence-gated fields require 'field' and 'bit'.");
  }
  return PresenceDefinition{
      .field = node["field"].as<std::string>(),
      .bit_index = static_cast<uint8_t>(node["bit"].as<uint32_t>()),
  };
}

StatusOr<std::vector<VariantCaseDefinition>> parse_variant_cases(const YAML::Node& node) {
  if (!node || !node.IsMap()) {
    return invalid_argument("Variant cases require a mapping.");
  }
  std::vector<VariantCaseDefinition> cases;
  cases.reserve(node.size());
  for (const auto& item : node) {
    cases.push_back({
        .tag_value = item.first.as<uint64_t>(),
        .referenced_type = item.second.as<std::string>(),
    });
  }
  return cases;
}

StatusOr<ValidationRuleDefinition> parse_validation_rule(const YAML::Node& node) {
  if (!node.IsMap() || !node["field"] || !node["op"]) {
    return invalid_argument("Validation rules require 'field' and 'op'.");
  }
  ValidationRuleDefinition rule;
  rule.field = node["field"].as<std::string>();
  const auto op = parse_validation_operator(node["op"].as<std::string>());
  if (!op.ok()) {
    return op.status();
  }
  rule.op = op.value();
  if (node["other_field"]) {
    rule.compare_to_field = true;
    rule.other_field = node["other_field"].as<std::string>();
  } else if (node["value"]) {
    rule.value = node["value"].as<uint64_t>();
  } else {
    return invalid_argument("Validation rules require either 'other_field' or 'value'.");
  }
  if (node["multiplier"]) {
    rule.multiplier = node["multiplier"].as<uint64_t>();
  }
  if (node["if"]) {
    auto condition = parse_condition(node["if"]);
    if (!condition.ok()) {
      return condition.status();
    }
    rule.when = std::move(condition).value();
  }
  return rule;
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
  } else if (type_name == kTypeReserved) {
    field.kind = FieldKind::kBytes;
    field.is_reserved = true;
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
  } else if (type_name == kTypeVariant) {
    field.kind = FieldKind::kVariant;
    if (!node["tag_from"]) {
      return invalid_argument("Variant fields require 'tag_from'.");
    }
    field.tag_from_field = node["tag_from"].as<std::string>();
    const auto cases = parse_variant_cases(node["cases"]);
    if (!cases.ok()) {
      return cases.status();
    }
    field.variant_cases = cases.value();
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
  if (node["align"]) {
    field.alignment = static_cast<size_t>(node["align"].as<uint64_t>());
  }
  if (node["reserved_fill"]) {
    field.is_reserved = true;
    field.reserved_fill_byte = static_cast<uint8_t>(node["reserved_fill"].as<uint32_t>());
  }
  if (node["count"]) {
    field.fixed_count = static_cast<size_t>(node["count"].as<uint64_t>());
    field.kind = FieldKind::kCollection;
  }
  if (node["count_from"]) {
    field.count_from_field = node["count_from"].as<std::string>();
    field.kind = FieldKind::kCollection;
  }
  if (node["expect"]) {
    field.has_expected_unsigned = true;
    field.expected_unsigned = node["expect"].as<uint64_t>();
  }
  if (node["bits"]) {
    if (!node["bits"].IsSequence()) {
      return invalid_argument("Bitfields must be declared as a sequence.");
    }
    field.bit_fields.reserve(node["bits"].size());
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
  if (node["if"]) {
    auto condition = parse_condition(node["if"]);
    if (!condition.ok()) {
      return condition.status();
    }
    field.condition = std::move(condition).value();
  }
  if (node["present"]) {
    auto presence = parse_presence(node["present"]);
    if (!presence.ok()) {
      return presence.status();
    }
    field.presence = std::move(presence).value();
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
  definitions.reserve(node.size());
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
    definition.fields.reserve(layout_node["fields"].size());
    for (const YAML::Node& field_node : layout_node["fields"]) {
      auto field = parse_field(field_node);
      if (!field.ok()) {
        return field.status();
      }
      definition.fields.push_back(std::move(field).value());
    }
    if (layout_node["validations"]) {
      if (!layout_node["validations"].IsSequence()) {
        return invalid_argument("Layout validations must be declared as a sequence.");
      }
      definition.validations.reserve(layout_node["validations"].size());
      for (const YAML::Node& validation_node : layout_node["validations"]) {
        auto rule = parse_validation_rule(validation_node);
        if (!rule.ok()) {
          return rule.status();
        }
        definition.validations.push_back(std::move(rule).value());
      }
    }
    definitions.push_back(std::move(definition));
  }
  return definitions;
}

StatusOr<std::vector<EnumDefinition>> parse_enum_sequence(const YAML::Node& node) {
  std::vector<EnumDefinition> definitions;
  if (!node) {
    return definitions;
  }
  if (!node.IsSequence()) {
    return invalid_argument("Protocol YAML field 'enums' must be a sequence.");
  }
  definitions.reserve(node.size());
  for (const YAML::Node& enum_node : node) {
    if (!enum_node["name"]) {
      return invalid_argument("Every enum requires 'name'.");
    }
    EnumDefinition definition;
    definition.name = enum_node["name"].as<std::string>();
    if (enum_node["underlying"]) {
      const std::string underlying = to_lower(enum_node["underlying"].as<std::string>());
      if (!underlying.starts_with(kTypeUnsignedPrefix)) {
        return invalid_argument("Enum underlying type must be an unsigned scalar.");
      }
      const auto width = parse_width_token(underlying, kTypeUnsignedPrefix);
      if (!width.ok()) {
        return width.status();
      }
      definition.width_bytes = width.value();
      if (underlying.find(kBigEndianSuffix) != std::string::npos) {
        definition.byte_order = ByteOrder::kBigEndian;
      }
    } else if (enum_node["width"]) {
      definition.width_bytes = static_cast<uint8_t>(enum_node["width"].as<uint32_t>());
    } else {
      return invalid_argument("Enum declarations require 'underlying' or 'width'.");
    }
    if (enum_node["endianness"]) {
      const auto byte_order = parse_byte_order(enum_node["endianness"].as<std::string>());
      if (!byte_order.ok()) {
        return byte_order.status();
      }
      definition.byte_order = byte_order.value();
    }
    const auto values = parse_enum_values(enum_node["values"]);
    if (!values.ok()) {
      return invalid_argument("Enum declarations require a 'values' mapping.");
    }
    definition.values = values.value();
    definitions.push_back(std::move(definition));
  }
  return definitions;
}

StatusOr<std::vector<ImportDefinition>> parse_import_sequence(const YAML::Node& node) {
  std::vector<ImportDefinition> imports;
  if (!node) {
    return imports;
  }
  if (!node.IsSequence()) {
    return invalid_argument("Protocol YAML field 'imports' must be a sequence.");
  }
  imports.reserve(node.size());
  for (const YAML::Node& import_node : node) {
    imports.push_back(ImportDefinition{.path = import_node.as<std::string>()});
  }
  return imports;
}

enum class UprTokenKind {
  KIdentifier,
  KNumber,
  KString,
  KPunctuation,
  KEnd,
};

struct UprToken {
  UprTokenKind kind = UprTokenKind::KEnd;
  std::string text;
  size_t line = 1;
  size_t column = 1;
};

class UprLexer {
 public:
  explicit UprLexer(std::string_view input) : input_(input) {}

  StatusOr<std::vector<UprToken>> tokenize() {
    std::vector<UprToken> tokens;
    while (true) {
      skip_whitespace_and_comments();
      if (eof()) {
        tokens.push_back(UprToken{.kind = UprTokenKind::KEnd, .text = {}, .line = line_, .column = column_});
        return tokens;
      }

      const char character = peek();
      if (is_identifier_start(character)) {
        tokens.push_back(read_identifier());
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(character))) {
        const auto number = read_number();
        if (!number.ok()) {
          return number.status();
        }
        tokens.push_back(number.value());
        continue;
      }
      if (character == '"' || character == '\'') {
        const auto string_token = read_string();
        if (!string_token.ok()) {
          return string_token.status();
        }
        tokens.push_back(string_token.value());
        continue;
      }
      if (is_punctuation(character)) {
        tokens.push_back(read_punctuation());
        continue;
      }
      return error("Unexpected character.");
    }
  }

 private:
  bool eof() const noexcept { return index_ >= input_.size(); }

  char peek(size_t lookahead = 0) const noexcept {
    const size_t position = index_ + lookahead;
    return position < input_.size() ? input_[position] : '\0';
  }

  char advance() noexcept {
    const char character = input_[index_++];
    if (character == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
    return character;
  }

  static bool is_identifier_start(char character) noexcept {
    return std::isalpha(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '$';
  }

  static bool is_identifier_part(char character) noexcept {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' || character == '.' ||
           character == '-';
  }

  static bool is_punctuation(char character) noexcept {
    switch (character) {
      case '{':
      case '}':
      case '[':
      case ']':
      case '(':
      case ')':
      case ':':
      case ',':
      case '@':
      case '!':
      case '*':
      case '=':
      case '<':
      case '>':
        return true;
      default:
        return false;
    }
  }

  void skip_whitespace_and_comments() noexcept {
    while (!eof()) {
      const char character = peek();
      if (std::isspace(static_cast<unsigned char>(character)) != 0) {
        advance();
        continue;
      }
      if (character == '#') {
        while (!eof() && peek() != '\n') {
          advance();
        }
        continue;
      }
      if (character == '/' && peek(1) == '/') {
        advance();
        advance();
        while (!eof() && peek() != '\n') {
          advance();
        }
        continue;
      }
      break;
    }
  }

  UprToken read_identifier() {
    UprToken token{.kind = UprTokenKind::KIdentifier, .text = {}, .line = line_, .column = column_};
    while (!eof() && is_identifier_part(peek())) {
      token.text.push_back(advance());
    }
    return token;
  }

  StatusOr<UprToken> read_number() {
    UprToken token{.kind = UprTokenKind::KNumber, .text = {}, .line = line_, .column = column_};
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
      token.text.push_back(advance());
      token.text.push_back(advance());
      if (!std::isxdigit(static_cast<unsigned char>(peek()))) {
        return error("Invalid hexadecimal literal.");
      }
      while (!eof() && std::isxdigit(static_cast<unsigned char>(peek())) != 0) {
        token.text.push_back(advance());
      }
      return token;
    }
    while (!eof() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
      token.text.push_back(advance());
    }
    return token;
  }

  StatusOr<UprToken> read_string() {
    UprToken token{.kind = UprTokenKind::KString, .text = {}, .line = line_, .column = column_};
    const char quote = advance();
    while (!eof()) {
      const char character = advance();
      if (character == quote) {
        return token;
      }
      if (character == '\\') {
        if (eof()) {
          return error("Unterminated escape sequence.");
        }
        const char escaped = advance();
        switch (escaped) {
          case '\\':
          case '\'':
          case '"':
            token.text.push_back(escaped);
            break;
          case 'n':
            token.text.push_back('\n');
            break;
          case 'r':
            token.text.push_back('\r');
            break;
          case 't':
            token.text.push_back('\t');
            break;
          default:
            return error("Unsupported escape sequence.");
        }
        continue;
      }
      token.text.push_back(character);
    }
    return error("Unterminated string literal.");
  }

  UprToken read_punctuation() {
    UprToken token{.kind = UprTokenKind::KPunctuation, .text = {}, .line = line_, .column = column_};
    token.text.push_back(advance());
    return token;
  }

  Status error(std::string_view message) const {
    return invalid_argument("UPR parse error at " + std::to_string(line_) + ":" + std::to_string(column_) + ": " +
                            std::string(message));
  }

  std::string_view input_;
  size_t index_ = 0;
  size_t line_ = 1;
  size_t column_ = 1;
};

class UprParser {
 public:
  UprParser(std::vector<UprToken> tokens, bool require_root_declaration)
      : tokens_(std::move(tokens)), require_root_declaration_(require_root_declaration) {}

  StatusOr<ProtocolDefinition> parse() {
    ProtocolDefinition definition;
    while (!is_end()) {
      if (match_identifier("protocol")) {
        if (!definition.name.empty()) {
          return error("Protocol name already declared.");
        }
        const auto protocol_name = parse_name("protocol name");
        if (!protocol_name.ok()) {
          return protocol_name.status();
        }
        definition.name = protocol_name.value();
        continue;
      }
      if (match_identifier("import")) {
        const auto import_path = parse_name("import path");
        if (!import_path.ok()) {
          return import_path.status();
        }
        definition.imports.push_back(ImportDefinition{.path = import_path.value()});
        consume_optional_separator();
        continue;
      }
      if (match_identifier("enum")) {
        auto parsed_enum = parse_enum_declaration();
        if (!parsed_enum.ok()) {
          return parsed_enum.status();
        }
        definition.enums.push_back(std::move(parsed_enum).value());
        continue;
      }
      if (match_identifier("struct")) {
        auto parsed_struct = parse_struct();
        if (!parsed_struct.ok()) {
          return parsed_struct.status();
        }
        definition.structs.push_back(std::move(parsed_struct).value());
        continue;
      }
      if (match_identifier("message")) {
        auto parsed_message = parse_message();
        if (!parsed_message.ok()) {
          return parsed_message.status();
        }
        definition.messages.push_back(std::move(parsed_message).value());
        continue;
      }
      return error("Expected 'protocol', 'import', 'enum', 'struct', or 'message'.");
    }

    if (require_root_declaration_) {
      if (definition.name.empty()) {
        return invalid_argument("UPR schema requires a protocol name.");
      }
      if (definition.messages.empty()) {
        return invalid_argument("UPR schema requires at least one message.");
      }
    }
    return definition;
  }

 private:
  StatusOr<EnumDefinition> parse_enum_declaration() {
    EnumDefinition definition;
    const auto name = parse_name("enum name");
    if (!name.ok()) {
      return name.status();
    }
    definition.name = name.value();
    if (!expect_punctuation(":", "Expected ':' after enum name.")) {
      return current_error();
    }
    const auto underlying = parse_identifier("enum underlying type");
    if (!underlying.ok()) {
      return underlying.status();
    }
    const std::string normalized_underlying = to_lower(underlying.value());
    if (!normalized_underlying.starts_with(kTypeUnsignedPrefix)) {
      return invalid_argument("Enum underlying type must be an unsigned scalar.");
    }
    const auto width = parse_width_token(normalized_underlying, kTypeUnsignedPrefix);
    if (!width.ok()) {
      return width.status();
    }
    definition.width_bytes = width.value();
    if (normalized_underlying.find(kBigEndianSuffix) != std::string::npos) {
      definition.byte_order = ByteOrder::kBigEndian;
    }
    if (!expect_punctuation("{", "Expected '{' after enum declaration.")) {
      return current_error();
    }
    const auto values = parse_enum_body();
    if (!values.ok()) {
      return values.status();
    }
    definition.values = std::move(values).value();
    return definition;
  }

  StatusOr<StructDefinition> parse_struct() {
    StructDefinition definition;
    const auto name = parse_name("struct name");
    if (!name.ok()) {
      return name.status();
    }
    definition.name = name.value();
    if (!expect_punctuation("{", "Expected '{' after struct name.")) {
      return current_error();
    }
    while (!match_punctuation("}")) {
      if (match_identifier("validate")) {
        auto validation = parse_validation_statement();
        if (!validation.ok()) {
          return validation.status();
        }
        definition.validations.push_back(std::move(validation).value());
        consume_optional_separator();
        continue;
      }
      auto field = parse_field_definition();
      if (!field.ok()) {
        return field.status();
      }
      definition.fields.push_back(std::move(field).value());
      consume_optional_separator();
    }
    return definition;
  }

  StatusOr<MessageDefinition> parse_message() {
    MessageDefinition definition;
    const auto name = parse_name("message name");
    if (!name.ok()) {
      return name.status();
    }
    definition.name = name.value();
    if (match_identifier("allow_trailing_bytes")) {
      definition.allow_trailing_bytes = true;
    }
    if (!expect_punctuation("{", "Expected '{' after message declaration.")) {
      return current_error();
    }
    while (!match_punctuation("}")) {
      if (match_identifier("allow_trailing_bytes")) {
        definition.allow_trailing_bytes = true;
        consume_optional_separator();
        continue;
      }
      if (match_identifier("validate")) {
        auto validation = parse_validation_statement();
        if (!validation.ok()) {
          return validation.status();
        }
        definition.validations.push_back(std::move(validation).value());
        consume_optional_separator();
        continue;
      }
      auto field = parse_field_definition();
      if (!field.ok()) {
        return field.status();
      }
      definition.fields.push_back(std::move(field).value());
      consume_optional_separator();
    }
    return definition;
  }

  StatusOr<FieldDefinition> parse_field_definition() {
    FieldDefinition field;
    const auto field_name = parse_name("field name");
    if (!field_name.ok()) {
      return field_name.status();
    }
    field.name = field_name.value();
    if (!expect_punctuation(":", "Expected ':' after field name.")) {
      return current_error();
    }
    auto field_status = parse_field_type(&field);
    if (!field_status.ok()) {
      return field_status;
    }

    while (true) {
      if (match_punctuation("=")) {
        const auto number = parse_u64("expected field value");
        if (!number.ok()) {
          return number.status();
        }
        field.has_expected_unsigned = true;
        field.expected_unsigned = number.value();
        continue;
      }
      if (peek_is_identifier("checksum") && peek_punctuation_after_current("(")) {
        advance();
        auto checksum = parse_checksum_suffix();
        if (!checksum.ok()) {
          return checksum.status();
        }
        field.checksum = std::move(checksum).value();
        continue;
      }
      if (peek_is_identifier("if") && peek_punctuation_after_current("(")) {
        advance();
        auto condition = parse_condition_suffix();
        if (!condition.ok()) {
          return condition.status();
        }
        field.condition = std::move(condition).value();
        continue;
      }
      if (peek_is_identifier("present") && peek_punctuation_after_current("(")) {
        advance();
        auto presence = parse_presence_suffix();
        if (!presence.ok()) {
          return presence.status();
        }
        field.presence = std::move(presence).value();
        continue;
      }
      if (peek_is_identifier("align") && peek_punctuation_after_current("(")) {
        advance();
        auto alignment = parse_alignment_suffix();
        if (!alignment.ok()) {
          return alignment.status();
        }
        field.alignment = alignment.value();
        continue;
      }
      if (match_punctuation("{")) {
        if (field.kind == FieldKind::kEnum) {
          auto enum_values = parse_enum_body();
          if (!enum_values.ok()) {
            return enum_values.status();
          }
          field.enum_values = std::move(enum_values).value();
        } else if (field.kind == FieldKind::kVariant) {
          auto variant_cases = parse_variant_case_body();
          if (!variant_cases.ok()) {
            return variant_cases.status();
          }
          field.variant_cases = std::move(variant_cases).value();
        } else {
          auto bit_fields = parse_bit_field_body();
          if (!bit_fields.ok()) {
            return bit_fields.status();
          }
          field.bit_fields = std::move(bit_fields).value();
        }
        continue;
      }
      break;
    }

    return field;
  }

  Status parse_field_type(FieldDefinition* field) {
    const UprToken& token = peek();
    if (token.kind != UprTokenKind::KIdentifier) {
      return error("Expected a field type.");
    }

    const std::string type_name = to_lower(token.text);
    advance();

    if (type_name == kTypeBytes) {
      field->kind = FieldKind::kBytes;
      return parse_optional_size_suffix(field);
    }
    if (type_name == kTypeReserved) {
      field->kind = FieldKind::kBytes;
      field->is_reserved = true;
      return parse_optional_size_suffix(field);
    }
    if (type_name == kTypeString) {
      field->kind = FieldKind::kString;
      return parse_optional_size_suffix(field);
    }
    if (type_name == kTypeAscii || type_name == kTypeUtf8) {
      field->kind = FieldKind::kString;
      field->string_encoding = type_name == kTypeAscii ? StringEncoding::kAscii : StringEncoding::kUtf8;
      return parse_optional_size_suffix(field);
    }
    if (type_name == kTypeEnum) {
      field->kind = FieldKind::kEnum;
      if (!expect_punctuation("<", "Expected '<' after enum.")) {
        return current_error();
      }
      const auto underlying = parse_identifier("enum underlying type");
      if (!underlying.ok()) {
        return underlying.status();
      }
      const std::string normalized_underlying = to_lower(underlying.value());
      if (!normalized_underlying.starts_with(kTypeUnsignedPrefix)) {
        return invalid_argument("Enum underlying type must be an unsigned scalar.");
      }
      const auto width = parse_width_token(normalized_underlying, kTypeUnsignedPrefix);
      if (!width.ok()) {
        return width.status();
      }
      field->width_bytes = width.value();
      if (normalized_underlying.find(kBigEndianSuffix) != std::string::npos) {
        field->byte_order = ByteOrder::kBigEndian;
      }
      if (!expect_punctuation(">", "Expected '>' after enum underlying type.")) {
        return current_error();
      }
      return Status::ok_status();
    }
    if (type_name == kTypeVariant) {
      field->kind = FieldKind::kVariant;
      if (!expect_punctuation("(", "Expected '(' after variant.")) {
        return current_error();
      }
      const auto tag_from = parse_name("variant tag field");
      if (!tag_from.ok()) {
        return tag_from.status();
      }
      field->tag_from_field = tag_from.value();
      if (!expect_punctuation(")", "Expected ')' after variant tag field.")) {
        return current_error();
      }
      return Status::ok_status();
    }
    if (type_name.starts_with(kTypeUnsignedPrefix)) {
      field->kind = FieldKind::kUnsigned;
      const auto width = parse_width_token(type_name, kTypeUnsignedPrefix);
      if (!width.ok()) {
        return width.status();
      }
      field->width_bytes = width.value();
      if (type_name.find(kBigEndianSuffix) != std::string::npos) {
        field->byte_order = ByteOrder::kBigEndian;
      }
      return Status::ok_status();
    }
    if (type_name.starts_with(kTypeSignedPrefix)) {
      field->kind = FieldKind::kSigned;
      const auto width = parse_width_token(type_name, kTypeSignedPrefix);
      if (!width.ok()) {
        return width.status();
      }
      field->width_bytes = width.value();
      if (type_name.find(kBigEndianSuffix) != std::string::npos) {
        field->byte_order = ByteOrder::kBigEndian;
      }
      return Status::ok_status();
    }
    if (type_name.starts_with(kTypeFloat32Prefix)) {
      field->kind = FieldKind::kFloat32;
      field->width_bytes = kFloat32WidthBytes;
      if (type_name.find(kBigEndianSuffix) != std::string::npos) {
        field->byte_order = ByteOrder::kBigEndian;
      }
      return Status::ok_status();
    }
    if (type_name.starts_with(kTypeFloat64Prefix)) {
      field->kind = FieldKind::kFloat64;
      field->width_bytes = kFloat64WidthBytes;
      if (type_name.find(kBigEndianSuffix) != std::string::npos) {
        field->byte_order = ByteOrder::kBigEndian;
      }
      return Status::ok_status();
    }

    field->kind = FieldKind::kStruct;
    field->referenced_type = token.text;
    return parse_optional_count_suffix(field);
  }

  Status parse_optional_size_suffix(FieldDefinition* field) {
    if (!match_punctuation("[")) {
      return Status::ok_status();
    }
    const UprToken& token = peek();
    if (token.kind == UprTokenKind::KNumber) {
      const auto size = parse_u64("field size");
      if (!size.ok()) {
        return size.status();
      }
      if (size.value() > std::numeric_limits<size_t>::max()) {
        return invalid_argument("Field size is too large.");
      }
      field->fixed_size = static_cast<size_t>(size.value());
    } else if (token.kind == UprTokenKind::KIdentifier || token.kind == UprTokenKind::KString) {
      const auto size_from = parse_name("size_from field");
      if (!size_from.ok()) {
        return size_from.status();
      }
      field->size_from_field = size_from.value();
    } else {
      return error("Expected a fixed size or field reference inside '[...]'.");
    }
    if (!expect_punctuation("]", "Expected ']' after field size.")) {
      return current_error();
    }
    return Status::ok_status();
  }

  Status parse_optional_count_suffix(FieldDefinition* field) {
    if (!match_punctuation("[")) {
      return Status::ok_status();
    }
    const UprToken& token = peek();
    field->kind = FieldKind::kCollection;
    if (token.kind == UprTokenKind::KNumber) {
      const auto count = parse_u64("collection count");
      if (!count.ok()) {
        return count.status();
      }
      if (count.value() > std::numeric_limits<size_t>::max()) {
        return invalid_argument("Collection count is too large.");
      }
      field->fixed_count = static_cast<size_t>(count.value());
    } else if (token.kind == UprTokenKind::KIdentifier || token.kind == UprTokenKind::KString) {
      const auto count_from = parse_name("count_from field");
      if (!count_from.ok()) {
        return count_from.status();
      }
      field->count_from_field = count_from.value();
    } else {
      return error("Expected a fixed count or field reference inside '[...]'.");
    }
    if (!expect_punctuation("]", "Expected ']' after collection count.")) {
      return current_error();
    }
    return Status::ok_status();
  }

  StatusOr<std::vector<EnumValueDefinition>> parse_enum_body() {
    std::vector<EnumValueDefinition> values;
    while (!match_punctuation("}")) {
      const auto numeric_value = parse_u64("enum value");
      if (!numeric_value.ok()) {
        return numeric_value.status();
      }
      if (!expect_punctuation("=", "Expected '=' in enum value declaration.")) {
        return current_error();
      }
      const auto label = parse_name("enum label");
      if (!label.ok()) {
        return label.status();
      }
      values.push_back({.value = numeric_value.value(), .name = label.value()});
      consume_optional_separator();
    }
    return values;
  }

  StatusOr<std::vector<BitFieldDefinition>> parse_bit_field_body() {
    std::vector<BitFieldDefinition> bit_fields;
    while (!match_punctuation("}")) {
      BitFieldDefinition bit_field;
      const auto bit_field_name = parse_name("bitfield name");
      if (!bit_field_name.ok()) {
        return bit_field_name.status();
      }
      bit_field.name = bit_field_name.value();
      if (!expect_punctuation("@", "Expected '@' in bitfield declaration.")) {
        return current_error();
      }
      const auto offset_bits = parse_u64("bitfield offset");
      if (!offset_bits.ok()) {
        return offset_bits.status();
      }
      if (!expect_punctuation(":", "Expected ':' between bitfield offset and width.")) {
        return current_error();
      }
      const auto width_bits = parse_u64("bitfield width");
      if (!width_bits.ok()) {
        return width_bits.status();
      }
      if (offset_bits.value() > std::numeric_limits<uint8_t>::max() ||
          width_bits.value() > std::numeric_limits<uint8_t>::max()) {
        return invalid_argument("Bitfield offset and width must fit in 8 bits.");
      }
      bit_field.offset_bits = static_cast<uint8_t>(offset_bits.value());
      bit_field.width_bits = static_cast<uint8_t>(width_bits.value());
      if (match_identifier("signed")) {
        bit_field.is_signed = true;
      }
      if (match_punctuation("{")) {
        const auto enum_values = parse_enum_body();
        if (!enum_values.ok()) {
          return enum_values.status();
        }
        bit_field.enum_values = std::move(enum_values).value();
      }
      bit_fields.push_back(std::move(bit_field));
      consume_optional_separator();
    }
    return bit_fields;
  }

  StatusOr<ChecksumDefinition> parse_checksum_suffix() {
    if (!expect_punctuation("(", "Expected '(' after checksum.")) {
      return current_error();
    }
    ChecksumDefinition checksum;
    const auto algorithm = parse_identifier("checksum algorithm");
    if (!algorithm.ok()) {
      return algorithm.status();
    }
    checksum.algorithm = algorithm.value();
    if (match_punctuation(",")) {
      const auto from = parse_name("checksum from anchor");
      if (!from.ok()) {
        return from.status();
      }
      checksum.from = from.value();
      if (match_punctuation(",")) {
        const auto to = parse_name("checksum to anchor");
        if (!to.ok()) {
          return to.status();
        }
        checksum.to = to.value();
      }
    }
    if (!expect_punctuation(")", "Expected ')' after checksum arguments.")) {
      return current_error();
    }
    return checksum;
  }

  StatusOr<ConditionDefinition> parse_condition_suffix() {
    if (!expect_punctuation("(", "Expected '(' after if.")) {
      return current_error();
    }
    const auto field_name = parse_name("condition field");
    if (!field_name.ok()) {
      return field_name.status();
    }
    if (!expect_punctuation("=", "Expected '==' in conditional field declaration.") ||
        !expect_punctuation("=", "Expected '==' in conditional field declaration.")) {
      return current_error();
    }
    const auto expected_value = parse_u64("conditional expected value");
    if (!expected_value.ok()) {
      return expected_value.status();
    }
    if (!expect_punctuation(")", "Expected ')' after conditional field declaration.")) {
      return current_error();
    }
    return ConditionDefinition{
        .field = field_name.value(),
        .equals_unsigned = expected_value.value(),
    };
  }

  StatusOr<PresenceDefinition> parse_presence_suffix() {
    if (!expect_punctuation("(", "Expected '(' after present.")) {
      return current_error();
    }
    const auto field_name = parse_name("presence field");
    if (!field_name.ok()) {
      return field_name.status();
    }
    if (!expect_punctuation(",", "Expected ',' after presence field.")) {
      return current_error();
    }
    const auto bit_index = parse_u64("presence bit index");
    if (!bit_index.ok()) {
      return bit_index.status();
    }
    if (bit_index.value() > std::numeric_limits<uint8_t>::max()) {
      return invalid_argument("Presence bit index must fit in 8 bits.");
    }
    if (!expect_punctuation(")", "Expected ')' after presence declaration.")) {
      return current_error();
    }
    return PresenceDefinition{
        .field = field_name.value(),
        .bit_index = static_cast<uint8_t>(bit_index.value()),
    };
  }

  StatusOr<size_t> parse_alignment_suffix() {
    if (!expect_punctuation("(", "Expected '(' after align.")) {
      return current_error();
    }
    const auto alignment = parse_u64("field alignment");
    if (!alignment.ok()) {
      return alignment.status();
    }
    if (!expect_punctuation(")", "Expected ')' after align declaration.")) {
      return current_error();
    }
    if (alignment.value() > std::numeric_limits<size_t>::max()) {
      return invalid_argument("Field alignment is too large.");
    }
    return static_cast<size_t>(alignment.value());
  }

  StatusOr<ValidationOperator> parse_validation_operator_tokens() {
    if (match_punctuation("=")) {
      if (!expect_punctuation("=", "Expected '==' in validation expression.")) {
        return current_error();
      }
      return ValidationOperator::kEq;
    }
    if (match_punctuation("!")) {
      if (!expect_punctuation("=", "Expected '!=' in validation expression.")) {
        return current_error();
      }
      return ValidationOperator::kNe;
    }
    if (match_punctuation("<")) {
      if (match_punctuation("=")) {
        return ValidationOperator::kLe;
      }
      return ValidationOperator::kLt;
    }
    if (match_punctuation(">")) {
      if (match_punctuation("=")) {
        return ValidationOperator::kGe;
      }
      return ValidationOperator::kGt;
    }
    return error("Expected a validation comparison operator.");
  }

  StatusOr<ValidationRuleDefinition> parse_validation_statement() {
    if (!expect_punctuation("(", "Expected '(' after validate.")) {
      return current_error();
    }
    ValidationRuleDefinition rule;
    const auto field_name = parse_name("validation field");
    if (!field_name.ok()) {
      return field_name.status();
    }
    rule.field = field_name.value();
    const auto op = parse_validation_operator_tokens();
    if (!op.ok()) {
      return op.status();
    }
    rule.op = op.value();
    if (peek().kind == UprTokenKind::KIdentifier || peek().kind == UprTokenKind::KString) {
      const auto other_field = parse_name("validation comparison field");
      if (!other_field.ok()) {
        return other_field.status();
      }
      rule.compare_to_field = true;
      rule.other_field = other_field.value();
    } else {
      const auto value = parse_u64("validation comparison value");
      if (!value.ok()) {
        return value.status();
      }
      rule.value = value.value();
    }
    if (match_punctuation("*")) {
      const auto multiplier = parse_u64("validation multiplier");
      if (!multiplier.ok()) {
        return multiplier.status();
      }
      rule.multiplier = multiplier.value();
    }
    if (match_punctuation(",")) {
      if (!match_identifier("if")) {
        return error("Expected 'if' after validation comma.");
      }
      auto condition = parse_condition_suffix();
      if (!condition.ok()) {
        return condition.status();
      }
      rule.when = std::move(condition).value();
    }
    if (!expect_punctuation(")", "Expected ')' after validate declaration.")) {
      return current_error();
    }
    return rule;
  }

  StatusOr<std::vector<VariantCaseDefinition>> parse_variant_case_body() {
    std::vector<VariantCaseDefinition> cases;
    while (!match_punctuation("}")) {
      const auto tag_value = parse_u64("variant tag value");
      if (!tag_value.ok()) {
        return tag_value.status();
      }
      if (!expect_punctuation("=", "Expected '=' in variant case declaration.")) {
        return current_error();
      }
      const auto referenced_type = parse_name("variant case struct");
      if (!referenced_type.ok()) {
        return referenced_type.status();
      }
      cases.push_back({
          .tag_value = tag_value.value(),
          .referenced_type = referenced_type.value(),
      });
      consume_optional_separator();
    }
    return cases;
  }

  StatusOr<uint64_t> parse_u64(std::string_view context) {
    const UprToken& token = peek();
    if (token.kind != UprTokenKind::KNumber) {
      return error("Expected " + std::string(context) + ".");
    }
    const int base = token.text.starts_with("0x") || token.text.starts_with("0X") ? 16 : 10;
    try {
      uint64_t value = std::stoull(token.text, nullptr, base);
      advance();
      return value;
    } catch (const std::exception&) {
      return error("Invalid numeric literal.");
    }
  }

  StatusOr<std::string> parse_identifier(std::string_view context) {
    const UprToken& token = peek();
    if (token.kind != UprTokenKind::KIdentifier) {
      return error("Expected " + std::string(context) + ".");
    }
    std::string value = token.text;
    advance();
    return value;
  }

  StatusOr<std::string> parse_name(std::string_view context) {
    const UprToken& token = peek();
    if (token.kind == UprTokenKind::KIdentifier || token.kind == UprTokenKind::KString) {
      std::string value = token.text;
      advance();
      return value;
    }
    return error("Expected " + std::string(context) + ".");
  }

  bool expect_punctuation(std::string_view text, std::string_view message) {
    if (match_punctuation(text)) {
      return true;
    }
    last_error_ = error(message);
    return false;
  }

  bool match_identifier(std::string_view text) {
    if (peek().kind != UprTokenKind::KIdentifier || peek().text != text) {
      return false;
    }
    advance();
    return true;
  }

  bool match_punctuation(std::string_view text) {
    if (peek().kind != UprTokenKind::KPunctuation || peek().text != text) {
      return false;
    }
    advance();
    return true;
  }

  bool peek_is_identifier(std::string_view text) const {
    return peek().kind == UprTokenKind::KIdentifier && peek().text == text;
  }

  bool peek_punctuation_after_current(std::string_view text) const {
    if (index_ + 1 >= tokens_.size()) {
      return false;
    }
    const UprToken& token = tokens_[index_ + 1];
    return token.kind == UprTokenKind::KPunctuation && token.text == text;
  }

  void consume_optional_separator() { match_punctuation(","); }

  const UprToken& peek() const noexcept { return tokens_[index_]; }

  void advance() noexcept {
    if (index_ < tokens_.size()) {
      ++index_;
    }
  }

  bool is_end() const noexcept { return peek().kind == UprTokenKind::KEnd; }

  Status error(std::string_view message) const {
    const UprToken& token = peek();
    return invalid_argument("UPR parse error at " + std::to_string(token.line) + ":" + std::to_string(token.column) +
                            ": " + std::string(message));
  }

  Status current_error() const { return last_error_.value_or(error("Unexpected token.")); }

  std::vector<UprToken> tokens_;
  size_t index_ = 0;
  std::optional<Status> last_error_;
  bool require_root_declaration_ = true;
};

StatusOr<ProtocolDefinition> load_protocol_definition_impl(  // NOLINT(misc-no-recursion)
    std::string_view schema_text,
    std::string_view format_hint,
    bool require_root_declaration) {
  const std::string normalized_hint = to_lower(std::string(format_hint));
  if (normalized_hint == kFormatYaml || normalized_hint == kFormatYml) {
    try {
      const YAML::Node root = YAML::Load(std::string(schema_text));
      ProtocolDefinition definition;
      if (root["protocol"]) {
        definition.name = root["protocol"].as<std::string>();
      } else if (root["name"]) {
        definition.name = root["name"].as<std::string>();
      } else {
        if (require_root_declaration) {
          return invalid_argument("Protocol YAML requires 'protocol' or 'name'.");
        }
      }

      const auto imports = parse_import_sequence(root["imports"]);
      if (!imports.ok()) {
        return imports.status();
      }
      definition.imports = imports.value();

      const auto enums = parse_enum_sequence(root["enums"]);
      if (!enums.ok()) {
        return enums.status();
      }
      definition.enums = enums.value();

      const auto structs = parse_layout_sequence<StructDefinition>(root["structs"], "structs", "struct");
      if (!structs.ok()) {
        return structs.status();
      }
      definition.structs = structs.value();

      const auto messages = parse_layout_sequence<MessageDefinition>(root["messages"], "messages", "message");
      if (!messages.ok()) {
        return messages.status();
      }
      if (require_root_declaration && messages.value().empty()) {
        return invalid_argument("Protocol YAML requires a 'messages' sequence.");
      }
      definition.messages = messages.value();
      return definition;
    } catch (const std::exception& exception) {
      return schema_error(exception.what());
    }
  }

  if (normalized_hint == kFormatUpr) {
    const auto tokens = UprLexer(schema_text).tokenize();
    if (!tokens.ok()) {
      return tokens.status();
    }
    return UprParser(tokens.value(), require_root_declaration).parse();
  }

  const auto upr_definition = load_protocol_definition_impl(schema_text, kFormatUpr, require_root_declaration);
  if (upr_definition.ok()) {
    return upr_definition;
  }
  return load_protocol_definition_impl(schema_text, kFormatYaml, require_root_declaration);
}

StatusOr<std::filesystem::path> find_workspace_root(const std::filesystem::path& start) {
  std::error_code error;
  std::filesystem::path current = std::filesystem::absolute(start, error);
  if (error) {
    return io_error("Unable to determine an absolute path for: " + start.string());
  }
  if (!std::filesystem::is_directory(current, error)) {
    current = current.parent_path();
  }

  while (!current.empty()) {
    if (std::filesystem::exists(current / "MODULE.bazel", error) ||
        std::filesystem::exists(current / "WORKSPACE.bazel", error) ||
        std::filesystem::exists(current / "WORKSPACE", error)) {
      return current;
    }
    const std::filesystem::path parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return not_found("Unable to locate a Bazel workspace root for schema import resolution.");
}

bool is_relative_import(std::string_view import_path) {
  return import_path.starts_with("./") || import_path.starts_with("../");
}

StatusOr<std::filesystem::path> resolve_import_path(std::string_view import_path,
                                                    const std::filesystem::path& importer_path,
                                                    const std::optional<std::filesystem::path>& workspace_root) {
  const std::filesystem::path import_fs_path(import_path);
  std::filesystem::path candidate;
  if (import_fs_path.is_absolute()) {
    candidate = import_fs_path;
  } else if (is_relative_import(import_path)) {
    candidate = importer_path.parent_path() / import_fs_path;
  } else if (workspace_root.has_value()) {
    candidate = workspace_root.value() / import_fs_path;
  } else {
    return not_found(
        "Workspace-relative import '" + std::string(import_path) +
        "' requires the schema file to be inside a Bazel workspace. Use './' or '../' for relative imports.");
  }

  std::error_code error;
  std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, error);
  if (error) {
    return io_error("Unable to resolve imported schema path: " + candidate.string());
  }
  return normalized;
}

class SchemaImportResolver {
 public:
  explicit SchemaImportResolver(std::optional<std::filesystem::path> workspace_root)
      : workspace_root_(std::move(workspace_root)) {}

  StatusOr<ProtocolDefinition> resolve(const std::filesystem::path& root_path) {
    const auto root_index = load_recursive(root_path);
    if (!root_index.ok()) {
      return root_index.status();
    }

    const ProtocolDefinition& root_definition = definitions_[root_index.value()];
    if (root_definition.name.empty()) {
      return invalid_argument("Root schema must declare a protocol name: " + root_paths_[root_index.value()].string());
    }
    if (root_definition.messages.empty()) {
      return invalid_argument("Root schema must declare at least one message: " +
                              root_paths_[root_index.value()].string());
    }

    ProtocolDefinition merged;
    merged.name = root_definition.name;

    size_t total_enums = 0;
    size_t total_structs = 0;
    size_t total_messages = 0;
    for (size_t index : postorder_) {
      total_enums += definitions_[index].enums.size();
      total_structs += definitions_[index].structs.size();
      total_messages += definitions_[index].messages.size();
    }
    merged.enums.reserve(total_enums);
    merged.structs.reserve(total_structs);
    merged.messages.reserve(total_messages);

    for (size_t index : postorder_) {
      ProtocolDefinition& definition = definitions_[index];
      std::move(definition.enums.begin(), definition.enums.end(), std::back_inserter(merged.enums));
      std::move(definition.structs.begin(), definition.structs.end(), std::back_inserter(merged.structs));
      std::move(definition.messages.begin(), definition.messages.end(), std::back_inserter(merged.messages));
    }
    return merged;
  }

 private:
  enum class VisitState {
    KVisiting,
    KDone,
  };

  StatusOr<size_t> load_recursive(const std::filesystem::path& path) {  // NOLINT(misc-no-recursion)
    std::error_code error;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
    if (error) {
      return io_error("Unable to resolve protocol file: " + path.string());
    }
    const std::string key = normalized.generic_string();

    const auto existing_state = states_.find(key);
    if (existing_state != states_.end()) {
      if (existing_state->second == VisitState::KVisiting) {
        return invalid_argument("Schema import cycle detected at: " + normalized.string());
      }
      return indices_.at(key);
    }

    std::ifstream input(normalized);
    if (!input.is_open()) {
      return io_error("Unable to open protocol file: " + normalized.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();

    const std::string extension = to_lower(normalized.extension().string());
    const auto parsed = [&]() -> StatusOr<ProtocolDefinition> {
      if (extension == ".upr") {
        return load_protocol_definition_impl(buffer.str(), kFormatUpr, false);
      }
      if (extension == ".yaml" || extension == ".yml") {
        return load_protocol_definition_impl(buffer.str(), kFormatYaml, false);
      }
      return load_protocol_definition_impl(buffer.str(), {}, false);
    }();
    if (!parsed.ok()) {
      return parsed.status();
    }

    const size_t index = definitions_.size();
    definitions_.push_back(parsed.value());
    root_paths_.push_back(normalized);
    indices_.emplace(key, index);
    states_.emplace(key, VisitState::KVisiting);

    const std::vector<ImportDefinition> imports = definitions_[index].imports;
    for (const ImportDefinition& import_definition : imports) {
      const auto resolved_import = resolve_import_path(import_definition.path, normalized, workspace_root_);
      if (!resolved_import.ok()) {
        return resolved_import.status();
      }
      const auto imported_index = load_recursive(resolved_import.value());
      if (!imported_index.ok()) {
        return imported_index.status();
      }
      (void)imported_index;
    }

    definitions_[index].imports.clear();
    states_[key] = VisitState::KDone;
    postorder_.push_back(index);
    return index;
  }

  std::optional<std::filesystem::path> workspace_root_;
  std::vector<ProtocolDefinition> definitions_;
  std::vector<std::filesystem::path> root_paths_;
  std::vector<size_t> postorder_;
  std::unordered_map<std::string, VisitState> states_;
  std::unordered_map<std::string, size_t> indices_;
};

}  // namespace

StatusOr<ProtocolDefinition> load_protocol_definition_from_yaml(std::string_view yaml_text) {
  return load_protocol_definition_impl(yaml_text, kFormatYaml, true);
}

StatusOr<ProtocolDefinition> load_protocol_definition_from_upr(std::string_view upr_text) {
  return load_protocol_definition_impl(upr_text, kFormatUpr, true);
}

StatusOr<ProtocolDefinition> load_protocol_definition(std::string_view schema_text, std::string_view format_hint) {
  return load_protocol_definition_impl(schema_text, format_hint, true);
}

StatusOr<ProtocolDefinition> load_protocol_definition_from_file(const std::string& path) {
  return load_protocol_definition_from_file(path, SchemaLoadOptions{});
}

StatusOr<ProtocolDefinition> load_protocol_definition_from_file(const std::string& path,
                                                                const SchemaLoadOptions& options) {
  std::error_code error;
  const std::filesystem::path absolute_path = std::filesystem::absolute(path, error);
  if (error) {
    return io_error("Unable to resolve protocol file path: " + path);
  }
  if (!options.resolve_imports) {
    std::ifstream input(absolute_path);
    if (!input.is_open()) {
      return io_error("Unable to open protocol file: " + absolute_path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();

    const std::string extension = to_lower(absolute_path.extension().string());
    if (extension == ".upr") {
      return load_protocol_definition_impl(buffer.str(), kFormatUpr, true);
    }
    if (extension == ".yaml" || extension == ".yml") {
      return load_protocol_definition_impl(buffer.str(), kFormatYaml, true);
    }
    return load_protocol_definition_impl(buffer.str(), {}, true);
  }

  const auto workspace_root = find_workspace_root(absolute_path);
  SchemaImportResolver resolver(workspace_root.ok() ? std::optional<std::filesystem::path>(workspace_root.value())
                                                    : std::nullopt);
  return resolver.resolve(absolute_path);
}

}  // namespace universal_protocol_runtime
