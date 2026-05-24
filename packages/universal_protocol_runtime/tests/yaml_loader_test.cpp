#include "universal_protocol_runtime/pdl/yaml_loader.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace upr = universal_protocol_runtime;

namespace {

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

struct ValidYamlCase {
  std::string name;
  std::string yaml;
  std::string protocol_name;
  size_t expected_struct_count = 0;
  std::string message_name;
  std::string field_name;
  upr::FieldKind expected_kind = upr::FieldKind::kUnsigned;
  uint8_t expected_width = 0;
  upr::ByteOrder expected_byte_order = upr::ByteOrder::kLittleEndian;
  upr::StringEncoding expected_string_encoding = upr::StringEncoding::kAscii;
  size_t expected_fixed_size = 0;
  std::string expected_size_from;
  std::string expected_referenced_type;
  bool expected_has_expect = false;
  uint64_t expected_expect = 0;
  size_t expected_enum_count = 0;
  size_t expected_bit_field_count = 0;
  bool expected_has_checksum = false;
  std::string expected_checksum_algorithm;
  bool expected_allow_trailing = false;
};

class ValidYamlLoaderTest : public ::testing::TestWithParam<ValidYamlCase> {
 public:
  ~ValidYamlLoaderTest() noexcept override = default;
};

TEST_P(ValidYamlLoaderTest, ParsesSupportedFieldDefinitions) {
  const ValidYamlCase& param = GetParam();

  upr::StatusOr<upr::ProtocolDefinition> definition = upr::load_protocol_definition_from_yaml(param.yaml);

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().name, param.protocol_name);
  EXPECT_EQ(definition.value().structs.size(), param.expected_struct_count);
  ASSERT_EQ(definition.value().messages.size(), 1U);
  const upr::MessageDefinition& message = definition.value().messages.front();
  EXPECT_EQ(message.name, param.message_name);
  EXPECT_EQ(message.allow_trailing_bytes, param.expected_allow_trailing);
  ASSERT_EQ(message.fields.size(), 1U);
  const upr::FieldDefinition& field = message.fields.front();
  EXPECT_EQ(field.name, param.field_name);
  EXPECT_EQ(field.kind, param.expected_kind);
  EXPECT_EQ(field.width_bytes, param.expected_width);
  EXPECT_EQ(field.byte_order, param.expected_byte_order);
  EXPECT_EQ(field.string_encoding, param.expected_string_encoding);
  EXPECT_EQ(field.fixed_size, param.expected_fixed_size);
  EXPECT_EQ(field.size_from_field, param.expected_size_from);
  EXPECT_EQ(field.referenced_type, param.expected_referenced_type);
  EXPECT_EQ(field.has_expected_unsigned, param.expected_has_expect);
  EXPECT_EQ(field.expected_unsigned, param.expected_expect);
  EXPECT_EQ(field.enum_values.size(), param.expected_enum_count);
  EXPECT_EQ(field.bit_fields.size(), param.expected_bit_field_count);
  EXPECT_EQ(field.checksum.has_value(), param.expected_has_checksum);
  if (field.checksum.has_value()) {
    EXPECT_EQ(field.checksum->algorithm, param.expected_checksum_algorithm);
  }
}

TEST(YamlLoaderTest, ParsesTopLevelEnums) {
  const auto definition = upr::load_protocol_definition_from_yaml(R"yaml(
protocol: market_data
enums:
  - name: Side
    underlying: uint8
    values:
      1: Buy
      2: Sell
messages:
  - name: Order
    fields:
      - name: side
        type: Side
)yaml");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().enums.size(), 1U);
  EXPECT_EQ(definition.value().enums[0].name, "Side");
  EXPECT_EQ(definition.value().enums[0].width_bytes, 1);
  ASSERT_EQ(definition.value().messages.size(), 1U);
  ASSERT_EQ(definition.value().messages[0].fields.size(), 1U);
  EXPECT_EQ(definition.value().messages[0].fields[0].referenced_type, "Side");
}

