#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "universal_protocol_runtime/compiler/schema_compiler.hpp"
#include "universal_protocol_runtime/pdl/yaml_loader.hpp"

namespace upr = universal_protocol_runtime;

namespace {

TEST(UprLoaderTest, ParsesCompactSchema) {
  const auto definition = upr::load_protocol_definition_from_upr(R"upr(
protocol market_data

enum Side: uint8 { 1 = Buy, 2 = Sell }

struct Header {
  flags: uint16_be {
    version @ 13:3
    urgent @ 12:1
  }
}

message Order {
  message_type: uint8 = 1
  header: Header
  symbol: ascii[4]
  side: Side
  payload_length: uint16
  payload: bytes[payload_length]
  checksum: uint8 checksum(xor8)
}
)upr");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().name, "market_data");
  ASSERT_EQ(definition.value().enums.size(), 1U);
  ASSERT_EQ(definition.value().structs.size(), 1U);
  ASSERT_EQ(definition.value().messages.size(), 1U);
  const upr::FieldDefinition& symbol = definition.value().messages.front().fields[2];
  EXPECT_EQ(symbol.kind, upr::FieldKind::kString);
  EXPECT_EQ(symbol.string_encoding, upr::StringEncoding::kAscii);
  EXPECT_EQ(symbol.fixed_size, 4U);
  const upr::FieldDefinition& side = definition.value().messages.front().fields[3];
  EXPECT_EQ(side.kind, upr::FieldKind::kStruct);
  EXPECT_EQ(side.referenced_type, "Side");
  const upr::FieldDefinition& checksum = definition.value().messages.front().fields.back();
  ASSERT_TRUE(checksum.checksum.has_value());
  EXPECT_EQ(checksum.checksum->algorithm, "xor8");
}

TEST(UprLoaderTest, CompilesNamedEnums) {
  const auto definition = upr::load_protocol_definition_from_upr(R"upr(
protocol named_enums

enum Side: uint8 { 1 = Buy, 2 = Sell }

message Order {
  side: Side
}
)upr");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  const auto compiled = upr::compile_protocol(definition.value());
  ASSERT_TRUE(compiled.ok()) << compiled.status().message();
  const upr::CompiledMessage* order = compiled.value().find_message("Order");
  ASSERT_NE(order, nullptr);
  ASSERT_EQ(order->fields().size(), 1U);
  EXPECT_EQ(order->fields()[0].kind, upr::FieldKind::kEnum);
  ASSERT_EQ(order->fields()[0].enum_values.size(), 2U);
  EXPECT_EQ(order->fields()[0].enum_values[0].name, "Buy");
}

struct InvalidUprCase {
  std::string name;
  std::string upr;
  std::string expected_message_substring;
};

class InvalidUprLoaderParameterizedTest : public ::testing::TestWithParam<InvalidUprCase> {};

TEST_P(InvalidUprLoaderParameterizedTest, RejectsInvalidUprBranches) {
  const InvalidUprCase& param = GetParam();
  const auto definition = upr::load_protocol_definition_from_upr(param.upr);
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(definition.status().code(), upr::StatusCode::kInvalidArgument);
  EXPECT_NE(std::string(definition.status().message()).find(param.expected_message_substring), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(
    Coverage,
    InvalidUprLoaderParameterizedTest,
    ::testing::Values(
        InvalidUprCase{
            .name = "duplicate_protocol",
            .upr = R"upr(
protocol one
protocol two
message Packet { id: uint8 }
)upr",
            .expected_message_substring = "Protocol name already declared",
        },
        InvalidUprCase{
            .name = "unknown_top_level_token",
            .upr = R"upr(
protocol bad
unknown thing
message Packet { id: uint8 }
)upr",
            .expected_message_substring = "Expected 'protocol', 'import', 'enum', 'struct', or 'message'",
        },
        InvalidUprCase{
            .name = "invalid_hex_literal",
            .upr = R"upr(
protocol bad
message Packet { id: uint8 = 0x }
)upr",
            .expected_message_substring = "Invalid hexadecimal literal",
        },
        InvalidUprCase{
            .name = "unterminated_string",
            .upr = R"upr(
protocol bad
import "unterminated
message Packet { id: uint8 }
)upr",
            .expected_message_substring = "Unterminated string literal",
        },
        InvalidUprCase{
            .name = "unsupported_escape",
            .upr = R"upr(
protocol bad
import "bad\q"
message Packet { id: uint8 }
)upr",
            .expected_message_substring = "Unsupported escape sequence",
        },
        InvalidUprCase{
            .name = "enum_underlying_must_be_unsigned",
            .upr = R"upr(
protocol bad
enum Side: int8 { 1 = Buy }
message Packet { id: uint8 }
)upr",
            .expected_message_substring = "Enum underlying type must be an unsigned scalar",
        },
        InvalidUprCase{
            .name = "enum_missing_colon",
            .upr = R"upr(
protocol bad
enum Side uint8 { 1 = Buy }
message Packet { id: uint8 }
)upr",
            .expected_message_substring = "Expected ':' after enum name",
        },
        InvalidUprCase{
            .name = "protocol_name_parse_failure",
            .upr = R"upr(
    protocol {
    message Packet { id: uint8 }
    )upr",
            .expected_message_substring = "Expected protocol name",
        },
        InvalidUprCase{
            .name = "protocol_missing_name",
            .upr = R"upr(
    protocol
    message Packet { id: uint8 }
    )upr",
            .expected_message_substring = "UPR",
        },
        InvalidUprCase{
            .name = "import_missing_path",
            .upr = R"upr(
    protocol bad
    import
    message Packet { id: uint8 }
    )upr",
            .expected_message_substring = "UPR",
        },
        InvalidUprCase{
            .name = "import_path_parse_failure",
            .upr = R"upr(
    protocol bad
    import {
    message Packet { id: uint8 }
    )upr",
            .expected_message_substring = "Expected import path",
        },
        InvalidUprCase{
            .name = "enum_missing_name",
            .upr = R"upr(
    protocol bad
    enum : uint8 { 1 = Buy }
    message Packet { id: uint8 }
    )upr",
            .expected_message_substring = "Expected enum name",
        },
        InvalidUprCase{
            .name = "struct_missing_name",
            .upr = R"upr(
    protocol bad
    struct { id: uint8 }
    message Packet { id: uint8 }
    )upr",
            .expected_message_substring = "Expected struct name",
        },
        InvalidUprCase{
            .name = "message_missing_name",
            .upr = R"upr(
    protocol bad
    message { id: uint8 }
    )upr",
            .expected_message_substring = "Expected message name",
        },
        InvalidUprCase{
            .name = "field_missing_name",
            .upr = R"upr(
    protocol bad
    message Packet { : uint8 }
    )upr",
            .expected_message_substring = "Expected field name",
        },
        InvalidUprCase{
            .name = "enum_field_underlying_not_identifier",
            .upr = R"upr(
    protocol bad
    message Packet { side: enum<1> { 1 = Buy } }
    )upr",
            .expected_message_substring = "Expected enum underlying type",
        },
        InvalidUprCase{
            .name = "enum_label_missing",
            .upr = R"upr(
    protocol bad
    enum Side: uint8 { 1 = }
    message Packet { id: uint8 }
    )upr",
            .expected_message_substring = "Expected enum label",
        },
        InvalidUprCase{
            .name = "bitfield_name_missing",
            .upr = R"upr(
    protocol bad
    message Packet { flags: uint8 { @ 0:2 } }
    )upr",
            .expected_message_substring = "Expected bitfield name",
        },
        InvalidUprCase{
            .name = "bitfield_offset_missing",
            .upr = R"upr(
    protocol bad
    message Packet { flags: uint8 { mode @ :2 } }
    )upr",
            .expected_message_substring = "Expected bitfield offset",
        },
        InvalidUprCase{
            .name = "bitfield_width_missing",
            .upr = R"upr(
    protocol bad
    message Packet { flags: uint8 { mode @ 1: } }
    )upr",
            .expected_message_substring = "Expected bitfield width",
        },
        InvalidUprCase{
            .name = "checksum_algorithm_missing",
            .upr = R"upr(
    protocol bad
    message Packet { crc: uint8 checksum() }
    )upr",
            .expected_message_substring = "Expected checksum algorithm",
        },
        InvalidUprCase{
            .name = "checksum_from_anchor_missing",
            .upr = R"upr(
    protocol bad
    message Packet { crc: uint8 checksum(xor8,) }
    )upr",
            .expected_message_substring = "Expected checksum from anchor",
        },
        InvalidUprCase{
            .name = "checksum_to_anchor_missing",
            .upr = R"upr(
    protocol bad
    message Packet { crc: uint8 checksum(xor8, frame_start,) }
    )upr",
            .expected_message_substring = "Expected checksum to anchor",
        },
        InvalidUprCase{
            .name = "field_missing_type",
            .upr = R"upr(
protocol bad
message Packet { id: }
)upr",
            .expected_message_substring = "Expected a field type",
        },
        InvalidUprCase{
            .name = "struct_field_parse_failure",
            .upr = R"upr(
    protocol bad
    struct Header { : uint8 }
    message Packet { id: uint8 }
    )upr",
            .expected_message_substring = "Expected field name",
        },
        InvalidUprCase{
            .name = "inline_enum_value_missing_label",
            .upr = R"upr(
    protocol bad
    message Packet { side: enum<uint8> { 1 = } }
    )upr",
            .expected_message_substring = "Expected enum label",
        },
        InvalidUprCase{
            .name = "inline_enum_missing_angle_open",
            .upr = R"upr(
protocol bad
message Packet { side: enum uint8> { 1 = Buy } }
)upr",
            .expected_message_substring = "Expected '<' after enum",
        },
        InvalidUprCase{
            .name = "inline_enum_missing_angle_close",
            .upr = R"upr(
protocol bad
message Packet { side: enum<uint8 { 1 = Buy } }
)upr",
            .expected_message_substring = "Expected '>' after enum underlying type",
        },
        InvalidUprCase{
            .name = "size_suffix_invalid_token",
            .upr = R"upr(
protocol bad
message Packet { payload: bytes[:] }
)upr",
            .expected_message_substring = "Expected a fixed size or field reference inside '[...]'",
        },
        InvalidUprCase{
            .name = "size_suffix_missing_closing_bracket",
            .upr = R"upr(
protocol bad
message Packet { payload: bytes[8 }
)upr",
            .expected_message_substring = "Expected ']' after field size",
        },
        InvalidUprCase{
            .name = "enum_body_missing_equals",
            .upr = R"upr(
protocol bad
enum Side: uint8 { 1 Buy }
message Packet { id: uint8 }
)upr",
            .expected_message_substring = "Expected '=' in enum value declaration",
        },
        InvalidUprCase{
            .name = "bitfield_missing_at",
            .upr = R"upr(
protocol bad
message Packet { flags: uint8 { mode 0:2 } }
)upr",
            .expected_message_substring = "Expected '@' in bitfield declaration",
        },
        InvalidUprCase{
            .name = "bitfield_missing_colon",
            .upr = R"upr(
protocol bad
message Packet { flags: uint8 { mode @ 0 2 } }
)upr",
            .expected_message_substring = "Expected ':' between bitfield offset and width",
        },
        InvalidUprCase{
            .name = "bitfield_width_out_of_range",
            .upr = R"upr(
protocol bad
message Packet { flags: uint16 { mode @ 0:300 } }
)upr",
            .expected_message_substring = "Bitfield offset and width must fit in 8 bits",
        },
        InvalidUprCase{
            .name = "checksum_missing_open_paren",
            .upr = R"upr(
protocol bad
message Packet { crc: uint8 checksum xor8) }
)upr",
            .expected_message_substring = "UPR parse error",
        },
        InvalidUprCase{
            .name = "checksum_missing_close_paren",
            .upr = R"upr(
protocol bad
message Packet { crc: uint8 checksum(xor8 }
)upr",
            .expected_message_substring = "Expected ')' after checksum arguments",
        },
        InvalidUprCase{
            .name = "numeric_overflow",
            .upr = R"upr(
protocol bad
message Packet { id: uint8 = 999999999999999999999999999999999999 }
)upr",
            .expected_message_substring = "Invalid numeric literal",
        },
        InvalidUprCase{
            .name = "unsigned_width_invalid_token",
            .upr = R"upr(
    protocol bad
    message Packet { id: uintx }
    )upr",
            .expected_message_substring = "Invalid scalar width token",
        },
        InvalidUprCase{
            .name = "signed_width_invalid_token",
            .upr = R"upr(
    protocol bad
    message Packet { id: intx }
    )upr",
            .expected_message_substring = "Invalid scalar width token",
        }),
    [](const ::testing::TestParamInfo<InvalidUprCase>& info) { return info.param.name; });