TEST(YamlLoaderTest, ParsesCollectionsVariantsPresenceAndConditions) {
  const auto definition = upr::load_protocol_definition_from_yaml(R"yaml(
protocol: market_data
structs:
  - name: Level
    fields:
      - name: price
        type: uint16
      - name: qty
        type: uint16
  - name: QuoteDetail
    fields:
      - name: best_bid
        type: uint16
      - name: best_ask
        type: uint16
  - name: TradeDetail
    fields:
      - name: trade_id
        type: uint32
messages:
  - name: Snapshot
    fields:
      - name: kind
        type: uint8
      - name: presence
        type: uint8
      - name: level_count
        type: uint8
      - name: levels
        type: Level
        count_from: level_count
      - name: detail
        type: variant
        tag_from: kind
        cases:
          1: QuoteDetail
          2: TradeDetail
      - name: note_len
        type: uint8
        present:
          field: presence
          bit: 0
      - name: note
        type: utf8
        size_from: note_len
        present:
          field: presence
          bit: 0
      - name: revision
        type: uint8
        if:
          field: kind
          equals: 2
)yaml");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  const upr::MessageDefinition& snapshot = definition.value().messages.front();
  ASSERT_EQ(snapshot.fields.size(), 8U);
  EXPECT_EQ(snapshot.fields[3].kind, upr::FieldKind::kCollection);
  EXPECT_EQ(snapshot.fields[3].count_from_field, "level_count");
  EXPECT_EQ(snapshot.fields[4].kind, upr::FieldKind::kVariant);
  EXPECT_EQ(snapshot.fields[4].tag_from_field, "kind");
  ASSERT_EQ(snapshot.fields[4].variant_cases.size(), 2U);
  ASSERT_TRUE(snapshot.fields[5].presence.has_value());
  EXPECT_EQ(snapshot.fields[5].presence->field, "presence");
  ASSERT_TRUE(snapshot.fields[7].condition.has_value());
  EXPECT_EQ(snapshot.fields[7].condition->equals_unsigned, 2U);
}

TEST(YamlLoaderTest, ParsesFixedCountCollections) {
  const auto definition = upr::load_protocol_definition_from_yaml(R"yaml(
protocol: sensors
structs:
  - name: Reading
    fields:
      - name: value
        type: uint16
messages:
  - name: Packet
    fields:
      - name: readings
        type: Reading
        count: 3
)yaml");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().messages.size(), 1U);
  ASSERT_EQ(definition.value().messages[0].fields.size(), 1U);
  const upr::FieldDefinition& field = definition.value().messages[0].fields[0];
  EXPECT_EQ(field.kind, upr::FieldKind::kCollection);
  EXPECT_EQ(field.fixed_count, 3U);
  EXPECT_TRUE(field.count_from_field.empty());
}

TEST(YamlLoaderTest, ParsesTopLevelEnumByteOrderSuffixAndOverride) {
  const auto definition = upr::load_protocol_definition_from_yaml(R"yaml(
protocol: market_data
enums:
  - name: Sequence
    underlying: uint16_be
    endianness: little
    values:
      1: One
messages:
  - name: Packet
    fields:
      - name: sequence
        type: Sequence
)yaml");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().enums.size(), 1U);
  EXPECT_EQ(definition.value().enums[0].byte_order, upr::ByteOrder::kLittleEndian);
}

TEST(YamlLoaderTest, RejectsInvalidAdvancedFieldDeclarations) {
  struct Case {
    const char* yaml;
    const char* expected_message_substring;
  };

  const std::vector<Case> cases = {
      {
          .yaml = R"yaml(
protocol: broken
messages:
  - name: Packet
    fields:
      - name: maybe_revision
        type: uint8
        if:
          field: kind
)yaml",
          .expected_message_substring = "Conditional fields require 'field' and 'equals'",
      },
      {
          .yaml = R"yaml(
protocol: broken
messages:
  - name: Packet
    fields:
      - name: maybe_note
        type: uint8
        present:
          field: flags
)yaml",
          .expected_message_substring = "Presence-gated fields require 'field' and 'bit'",
      },
      {
          .yaml = R"yaml(
protocol: broken
messages:
  - name: Packet
    fields:
      - name: detail
        type: variant
        cases:
          1: QuoteDetail
)yaml",
          .expected_message_substring = "Variant fields require 'tag_from'",
      },
      {
          .yaml = R"yaml(
protocol: broken
messages:
  - name: Packet
    fields:
      - name: detail
        type: variant
        tag_from: kind
        cases:
          - QuoteDetail
)yaml",
          .expected_message_substring = "Variant cases require a mapping",
      },
  };

  for (const Case& test_case : cases) {
    const auto definition = upr::load_protocol_definition_from_yaml(test_case.yaml);
    ASSERT_FALSE(definition.ok());
    EXPECT_NE(std::string(definition.status().message()).find(test_case.expected_message_substring), std::string::npos);
  }
}

TEST(YamlLoaderTest, RejectsInvalidValidationOperatorAndLayoutValidationShapes) {
  {
    const auto definition = upr::load_protocol_definition_from_yaml(R"yaml(
protocol: broken
messages:
  - name: Packet
    fields:
      - name: left
        type: uint8
    validations:
      field: left
)yaml");
    ASSERT_FALSE(definition.ok());
    EXPECT_NE(std::string(definition.status().message()).find("Layout validations must be declared as a sequence"),
              std::string::npos);
  }

  {
    const auto definition = upr::load_protocol_definition_from_yaml(R"yaml(
protocol: broken
messages:
  - name: Packet
    fields:
      - name: left
        type: uint8
    validations:
      - field: left
        op: approx
        value: 1
)yaml");
    ASSERT_FALSE(definition.ok());
    EXPECT_NE(std::string(definition.status().message()).find("Unsupported validation operator: approx"),
              std::string::npos);
  }

  {
    const auto definition = upr::load_protocol_definition_from_yaml(R"yaml(
protocol: broken
messages:
  - name: Packet
    fields:
      - name: left
        type: uint8
    validations:
      - field: left
        op: ge
)yaml");
    ASSERT_FALSE(definition.ok());
    EXPECT_NE(std::string(definition.status().message()).find("either 'other_field' or 'value'"), std::string::npos);
  }
}