TEST(UprLoaderTest, ParsesExtendedUprFeatures) {
  const auto definition = upr::load_protocol_definition_from_upr(R"upr(
protocol extended
import "shared/types.upr"

enum Side: uint16_be { 0x1 = Buy, 0x2 = Sell }

message Packet allow_trailing_bytes {
  id: uint32
  desc: string[len]
  price32: float32_be
  price64: float64_be
  delta: int16_be
  side: enum<uint16_be> { 1 = Buy, 2 = Sell }
  flags: uint16 {
    version @ 8:4 signed
    kind @ 0:4 { 1 = A, 2 = B }
  }
  checksum: uint16 checksum(sum16, frame_start, before_self)
}
)upr");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().imports.size(), 1U);
  ASSERT_EQ(definition.value().messages.size(), 1U);
  const upr::MessageDefinition& message = definition.value().messages.front();
  EXPECT_TRUE(message.allow_trailing_bytes);
  ASSERT_EQ(message.fields.size(), 8U);
  EXPECT_EQ(message.fields[1].size_from_field, "len");
  EXPECT_EQ(message.fields[2].byte_order, upr::ByteOrder::kBigEndian);
  EXPECT_EQ(message.fields[3].byte_order, upr::ByteOrder::kBigEndian);
  EXPECT_EQ(message.fields[4].byte_order, upr::ByteOrder::kBigEndian);
  ASSERT_EQ(message.fields[6].bit_fields.size(), 2U);
  EXPECT_TRUE(message.fields[6].bit_fields[0].is_signed);
  ASSERT_TRUE(message.fields[7].checksum.has_value());
  EXPECT_EQ(message.fields[7].checksum->from, "frame_start");
  EXPECT_EQ(message.fields[7].checksum->to, "before_self");
}