INSTANTIATE_TEST_SUITE_P(Coverage,
                         ValidYamlLoaderTest,
                         ::testing::Values(
                             ValidYamlCase{
                                 .name = "unsigned_big_endian",
                                 .yaml = R"yaml(
protocol: telemetry
messages:
  - name: Packet
    fields:
      - name: length
        type: uint16_be
)yaml",
                                 .protocol_name = "telemetry",
                                 .message_name = "Packet",
                                 .field_name = "length",
                                 .expected_kind = upr::FieldKind::kUnsigned,
                                 .expected_width = 2,
                                 .expected_byte_order = upr::ByteOrder::kBigEndian,
                             },
                             ValidYamlCase{
                                 .name = "name_fallback_and_enum_width",
                                 .yaml = R"yaml(
name: control
messages:
  - name: Status
    allow_trailing_bytes: true
    fields:
      - name: state
        type: enum
        width: 1
        values:
          1: Ready
          2: Fault
)yaml",
                                 .protocol_name = "control",
                                 .message_name = "Status",
                                 .field_name = "state",
                                 .expected_kind = upr::FieldKind::kEnum,
                                 .expected_width = 1,
                                 .expected_enum_count = 2,
                                 .expected_allow_trailing = true,
                             },
                             ValidYamlCase{
                                 .name = "dynamic_bytes",
                                 .yaml = R"yaml(
protocol: capture
messages:
  - name: Blob
    fields:
      - name: payload
        type: bytes
        size_from: length
)yaml",
                                 .protocol_name = "capture",
                                 .message_name = "Blob",
                                 .field_name = "payload",
                                 .expected_kind = upr::FieldKind::kBytes,
                                 .expected_size_from = "length",
                             },
                             ValidYamlCase{
                                 .name = "string_with_utf8_encoding",
                                 .yaml = R"yaml(
protocol: text
messages:
  - name: Packet
    fields:
      - name: symbol
        type: string
        encoding: utf8
        size: 8
)yaml",
                                 .protocol_name = "text",
                                 .message_name = "Packet",
                                 .field_name = "symbol",
                                 .expected_kind = upr::FieldKind::kString,
                                 .expected_string_encoding = upr::StringEncoding::kUtf8,
                                 .expected_fixed_size = 8,
                             },
                             ValidYamlCase{
                                 .name = "string_with_ascii_encoding_and_expect",
                                 .yaml = R"yaml(
protocol: ascii_text
messages:
  - name: Packet
    fields:
      - name: symbol
        type: string
        encoding: ascii
        size: 4
        expect: 7
)yaml",
                                 .protocol_name = "ascii_text",
                                 .message_name = "Packet",
                                 .field_name = "symbol",
                                 .expected_kind = upr::FieldKind::kString,
                                 .expected_string_encoding = upr::StringEncoding::kAscii,
                                 .expected_fixed_size = 4,
                                 .expected_has_expect = true,
                                 .expected_expect = 7,
                             },
                             ValidYamlCase{
                                 .name = "custom_struct_reference",
                                 .yaml = R"yaml(
protocol: nested
structs:
  - name: Order
    fields:
      - name: price
        type: uint32
messages:
  - name: Orders
    fields:
      - name: order
        type: Order
)yaml",
                                 .protocol_name = "nested",
                                 .expected_struct_count = 1,
                                 .message_name = "Orders",
                                 .field_name = "order",
                                 .expected_kind = upr::FieldKind::kStruct,
                                 .expected_referenced_type = "Order",
                             },
                             ValidYamlCase{
                                 .name = "bitfields_with_signed_enum_values",
                                 .yaml = R"yaml(
protocol: signed_bits
messages:
  - name: Packet
    fields:
      - name: header
        type: uint8
        bits:
          - name: delta
            offset: 4
            width: 4
            signed: true
            values:
              15: NegativeOne
)yaml",
                                 .protocol_name = "signed_bits",
                                 .message_name = "Packet",
                                 .field_name = "header",
                                 .expected_kind = upr::FieldKind::kUnsigned,
                                 .expected_width = 1,
                                 .expected_bit_field_count = 1,
                             },
                             ValidYamlCase{
                                 .name = "bitfields_and_checksum",
                                 .yaml = R"yaml(
protocol: framed
messages:
  - name: Packet
    fields:
      - name: header
        type: uint16_be
        bits:
          - name: version
            offset: 13
            width: 3
          - name: kind
            offset: 0
            width: 12
        checksum:
          algorithm: crc16_ccitt
          from: frame_start
          to: before_self
)yaml",
                                 .protocol_name = "framed",
                                 .message_name = "Packet",
                                 .field_name = "header",
                                 .expected_kind = upr::FieldKind::kUnsigned,
                                 .expected_width = 2,
                                 .expected_byte_order = upr::ByteOrder::kBigEndian,
                                 .expected_bit_field_count = 2,
                                 .expected_has_checksum = true,
                                 .expected_checksum_algorithm = "crc16_ccitt",
                             },
                             ValidYamlCase{
                                 .name = "explicit_little_endian_override",
                                 .yaml = R"yaml(
protocol: endian
messages:
  - name: Packet
    fields:
      - name: count
        type: uint16_be
        endianness: little
)yaml",
                                 .protocol_name = "endian",
                                 .message_name = "Packet",
                                 .field_name = "count",
                                 .expected_kind = upr::FieldKind::kUnsigned,
                                 .expected_width = 2,
                                 .expected_byte_order = upr::ByteOrder::kLittleEndian,
                             },
                             ValidYamlCase{
                                 .name = "enum_underlying_big_endian",
                                 .yaml = R"yaml(
protocol: control
messages:
  - name: Status
    fields:
      - name: state
        type: enum
        underlying: uint16_be
        values:
          1: Ready
)yaml",
                                 .protocol_name = "control",
                                 .message_name = "Status",
                                 .field_name = "state",
                                 .expected_kind = upr::FieldKind::kEnum,
                                 .expected_width = 2,
                                 .expected_byte_order = upr::ByteOrder::kBigEndian,
                                 .expected_enum_count = 1,
                             },
                             ValidYamlCase{
                                 .name = "signed_integer_type",
                                 .yaml = R"yaml(
protocol: signed_values
messages:
  - name: Sample
    fields:
      - name: delta
        type: int32
)yaml",
                                 .protocol_name = "signed_values",
                                 .message_name = "Sample",
                                 .field_name = "delta",
                                 .expected_kind = upr::FieldKind::kSigned,
                                 .expected_width = 4,
                             },
                             ValidYamlCase{
                                 .name = "float32_type",
                                 .yaml = R"yaml(
protocol: floats
messages:
  - name: Sample
    fields:
      - name: score
        type: float32
)yaml",
                                 .protocol_name = "floats",
                                 .message_name = "Sample",
                                 .field_name = "score",
                                 .expected_kind = upr::FieldKind::kFloat32,
                                 .expected_width = 4,
                             },
                             ValidYamlCase{
                                 .name = "float64_with_endianness_override",
                                 .yaml = R"yaml(
protocol: analytics
messages:
  - name: Sample
    fields:
      - name: score
        type: float64
        endianness: big
)yaml",
                                 .protocol_name = "analytics",
                                 .message_name = "Sample",
                                 .field_name = "score",
                                 .expected_kind = upr::FieldKind::kFloat64,
                                 .expected_width = 8,
                                 .expected_byte_order = upr::ByteOrder::kBigEndian,
                             }),
                         [](const ::testing::TestParamInfo<ValidYamlCase>& info) { return info.param.name; });

struct InvalidYamlCase {
  std::string name;
  std::string yaml;
  upr::StatusCode expected_code = upr::StatusCode::kInvalidArgument;
  std::string expected_message_substring;
};

class InvalidYamlLoaderTest : public ::testing::TestWithParam<InvalidYamlCase> {
 public:
  ~InvalidYamlLoaderTest() noexcept override = default;
};

TEST_P(InvalidYamlLoaderTest, RejectsInvalidDefinitions) {
  const InvalidYamlCase& param = GetParam();

  upr::StatusOr<upr::ProtocolDefinition> definition = upr::load_protocol_definition_from_yaml(param.yaml);

  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(definition.status().code(), param.expected_code);
  EXPECT_NE(std::string(definition.status().message()).find(param.expected_message_substring), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(Coverage,
                         InvalidYamlLoaderTest,
                         ::testing::Values(
                             InvalidYamlCase{
                                 .name = "missing_protocol_name",
                                 .yaml = R"yaml(messages: [])yaml",
                                 .expected_message_substring = "requires 'protocol' or 'name'",
                             },
                             InvalidYamlCase{
                                 .name = "missing_messages",
                                 .yaml = R"yaml(protocol: sample)yaml",
                                 .expected_message_substring = "requires a 'messages' sequence",
                             },
                             InvalidYamlCase{
                                 .name = "missing_message_name",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Every message requires 'name'",
                             },
                             InvalidYamlCase{
                                 .name = "missing_fields",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
)yaml",
                                 .expected_message_substring = "requires a 'fields' sequence",
                             },
                             InvalidYamlCase{
                                 .name = "missing_field_type",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: value
)yaml",
                                 .expected_message_substring = "requires 'name' and 'type'",
                             },
                             InvalidYamlCase{
                                 .name = "enum_underlying_must_be_unsigned",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: state
        type: enum
        underlying: int8
        values:
          1: Ready
)yaml",
                                 .expected_message_substring = "Enum underlying type must be an unsigned scalar",
                             },
                             InvalidYamlCase{
                                 .name = "missing_unsigned_width",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: value
        type: uint
)yaml",
                                 .expected_message_substring = "Missing width for scalar type",
                             },
                             InvalidYamlCase{
                                 .name = "invalid_unsigned_width_token",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: value
        type: uintx
)yaml",
                                 .expected_message_substring = "Invalid scalar width token",
                             },
                             InvalidYamlCase{
                                 .name = "unsupported_unsigned_width_token",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: value
        type: uint7
)yaml",
                                 .expected_message_substring = "Unsupported scalar width token",
                             },
                             InvalidYamlCase{
                                 .name = "invalid_signed_width_token",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: delta
        type: intx
)yaml",
                                 .expected_message_substring = "Invalid scalar width token",
                             },
                             InvalidYamlCase{
                                 .name = "enum_requires_width_or_underlying",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: state
        type: enum
        values:
          1: Ready
)yaml",
                                 .expected_message_substring = "Enum fields require 'underlying' or 'width'",
                             },
                             InvalidYamlCase{
                                 .name = "enum_requires_values_mapping",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: state
        type: enum
        width: 1
)yaml",
                                 .expected_message_substring = "Enum fields require a 'values' mapping",
                             },
                             InvalidYamlCase{
                                 .name = "enum_underlying_invalid_width_token",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: state
        type: enum
        underlying: uintx
        values:
          1: Ready
)yaml",
                                 .expected_message_substring = "Invalid scalar width token",
                             },
                             InvalidYamlCase{
                                 .name = "unsupported_endianness",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: value
        type: uint16
        endianness: middle
)yaml",
                                 .expected_message_substring = "Unsupported endianness",
                             },
                             InvalidYamlCase{
                                 .name = "unsupported_string_encoding",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: text
        type: string
        encoding: utf16
        size: 2
)yaml",
                                 .expected_message_substring = "Unsupported string encoding",
                             },
                             InvalidYamlCase{
                                 .name = "bitfields_must_be_sequence",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: header
        type: uint8
        bits: {}
)yaml",
                                 .expected_message_substring = "Bitfields must be declared as a sequence",
                             },
                             InvalidYamlCase{
                                 .name = "bitfield_requires_name_offset_width",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: header
        type: uint8
        bits:
          - offset: 0
            width: 1
)yaml",
                                 .expected_message_substring = "Each bitfield requires 'name', 'offset', and 'width'",
                             },
                             InvalidYamlCase{
                                 .name = "bitfield_enum_values_require_mapping",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: header
        type: uint8
        bits:
          - name: mode
            offset: 0
            width: 2
            values: []
)yaml",
                                 .expected_message_substring = "Enum-style values require a mapping",
                             },
                             InvalidYamlCase{
                                 .name = "checksum_requires_algorithm",
                                 .yaml = R"yaml(
protocol: sample
messages:
  - name: Packet
    fields:
      - name: crc
        type: uint8
        checksum: {}
)yaml",
                                 .expected_message_substring = "require an 'algorithm'",
                             },
                             InvalidYamlCase{
                                 .name = "structs_must_be_sequence",
                                 .yaml = R"yaml(
protocol: sample
structs: {}
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Protocol YAML field 'structs' must be a sequence",
                             },
                             InvalidYamlCase{
                                 .name = "struct_requires_fields_sequence",
                                 .yaml = R"yaml(
protocol: sample
structs:
  - name: Header
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Every struct requires a 'fields' sequence",
                             },
                             InvalidYamlCase{
                                 .name = "imports_must_be_sequence",
                                 .yaml = R"yaml(
protocol: sample
imports: {}
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Protocol YAML field 'imports' must be a sequence",
                             },
                             InvalidYamlCase{
                                 .name = "enums_must_be_sequence",
                                 .yaml = R"yaml(
protocol: sample
enums: {}
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Protocol YAML field 'enums' must be a sequence",
                             },
                             InvalidYamlCase{
                                 .name = "enum_requires_name",
                                 .yaml = R"yaml(
protocol: sample
enums:
  - underlying: uint8
    values:
      1: Ready
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Every enum requires 'name'",
                             },
                             InvalidYamlCase{
                                 .name = "enum_declaration_requires_unsigned_underlying",
                                 .yaml = R"yaml(
protocol: sample
enums:
  - name: Side
    underlying: int8
    values:
      1: Ready
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Enum underlying type must be an unsigned scalar",
                             },
                             InvalidYamlCase{
                                 .name = "enum_declaration_invalid_underlying_width",
                                 .yaml = R"yaml(
protocol: sample
enums:
  - name: Side
    underlying: uintx
    values:
      1: Ready
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Invalid scalar width token",
                             },
                             InvalidYamlCase{
                                 .name = "enum_declaration_requires_width_or_underlying",
                                 .yaml = R"yaml(
protocol: sample
enums:
  - name: Side
    values:
      1: Ready
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Enum declarations require 'underlying' or 'width'",
                             },
                             InvalidYamlCase{
                                 .name = "enum_declaration_invalid_endianness",
                                 .yaml = R"yaml(
protocol: sample
enums:
  - name: Side
    width: 1
    endianness: middle
    values:
      1: Ready
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Unsupported endianness",
                             },
                             InvalidYamlCase{
                                 .name = "enum_declaration_requires_values_mapping",
                                 .yaml = R"yaml(
protocol: sample
enums:
  - name: Side
    width: 1
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                 .expected_message_substring = "Enum declarations require a 'values' mapping",
                             }),
                         [](const ::testing::TestParamInfo<InvalidYamlCase>& info) { return info.param.name; });