TEST(UprLoaderTest, ParsesEscapedStringAndCommentForms) {
  const auto definition = upr::load_protocol_definition_from_upr(
      "protocol escaped\n"
      "// slash comment\n"
      "# hash comment\n"
      "import \"folder\\\\file\\\"name\\'quoted\\n\\r\\t.upr\"\n"
      "message Packet {\n"
      "  payload: bytes\n"
      "  allow_trailing_bytes\n"
      "}\n");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().imports.size(), 1U);
  EXPECT_NE(definition.value().imports[0].path.find("folder"), std::string::npos);
  ASSERT_EQ(definition.value().messages.size(), 1U);
  EXPECT_TRUE(definition.value().messages[0].allow_trailing_bytes);
  ASSERT_EQ(definition.value().messages[0].fields.size(), 1U);
  EXPECT_EQ(definition.value().messages[0].fields[0].kind, upr::FieldKind::kBytes);
}

TEST(UprLoaderTest, GenericLoaderDetectsYamlWhenFormatHintIsEmpty) {
  const auto definition = upr::load_protocol_definition(R"yaml(
protocol: generic_yaml
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  EXPECT_EQ(definition.value().name, "generic_yaml");
}

TEST(UprLoaderTest, GenericLoaderAcceptsExplicitFormatHints) {
  const auto upr_definition = upr::load_protocol_definition(R"upr(
protocol hinted
message Packet { value: uint8 }
)upr",
                                                            "upr");
  ASSERT_TRUE(upr_definition.ok()) << upr_definition.status().message();

  const auto yaml_definition = upr::load_protocol_definition(R"yaml(
protocol: hinted_yaml
messages:
  - name: Packet
    fields:
      - name: value
        type: uint8
)yaml",
                                                             "yaml");
  ASSERT_TRUE(yaml_definition.ok()) << yaml_definition.status().message();
}