TEST(YamlLoaderTest, LoadsDefinitionFromFile) {
  const std::string path = "/tmp/upr_yaml_loader_test.yaml";
  {
    std::ofstream output(path);
    output << R"yaml(
protocol: file_protocol
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml";
  }

  upr::StatusOr<upr::ProtocolDefinition> definition = upr::load_protocol_definition_from_file(path);

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  EXPECT_EQ(definition.value().name, "file_protocol");
  std::remove(path.c_str());
}

TEST(YamlLoaderTest, LoadsYmlDefinitionWhenImportResolutionDisabled) {
  const std::string path = "/tmp/upr_yaml_loader_test.yml";
  {
    std::ofstream output(path);
    output << R"yaml(
protocol: yml_protocol
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml";
  }

  upr::SchemaLoadOptions options;
  options.resolve_imports = false;
  upr::StatusOr<upr::ProtocolDefinition> definition = upr::load_protocol_definition_from_file(path, options);

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  EXPECT_EQ(definition.value().name, "yml_protocol");
  std::remove(path.c_str());
}

TEST(YamlLoaderTest, ResolvesImportedYamlSchemasFromFile) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_yaml_loader_imports";
  const std::filesystem::path types_dir = root_dir / "types";
  const std::filesystem::path orders_dir = root_dir / "orders";
  std::filesystem::create_directories(types_dir);
  std::filesystem::create_directories(orders_dir);

  {
    std::ofstream output(types_dir / "shared.yaml");
    ASSERT_TRUE(output.is_open());
    output << R"yaml(
enums:
  - name: Side
    underlying: uint8
    values:
      1: Buy
      2: Sell
)yaml";
  }

  {
    std::ofstream output(orders_dir / "market_data.yaml");
    ASSERT_TRUE(output.is_open());
    output << R"yaml(
protocol: market_data
imports:
  - ../types/shared.yaml
messages:
  - name: Order
    fields:
      - name: message_type
        type: uint8
        expect: 1
      - name: side
        type: Side
)yaml";
  }

  upr::StatusOr<upr::ProtocolDefinition> definition =
      upr::load_protocol_definition_from_file((orders_dir / "market_data.yaml").string());

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  EXPECT_TRUE(definition.value().imports.empty());
  ASSERT_EQ(definition.value().enums.size(), 1U);
  ASSERT_EQ(definition.value().messages.size(), 1U);

  std::filesystem::remove_all(root_dir);
}

TEST(YamlLoaderTest, ReturnsIoErrorWhenFileCannotBeOpened) {
  upr::StatusOr<upr::ProtocolDefinition> definition =
      upr::load_protocol_definition_from_file("/tmp/definitely_missing_protocol.yaml");

  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(definition.status().code(), upr::StatusCode::kIoError);
}

TEST(YamlLoaderTest, ReturnsSchemaErrorForMalformedYaml) {
  upr::StatusOr<upr::ProtocolDefinition> definition =
      upr::load_protocol_definition_from_yaml("protocol: bad\nmessages: [");

  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(definition.status().code(), upr::StatusCode::kSchemaError);
}

TEST(YamlLoaderTest, ParsesReservedAlignedFieldsAndValidations) {
  const auto definition = upr::load_protocol_definition_from_yaml(R"yaml(
protocol: hardware
messages:
  - name: Packet
    fields:
      - name: version
        type: uint8
      - name: pad
        type: reserved
        size: 3
        align: 4
        reserved_fill: 170
      - name: payload_len
        type: uint8
      - name: item_count
        type: uint8
    validations:
      - field: payload_len
        op: eq
        other_field: item_count
        multiplier: 4
        if:
          field: version
          equals: 2
)yaml");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  const auto& message = definition.value().messages.front();
  ASSERT_EQ(message.fields.size(), 4U);
  EXPECT_EQ(message.fields[1].kind, upr::FieldKind::kBytes);
  EXPECT_TRUE(message.fields[1].is_reserved);
  EXPECT_EQ(message.fields[1].alignment, 4U);
  EXPECT_EQ(message.fields[1].reserved_fill_byte, 170U);
  ASSERT_EQ(message.validations.size(), 1U);
  EXPECT_EQ(message.validations[0].field, "payload_len");
  EXPECT_TRUE(message.validations[0].compare_to_field);
  EXPECT_EQ(message.validations[0].other_field, "item_count");
  EXPECT_EQ(message.validations[0].multiplier, 4U);
  ASSERT_TRUE(message.validations[0].when.has_value());
  EXPECT_EQ(message.validations[0].when->field, "version");
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

}  // namespace