TEST(UprLoaderTest, GenericLoaderDetectsUprWhenFormatHintIsEmpty) {
  const auto definition = upr::load_protocol_definition(R"upr(
protocol autodetect_upr
message Packet {
  id: uint8
}
)upr");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  EXPECT_EQ(definition.value().name, "autodetect_upr");
}

TEST(UprLoaderTest, AutoDetectsUprFiles) {
  const std::string path = "/tmp/upr_loader_test_schema.upr";
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(output.is_open());
  output << R"upr(
protocol auto_detect
message Packet {
  id: uint8 = 7
  body: utf8[8]
}
)upr";
  output.close();

  const auto definition = upr::load_protocol_definition_from_file(path);
  std::remove(path.c_str());

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().messages.front().fields.size(), 2U);
  EXPECT_EQ(definition.value().messages.front().fields[1].string_encoding, upr::StringEncoding::kUtf8);
}

TEST(UprLoaderTest, ResolvesImportedSchemasFromFile) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_imports";
  const std::filesystem::path types_dir = root_dir / "order_type";
  const std::filesystem::path orders_dir = root_dir / "order";
  std::filesystem::create_directories(types_dir);
  std::filesystem::create_directories(orders_dir);

  {
    std::ofstream output(types_dir / "types.upr", std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output << "enum Side: uint8 { 1 = Buy, 2 = Sell }\n";
    output << "enum OrderType: uint8 { 1 = Limit, 2 = Market }\n";
  }
  {
    std::ofstream output(orders_dir / "market_data.upr", std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output << R"upr(
protocol market_data
import "../order_type/types.upr"

message Order {
  message_type: uint8 = 1
  side: Side
  order_type: OrderType
}
)upr";
  }

  const auto definition = upr::load_protocol_definition_from_file((orders_dir / "market_data.upr").string());
  ASSERT_TRUE(definition.ok()) << definition.status().message();
  EXPECT_TRUE(definition.value().imports.empty());
  ASSERT_EQ(definition.value().enums.size(), 2U);
  ASSERT_EQ(definition.value().messages.size(), 1U);

  const auto compiled = upr::compile_protocol(definition.value());
  ASSERT_TRUE(compiled.ok()) << compiled.status().message();
  const upr::CompiledMessage* order = compiled.value().find_message("Order");
  ASSERT_NE(order, nullptr);
  EXPECT_EQ(order->fields()[1].kind, upr::FieldKind::kEnum);

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, SupportsAbsoluteImportPaths) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_absolute_imports";
  const std::filesystem::path types_path = root_dir / "types.upr";
  const std::filesystem::path schema_path = root_dir / "market_data.upr";
  std::filesystem::create_directories(root_dir);

  {
    std::ofstream output(types_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output << "enum Side: uint8 { 1 = Buy, 2 = Sell }\n";
  }
  {
    std::ofstream output(schema_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output << "protocol market_data\n";
    output << "import \"" << types_path.string() << "\"\n";
    output << "message Order { side: Side }\n";
  }

  const auto definition = upr::load_protocol_definition_from_file(schema_path.string());
  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().enums.size(), 1U);

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, ResolvesSharedImportOnlyOnce) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_shared_import";
  const std::filesystem::path common_path = root_dir / "common.upr";
  const std::filesystem::path left_path = root_dir / "left.upr";
  const std::filesystem::path right_path = root_dir / "right.upr";
  const std::filesystem::path root_schema = root_dir / "root.upr";
  std::filesystem::create_directories(root_dir);

  {
    std::ofstream out(common_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "enum Shared: uint8 { 1 = One }\n";
  }
  {
    std::ofstream out(left_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "import \"./common.upr\"\n";
  }
  {
    std::ofstream out(right_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "import \"./common.upr\"\n";
  }
  {
    std::ofstream out(root_schema, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << R"upr(
protocol root
import "./left.upr"
import "./right.upr"
message Packet {
  value: Shared
}
)upr";
  }

  const auto definition = upr::load_protocol_definition_from_file(root_schema.string());
  ASSERT_TRUE(definition.ok()) << definition.status().message();
  EXPECT_EQ(definition.value().enums.size(), 1U);

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, RejectsImportCycles) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_cycle";
  const std::filesystem::path a_path = root_dir / "a.upr";
  const std::filesystem::path b_path = root_dir / "b.upr";
  const std::filesystem::path root_schema = root_dir / "root.upr";
  std::filesystem::create_directories(root_dir);

  {
    std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "import \"./b.upr\"\n";
  }
  {
    std::ofstream out(b_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "import \"./a.upr\"\n";
  }
  {
    std::ofstream out(root_schema, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << R"upr(
protocol root
import "./a.upr"
message Packet {
  id: uint8
}
)upr";
  }

  const auto definition = upr::load_protocol_definition_from_file(root_schema.string());
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(definition.status().code(), upr::StatusCode::kInvalidArgument);
  EXPECT_NE(std::string(definition.status().message()).find("Schema import cycle detected"), std::string::npos);

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, RejectsWorkspaceRelativeImportOutsideWorkspace) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_workspace_import";
  const std::filesystem::path root_schema = root_dir / "root.upr";
  std::filesystem::create_directories(root_dir);

  {
    std::ofstream out(root_schema, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << R"upr(
protocol root
import "examples/order_types.upr"
message Packet {
  id: uint8
}
)upr";
  }

  const auto definition = upr::load_protocol_definition_from_file(root_schema.string());
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(definition.status().code(), upr::StatusCode::kNotFound);

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, RejectsRootSchemaWithoutProtocolNameAfterImportResolution) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_missing_root_protocol";
  const std::filesystem::path root_schema = root_dir / "root.upr";
  std::filesystem::create_directories(root_dir);

  {
    std::ofstream out(root_schema, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "enum Side: uint8 { 1 = Buy }\n";
  }

  const auto definition = upr::load_protocol_definition_from_file(root_schema.string());
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(definition.status().code(), upr::StatusCode::kInvalidArgument);

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, RejectsRootSchemaWithoutMessagesAfterImportResolution) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_missing_root_messages";
  const std::filesystem::path root_schema = root_dir / "root.upr";
  std::filesystem::create_directories(root_dir);

  {
    std::ofstream out(root_schema, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "protocol root_only\n";
  }

  const auto definition = upr::load_protocol_definition_from_file(root_schema.string());
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(definition.status().code(), upr::StatusCode::kInvalidArgument);

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, RejectsMalformedImportedSchema) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_malformed_import";
  const std::filesystem::path bad_import = root_dir / "bad.upr";
  const std::filesystem::path root_schema = root_dir / "root.upr";
  std::filesystem::create_directories(root_dir);

  {
    std::ofstream out(bad_import, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "message Packet { id uint8 }\n";
  }
  {
    std::ofstream out(root_schema, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << R"upr(
protocol root
import "./bad.upr"
message Main {
  id: uint8
}
)upr";
  }

  const auto definition = upr::load_protocol_definition_from_file(root_schema.string());
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(definition.status().code(), upr::StatusCode::kInvalidArgument);

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, LoadsWithImportResolutionDisabled) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_no_resolve";
  const std::filesystem::path root_schema = root_dir / "root.upr";
  std::filesystem::create_directories(root_dir);

  {
    std::ofstream out(root_schema, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << R"upr(
protocol root
import "./missing.upr"
message Main {
  id: uint8
}
)upr";
  }

  upr::SchemaLoadOptions options;
  options.resolve_imports = false;
  const auto definition = upr::load_protocol_definition_from_file(root_schema.string(), options);
  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().imports.size(), 1U);

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, LoadsUnknownExtensionWithAutoDetectWhenImportResolutionDisabled) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_unknown_ext";
  const std::filesystem::path root_schema = root_dir / "root.schema";
  std::filesystem::create_directories(root_dir);

  {
    std::ofstream out(root_schema, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << R"yaml(
protocol: root
messages:
  - name: Main
    fields:
      - name: id
        type: uint8
)yaml";
  }

  upr::SchemaLoadOptions options;
  options.resolve_imports = false;
  const auto definition = upr::load_protocol_definition_from_file(root_schema.string(), options);
  ASSERT_TRUE(definition.ok()) << definition.status().message();
  EXPECT_EQ(definition.value().name, "root");

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, LoadsYmlExtensionWhenImportResolutionDisabled) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_yml_ext";
  const std::filesystem::path root_schema = root_dir / "root.yml";
  std::filesystem::create_directories(root_dir);

  {
    std::ofstream out(root_schema, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << R"yaml(
protocol: yml_root
messages:
  - name: Main
    fields:
      - name: id
        type: uint8
)yaml";
  }

  upr::SchemaLoadOptions options;
  options.resolve_imports = false;
  const auto definition = upr::load_protocol_definition_from_file(root_schema.string(), options);
  ASSERT_TRUE(definition.ok()) << definition.status().message();
  EXPECT_EQ(definition.value().name, "yml_root");

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, ReturnsIoErrorForMissingFileWhenImportResolutionDisabled) {
  upr::SchemaLoadOptions options;
  options.resolve_imports = false;
  const auto definition = upr::load_protocol_definition_from_file("/tmp/upr_missing_for_no_resolve.upr", options);
  ASSERT_FALSE(definition.ok());
  EXPECT_EQ(definition.status().code(), upr::StatusCode::kIoError);
}

TEST(UprLoaderTest, ResolvesWorkspaceRelativeImportsUsingWorkspaceRoot) {
  const std::filesystem::path root_dir = std::filesystem::path("/tmp") / "upr_loader_fake_workspace";
  const std::filesystem::path workspace_root = root_dir / "repo";
  const std::filesystem::path schema_dir = workspace_root / "schemas";
  const std::filesystem::path shared_dir = workspace_root / "shared";
  std::filesystem::create_directories(schema_dir);
  std::filesystem::create_directories(shared_dir);

  {
    std::ofstream out(workspace_root / "WORKSPACE", std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "# fake workspace\n";
  }
  {
    std::ofstream out(shared_dir / "types.upr", std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << "enum Side: uint8 { 1 = Buy, 2 = Sell }\n";
  }
  {
    std::ofstream out(schema_dir / "root.upr", std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << R"upr(
protocol root
import "shared/types.upr"
message Packet {
  side: Side
}
)upr";
  }

  const auto definition = upr::load_protocol_definition_from_file((schema_dir / "root.upr").string());
  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().enums.size(), 1U);

  std::filesystem::remove_all(root_dir);
}

TEST(UprLoaderTest, CompileRejectsUnresolvedImports) {
  const auto definition = upr::load_protocol_definition_from_upr(R"upr(
protocol market_data
import "order_type/types.upr"

message Order {
  message_type: uint8 = 1
}
)upr");

  ASSERT_TRUE(definition.ok()) << definition.status().message();
  ASSERT_EQ(definition.value().imports.size(), 1U);
  const auto compiled = upr::compile_protocol(definition.value());
  ASSERT_FALSE(compiled.ok());
  EXPECT_EQ(compiled.status().code(), upr::StatusCode::kInvalidArgument);
}

}  // namespace
