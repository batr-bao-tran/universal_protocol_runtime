#include "universal_protocol_runtime/compiler/schema_compiler.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "detail/test_support.hpp"

namespace upr = universal_protocol_runtime;

namespace {

upr::ProtocolDefinition make_valid_definition() {
  upr::FieldDefinition header =
      upr_test_support::make_scalar_field("header", upr::FieldKind::kUnsigned, 2, upr::ByteOrder::kBigEndian);
  upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("version", 13, 3));
  upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("kind", 0, 12));

  upr::FieldDefinition crc = upr_test_support::make_scalar_field("crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&crc, "xor8");

  return upr_test_support::make_protocol(
      "feeds",
      {
          upr_test_support::make_message(
              "Order",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 1),
                  header,
                  upr_test_support::make_scalar_field("price", upr::FieldKind::kFloat32, 4),
                  upr_test_support::make_scalar_field("quantity", upr::FieldKind::kUnsigned, 4),
                  upr_test_support::make_enum_field(
                      "side", 1, {{.value = 1, .name = "Buy"}, {.value = 2, .name = "Sell"}}),
                  upr_test_support::make_string_field("symbol", 4),
              }),
          upr_test_support::make_message(
              "Blob",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 2),
                  upr_test_support::make_scalar_field("length", upr::FieldKind::kUnsigned, 1),
                  upr_test_support::make_dynamic_bytes_field("payload", "length"),
                  crc,
              }),
          upr_test_support::make_message(
              "NestedOrder",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 3),
                  upr_test_support::make_struct_field("order", "OrderBody"),
              }),
      },
      {
          upr_test_support::make_struct("OrderBody",
                                        {
                                            upr_test_support::make_scalar_field("price", upr::FieldKind::kUnsigned, 4),
                                            upr_test_support::make_scalar_field("qty", upr::FieldKind::kUnsigned, 4),
                                        }),
      });
}

TEST(SchemaCompilerTest, CompilesProtocolsAndSupportsLookups) {
  upr::StatusOr<upr::CompiledProtocol> compiled = upr::compile_protocol(make_valid_definition());

  ASSERT_TRUE(compiled.ok()) << compiled.status().message();
  EXPECT_EQ(compiled.value().name(), "feeds");
  ASSERT_EQ(compiled.value().messages().size(), 3U);
  ASSERT_EQ(compiled.value().structs().size(), 1U);
  EXPECT_NE(compiled.value().fingerprint(), 0U);

  const upr::CompiledMessage* blob = compiled.value().find_message("Blob");
  ASSERT_NE(blob, nullptr);
  EXPECT_EQ(blob->minimum_size(), 3U);
  EXPECT_FALSE(blob->allow_trailing_bytes());
  ASSERT_EQ(blob->fields().size(), 4U);
  EXPECT_TRUE(blob->fields()[2].dynamic_size);
  EXPECT_EQ(blob->fields()[2].size_from_field, 1U);
  EXPECT_TRUE(blob->find_field("payload").has_value());
  ASSERT_EQ(blob->checksums().size(), 1U);
  EXPECT_EQ(blob->checksums()[0].algorithm_name, "xor8");

  const upr::CompiledMessage* order = compiled.value().find_message("Order");
  ASSERT_NE(order, nullptr);
  ASSERT_EQ(order->bit_fields().size(), 2U);
  EXPECT_TRUE(order->find_bit_field("version").has_value());
  EXPECT_EQ(order->fields().back().kind, upr::FieldKind::kString);

  const upr::CompiledMessage* order_body = compiled.value().find_struct("OrderBody");
  ASSERT_NE(order_body, nullptr);
  EXPECT_EQ(order_body->minimum_size(), 8U);
  EXPECT_EQ(compiled.value().find_struct("Missing"), nullptr);
  EXPECT_EQ(compiled.value().struct_by_id(99), nullptr);

  const upr::CompiledMessage* nested = compiled.value().find_message("NestedOrder");
  ASSERT_NE(nested, nullptr);
  ASSERT_EQ(nested->fields().size(), 2U);
  EXPECT_EQ(nested->fields()[1].kind, upr::FieldKind::kStruct);
}

TEST(SchemaCompilerTest, CompilesBigEndianDispatchPrefixesSignedBitfieldsAndChecksumAnchors) {
  upr::FieldDefinition kind = upr_test_support::make_scalar_field(
      "kind", upr::FieldKind::kUnsigned, 2, upr::ByteOrder::kBigEndian, true, 0xB234U);
  upr::FieldDefinition flags = upr_test_support::make_scalar_field("flags", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_bit_field(&flags, upr_test_support::make_bit_field("delta", 4, 4, true));

  upr::FieldDefinition before_crc = upr_test_support::make_scalar_field("before_crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&before_crc, "xor8", "frame_start", "before_self");

  upr::FieldDefinition after_crc = upr_test_support::make_scalar_field("after_crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&after_crc, "xor8", "after_self", "frame_end");

  upr::FieldDefinition range_crc = upr_test_support::make_scalar_field("range_crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&range_crc, "xor8", "payload.start", "tail.end");

  upr::StatusOr<upr::CompiledProtocol> compiled = upr::compile_protocol(upr_test_support::make_protocol(
      "anchored",
      {
          upr_test_support::make_message(
              "Anchored",
              {
                  kind,
                  flags,
                  upr_test_support::make_scalar_field("payload", upr::FieldKind::kUnsigned, 1),
                  before_crc,
                  after_crc,
                  upr_test_support::make_scalar_field("tail", upr::FieldKind::kUnsigned, 1),
                  range_crc,
              }),
      }));

  ASSERT_TRUE(compiled.ok()) << compiled.status().message();
  const upr::CompiledMessage* message = compiled.value().find_message("Anchored");
  ASSERT_NE(message, nullptr);
  ASSERT_EQ(message->dispatch_prefix().size(), 2U);
  EXPECT_EQ(message->dispatch_prefix()[0], std::byte{0xB2});
  EXPECT_EQ(message->dispatch_prefix()[1], std::byte{0x34});
  ASSERT_EQ(message->bit_fields().size(), 1U);
  EXPECT_TRUE(message->bit_fields()[0].is_signed);
  ASSERT_EQ(message->checksums().size(), 3U);
  EXPECT_EQ(message->checksums()[0].from.kind, upr::ChecksumAnchorKind::kFrameStart);
  EXPECT_EQ(message->checksums()[0].to.kind, upr::ChecksumAnchorKind::kBeforeSelf);
  EXPECT_EQ(message->checksums()[1].from.kind, upr::ChecksumAnchorKind::kAfterSelf);
  EXPECT_EQ(message->checksums()[1].to.kind, upr::ChecksumAnchorKind::kFrameEnd);
  EXPECT_EQ(message->checksums()[2].from.kind, upr::ChecksumAnchorKind::kFieldStart);
  EXPECT_EQ(message->checksums()[2].from.field_id, 2U);
  EXPECT_EQ(message->checksums()[2].to.kind, upr::ChecksumAnchorKind::kFieldEnd);
  EXPECT_EQ(message->checksums()[2].to.field_id, 5U);
}

TEST(SchemaCompilerTest, FingerprintChangesWhenDefinitionChanges) {
  upr::ProtocolDefinition first = make_valid_definition();
  upr::ProtocolDefinition second = make_valid_definition();
  second.structs[0].fields[1].name = "updated_qty";

  upr::StatusOr<upr::CompiledProtocol> first_compiled = upr::compile_protocol(first);
  upr::StatusOr<upr::CompiledProtocol> second_compiled = upr::compile_protocol(second);

  ASSERT_TRUE(first_compiled.ok()) << first_compiled.status().message();
  ASSERT_TRUE(second_compiled.ok()) << second_compiled.status().message();
  EXPECT_NE(first_compiled.value().fingerprint(), second_compiled.value().fingerprint());
}

struct InvalidCompilerCase {
  std::string name;
  upr::ProtocolDefinition (*factory)();
  upr::StatusCode expected_code = upr::StatusCode::kInvalidArgument;
  std::string expected_message_substring;
};

upr::ProtocolDefinition make_empty_protocol_name() {
  upr::ProtocolDefinition protocol = make_valid_definition();
  protocol.name.clear();
  return protocol;
}

upr::ProtocolDefinition make_protocol_without_messages() {
  upr::ProtocolDefinition protocol;
  protocol.name = "empty";
  return protocol;
}

upr::ProtocolDefinition make_empty_message_name() {
  upr::ProtocolDefinition protocol = make_valid_definition();
  protocol.messages[0].name.clear();
  return protocol;
}

upr::ProtocolDefinition make_empty_struct_name() {
  upr::ProtocolDefinition protocol = make_valid_definition();
  protocol.structs[0].name.clear();
  return protocol;
}

upr::ProtocolDefinition make_duplicate_struct_names() {
  upr::ProtocolDefinition protocol = make_valid_definition();
  protocol.structs.push_back(protocol.structs.front());
  return protocol;
}

upr::ProtocolDefinition make_duplicate_struct_and_message_names() {
  upr::ProtocolDefinition protocol = make_valid_definition();
  protocol.structs[0].name = protocol.messages[0].name;
  return protocol;
}

upr::ProtocolDefinition make_duplicate_message_names() {
  upr::ProtocolDefinition protocol = make_valid_definition();
  protocol.messages[1].name = protocol.messages[0].name;
  return protocol;
}

upr::ProtocolDefinition make_message_without_fields() {
  return upr_test_support::make_protocol("empty_fields", {upr_test_support::make_message("Empty", {})});
}

upr::ProtocolDefinition make_too_many_fields() {
  std::vector<upr::FieldDefinition> fields;
  for (size_t index = 0; index <= upr::kMaxFieldsPerMessage; ++index) {
    fields.push_back(
        upr_test_support::make_scalar_field("field_" + std::to_string(index), upr::FieldKind::kUnsigned, 1));
  }
  return upr_test_support::make_protocol("too_many", {upr_test_support::make_message("Overflow", std::move(fields))});
}

upr::ProtocolDefinition make_empty_field_name() {
  upr::ProtocolDefinition protocol = make_valid_definition();
  protocol.messages[0].fields[0].name.clear();
  return protocol;
}

upr::ProtocolDefinition make_duplicate_field_names() {
  upr::ProtocolDefinition protocol = make_valid_definition();
  protocol.messages[0].fields[1].name = protocol.messages[0].fields[0].name;
  return protocol;
}

upr::ProtocolDefinition make_byte_field_with_expect() {
  upr::FieldDefinition payload = upr_test_support::make_fixed_bytes_field("payload", 4);
  payload.has_expected_unsigned = true;
  payload.expected_unsigned = 1;
  return upr_test_support::make_protocol("bytes_expect", {upr_test_support::make_message("Blob", {payload})});
}

upr::ProtocolDefinition make_byte_field_with_both_size_and_size_from() {
  upr::FieldDefinition payload = upr_test_support::make_fixed_bytes_field("payload", 4);
  payload.size_from_field = "length";
  return upr_test_support::make_protocol(
      "bytes_both",
      {upr_test_support::make_message("Blob",
                                      {
                                          upr_test_support::make_scalar_field("length", upr::FieldKind::kUnsigned, 1),
                                          payload,
                                      })});
}

upr::ProtocolDefinition make_byte_field_with_neither_size_nor_size_from() {
  upr::FieldDefinition payload = upr_test_support::make_fixed_bytes_field("payload", 0);
  return upr_test_support::make_protocol("bytes_neither", {upr_test_support::make_message("Blob", {payload})});
}

upr::ProtocolDefinition make_dynamic_ref_to_signed_field() {
  return upr_test_support::make_protocol(
      "dynamic_signed",
      {upr_test_support::make_message("Blob",
                                      {
                                          upr_test_support::make_scalar_field("length", upr::FieldKind::kSigned, 1),
                                          upr_test_support::make_dynamic_bytes_field("payload", "length"),
                                      })});
}

upr::ProtocolDefinition make_dynamic_ref_missing() {
  return upr_test_support::make_protocol(
      "dynamic_missing",
      {upr_test_support::make_message("Blob", {upr_test_support::make_dynamic_bytes_field("payload", "missing")})});
}

upr::ProtocolDefinition make_invalid_scalar_width() {
  return upr_test_support::make_protocol(
      "bad_width",
      {upr_test_support::make_message("Message",
                                      {upr_test_support::make_scalar_field("value", upr::FieldKind::kUnsigned, 3)})});
}

upr::ProtocolDefinition make_invalid_float32_width() {
  return upr_test_support::make_protocol(
      "bad_float32",
      {upr_test_support::make_message("Message",
                                      {upr_test_support::make_scalar_field("value", upr::FieldKind::kFloat32, 2)})});
}

upr::ProtocolDefinition make_invalid_float64_width() {
  return upr_test_support::make_protocol(
      "bad_float64",
      {upr_test_support::make_message("Message",
                                      {upr_test_support::make_scalar_field("value", upr::FieldKind::kFloat64, 4)})});
}

upr::ProtocolDefinition make_unknown_struct_type() {
  return upr_test_support::make_protocol(
      "unknown_struct",
      {upr_test_support::make_message(
          "Message",
          {
              upr_test_support::make_scalar_field("message_type", upr::FieldKind::kUnsigned, 1),
              upr_test_support::make_struct_field("body", "MissingStruct"),
          })});
}

upr::ProtocolDefinition make_recursive_struct_dependency() {
  return upr_test_support::make_protocol(
      "recursive_structs",
      {
          upr_test_support::make_message(
              "Envelope",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 1),
                  upr_test_support::make_struct_field("body", "A"),
              }),
      },
      {
          upr_test_support::make_struct("A", {upr_test_support::make_struct_field("b", "B")}),
          upr_test_support::make_struct("B", {upr_test_support::make_struct_field("a", "A")}),
      });
}

upr::ProtocolDefinition make_struct_field_with_expect() {
  upr::FieldDefinition body = upr_test_support::make_struct_field("body", "OrderBody");
  body.has_expected_unsigned = true;
  body.expected_unsigned = 1;
  return upr_test_support::make_protocol(
      "struct_expect",
      {upr_test_support::make_message("Message", {body})},
      {upr_test_support::make_struct("OrderBody",
                                     {upr_test_support::make_scalar_field("value", upr::FieldKind::kUnsigned, 1)})});
}

upr::ProtocolDefinition make_struct_field_with_size() {
  upr::FieldDefinition body = upr_test_support::make_struct_field("body", "OrderBody");
  body.fixed_size = 4;
  return upr_test_support::make_protocol(
      "struct_size",
      {upr_test_support::make_message("Message", {body})},
      {upr_test_support::make_struct("OrderBody",
                                     {upr_test_support::make_scalar_field("value", upr::FieldKind::kUnsigned, 1)})});
}

upr::ProtocolDefinition make_struct_field_with_bitfields() {
  upr::FieldDefinition body = upr_test_support::make_struct_field("body", "OrderBody");
  upr_test_support::add_bit_field(&body, upr_test_support::make_bit_field("bad", 0, 1));
  return upr_test_support::make_protocol(
      "struct_bits",
      {upr_test_support::make_message("Message", {body})},
      {upr_test_support::make_struct("OrderBody",
                                     {upr_test_support::make_scalar_field("value", upr::FieldKind::kUnsigned, 1)})});
}

upr::ProtocolDefinition make_struct_field_without_reference() {
  upr::FieldDefinition body;
  body.name = "body";
  body.kind = upr::FieldKind::kStruct;
  return upr_test_support::make_protocol("struct_missing_ref", {upr_test_support::make_message("Message", {body})});
}

upr::ProtocolDefinition make_bitfield_on_bytes() {
  upr::FieldDefinition payload = upr_test_support::make_fixed_bytes_field("payload", 4);
  upr_test_support::add_bit_field(&payload, upr_test_support::make_bit_field("version", 0, 1));
  return upr_test_support::make_protocol("bitfield_bytes", {upr_test_support::make_message("Blob", {payload})});
}

upr::ProtocolDefinition make_empty_bitfield_name() {
  upr::FieldDefinition header = upr_test_support::make_scalar_field("header", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("", 0, 1));
  return upr_test_support::make_protocol("bitfield_empty", {upr_test_support::make_message("Message", {header})});
}

upr::ProtocolDefinition make_duplicate_bitfield_names() {
  upr::FieldDefinition header = upr_test_support::make_scalar_field("header", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("dup", 0, 1));
  upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("dup", 1, 1));
  return upr_test_support::make_protocol("bitfield_duplicate", {upr_test_support::make_message("Message", {header})});
}

upr::ProtocolDefinition make_zero_width_bitfield() {
  upr::FieldDefinition header = upr_test_support::make_scalar_field("header", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("zero", 0, 0));
  return upr_test_support::make_protocol("bitfield_zero", {upr_test_support::make_message("Message", {header})});
}

upr::ProtocolDefinition make_too_wide_bitfield() {
  upr::FieldDefinition header = upr_test_support::make_scalar_field("header", upr::FieldKind::kUnsigned, 8);
  upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("too_wide", 0, 65));
  return upr_test_support::make_protocol("bitfield_too_wide", {upr_test_support::make_message("Message", {header})});
}

upr::ProtocolDefinition make_out_of_range_bitfield() {
  upr::FieldDefinition header = upr_test_support::make_scalar_field("header", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("out_of_range", 7, 2));
  return upr_test_support::make_protocol("bitfield_range", {upr_test_support::make_message("Message", {header})});
}

upr::ProtocolDefinition make_too_many_bitfields() {
  upr::FieldDefinition header = upr_test_support::make_scalar_field("header", upr::FieldKind::kUnsigned, 8);
  for (size_t index = 0; index <= upr::kMaxBitFieldsPerMessage; ++index) {
    upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("bit_" + std::to_string(index), 0, 1));
  }
  return upr_test_support::make_protocol("bitfield_limit", {upr_test_support::make_message("Message", {header})});
}

upr::ProtocolDefinition make_checksum_width_mismatch() {
  upr::FieldDefinition crc = upr_test_support::make_scalar_field("crc", upr::FieldKind::kUnsigned, 2);
  upr_test_support::add_checksum(&crc, "xor8");
  return upr_test_support::make_protocol(
      "checksum_width",
      {upr_test_support::make_message(
          "Message",
          {
              upr_test_support::make_scalar_field("message_type", upr::FieldKind::kUnsigned, 1),
              crc,
          })});
}

upr::ProtocolDefinition make_checksum_unknown_algorithm() {
  upr::FieldDefinition crc = upr_test_support::make_scalar_field("crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&crc, "missing_crc");
  return upr_test_support::make_protocol(
      "checksum_unknown",
      {upr_test_support::make_message(
          "Message",
          {
              upr_test_support::make_scalar_field("message_type", upr::FieldKind::kUnsigned, 1),
              crc,
          })});
}

upr::ProtocolDefinition make_checksum_non_scalar_field() {
  upr::FieldDefinition payload = upr_test_support::make_string_field("payload", 4);
  upr_test_support::add_checksum(&payload, "xor8");
  return upr_test_support::make_protocol("checksum_string", {upr_test_support::make_message("Message", {payload})});
}

upr::ProtocolDefinition make_checksum_unknown_start_anchor() {
  upr::FieldDefinition crc = upr_test_support::make_scalar_field("crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&crc, "xor8", "missing.start", "before_self");
  return upr_test_support::make_protocol(
      "checksum_bad_start",
      {upr_test_support::make_message("Message",
                                      {
                                          upr_test_support::make_scalar_field("payload", upr::FieldKind::kUnsigned, 1),
                                          crc,
                                      })});
}

upr::ProtocolDefinition make_checksum_unknown_end_anchor() {
  upr::FieldDefinition crc = upr_test_support::make_scalar_field("crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&crc, "xor8", "frame_start", "missing.end");
  return upr_test_support::make_protocol(
      "checksum_bad_end",
      {upr_test_support::make_message("Message",
                                      {
                                          upr_test_support::make_scalar_field("payload", upr::FieldKind::kUnsigned, 1),
                                          crc,
                                      })});
}

upr::ProtocolDefinition make_checksum_unsupported_anchor() {
  upr::FieldDefinition crc = upr_test_support::make_scalar_field("crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&crc, "xor8", "payload.middle", "before_self");
  return upr_test_support::make_protocol(
      "checksum_bad_anchor",
      {upr_test_support::make_message("Message",
                                      {
                                          upr_test_support::make_scalar_field("payload", upr::FieldKind::kUnsigned, 1),
                                          crc,
                                      })});
}

upr::ProtocolDefinition make_string_field_with_expect() {
  upr::FieldDefinition payload = upr_test_support::make_string_field("payload", 4);
  payload.has_expected_unsigned = true;
  payload.expected_unsigned = 7;
  return upr_test_support::make_protocol("string_expect", {upr_test_support::make_message("Message", {payload})});
}

class InvalidSchemaCompilerTest : public ::testing::TestWithParam<InvalidCompilerCase> {
 public:
  ~InvalidSchemaCompilerTest() noexcept override = default;
};

TEST_P(InvalidSchemaCompilerTest, RejectsInvalidDefinitions) {
  const InvalidCompilerCase& param = GetParam();

  upr::StatusOr<upr::CompiledProtocol> compiled = upr::compile_protocol(param.factory());

  ASSERT_FALSE(compiled.ok());
  EXPECT_EQ(compiled.status().code(), param.expected_code);
  EXPECT_NE(std::string(compiled.status().message()).find(param.expected_message_substring), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(
    Coverage,
    InvalidSchemaCompilerTest,
    ::testing::Values(
        InvalidCompilerCase{"empty_protocol_name",
                            &make_empty_protocol_name,
                            upr::StatusCode::kInvalidArgument,
                            "Protocol name must not be empty"},
        InvalidCompilerCase{
            "no_messages", &make_protocol_without_messages, upr::StatusCode::kInvalidArgument, "at least one message"},
        InvalidCompilerCase{"empty_message_name",
                            &make_empty_message_name,
                            upr::StatusCode::kInvalidArgument,
                            "Message name must not be empty"},
        InvalidCompilerCase{"empty_struct_name",
                            &make_empty_struct_name,
                            upr::StatusCode::kInvalidArgument,
                            "Struct name must not be empty"},
        InvalidCompilerCase{"duplicate_struct_names",
                            &make_duplicate_struct_names,
                            upr::StatusCode::kInvalidArgument,
                            "Duplicate layout name"},
        InvalidCompilerCase{"duplicate_struct_and_message_names",
                            &make_duplicate_struct_and_message_names,
                            upr::StatusCode::kInvalidArgument,
                            "Duplicate layout name"},
        InvalidCompilerCase{"duplicate_message_names",
                            &make_duplicate_message_names,
                            upr::StatusCode::kInvalidArgument,
                            "Duplicate layout name"},
        InvalidCompilerCase{"message_without_fields",
                            &make_message_without_fields,
                            upr::StatusCode::kInvalidArgument,
                            "must have at least one field"},
        InvalidCompilerCase{"too_many_fields",
                            &make_too_many_fields,
                            upr::StatusCode::kExhausted,
                            "exceeds the per-message field limit"},
        InvalidCompilerCase{"empty_field_name",
                            &make_empty_field_name,
                            upr::StatusCode::kInvalidArgument,
                            "Field name must not be empty"},
        InvalidCompilerCase{"duplicate_field_names",
                            &make_duplicate_field_names,
                            upr::StatusCode::kInvalidArgument,
                            "Duplicate field name"},
        InvalidCompilerCase{"byte_field_with_expect",
                            &make_byte_field_with_expect,
                            upr::StatusCode::kInvalidArgument,
                            "cannot use 'expect'"},
        InvalidCompilerCase{"string_field_with_expect",
                            &make_string_field_with_expect,
                            upr::StatusCode::kInvalidArgument,
                            "cannot use 'expect'"},
        InvalidCompilerCase{"byte_field_with_both_size_and_size_from",
                            &make_byte_field_with_both_size_and_size_from,
                            upr::StatusCode::kInvalidArgument,
                            "exactly one of 'size' or 'size_from'"},
        InvalidCompilerCase{"byte_field_with_neither_size_nor_size_from",
                            &make_byte_field_with_neither_size_nor_size_from,
                            upr::StatusCode::kInvalidArgument,
                            "exactly one of 'size' or 'size_from'"},
        InvalidCompilerCase{"dynamic_ref_to_signed",
                            &make_dynamic_ref_to_signed_field,
                            upr::StatusCode::kInvalidArgument,
                            "must reference an unsigned or enum field"},
        InvalidCompilerCase{"dynamic_ref_missing",
                            &make_dynamic_ref_missing,
                            upr::StatusCode::kInvalidArgument,
                            "must reference a prior field"},
        InvalidCompilerCase{
            "invalid_scalar_width", &make_invalid_scalar_width, upr::StatusCode::kInvalidArgument, "unsupported width"},
        InvalidCompilerCase{"invalid_float32_width",
                            &make_invalid_float32_width,
                            upr::StatusCode::kInvalidArgument,
                            "float32 fields must be 4 bytes wide"},
        InvalidCompilerCase{"invalid_float64_width",
                            &make_invalid_float64_width,
                            upr::StatusCode::kInvalidArgument,
                            "float64 fields must be 8 bytes wide"},
        InvalidCompilerCase{
            "unknown_struct_type", &make_unknown_struct_type, upr::StatusCode::kNotFound, "Unknown struct type"},
        InvalidCompilerCase{"recursive_struct_dependency",
                            &make_recursive_struct_dependency,
                            upr::StatusCode::kInvalidArgument,
                            "Recursive struct dependency detected"},
        InvalidCompilerCase{"struct_field_with_expect",
                            &make_struct_field_with_expect,
                            upr::StatusCode::kInvalidArgument,
                            "cannot use 'expect'"},
        InvalidCompilerCase{"struct_field_with_size",
                            &make_struct_field_with_size,
                            upr::StatusCode::kInvalidArgument,
                            "derives its size from the referenced struct"},
        InvalidCompilerCase{"struct_field_with_bitfields",
                            &make_struct_field_with_bitfields,
                            upr::StatusCode::kInvalidArgument,
                            "Bitfields are only supported on scalar containers"},
        InvalidCompilerCase{"struct_field_without_reference",
                            &make_struct_field_without_reference,
                            upr::StatusCode::kInvalidArgument,
                            "must reference a struct type"},
        InvalidCompilerCase{"bitfield_on_bytes",
                            &make_bitfield_on_bytes,
                            upr::StatusCode::kInvalidArgument,
                            "Bitfields are only supported on scalar containers"},
        InvalidCompilerCase{"empty_bitfield_name",
                            &make_empty_bitfield_name,
                            upr::StatusCode::kInvalidArgument,
                            "Bitfield names must not be empty"},
        InvalidCompilerCase{"duplicate_bitfield_names",
                            &make_duplicate_bitfield_names,
                            upr::StatusCode::kInvalidArgument,
                            "Duplicate bitfield name"},
        InvalidCompilerCase{"zero_width_bitfield",
                            &make_zero_width_bitfield,
                            upr::StatusCode::kInvalidArgument,
                            "must be at least 1 bit wide"},
        InvalidCompilerCase{
            "too_wide_bitfield", &make_too_wide_bitfield, upr::StatusCode::kInvalidArgument, "cannot exceed 64 bits"},
        InvalidCompilerCase{"out_of_range_bitfield",
                            &make_out_of_range_bitfield,
                            upr::StatusCode::kInvalidArgument,
                            "exceeds the width of container field"},
        InvalidCompilerCase{
            "too_many_bitfields", &make_too_many_bitfields, upr::StatusCode::kExhausted, "exceeds the bitfield limit"},
        InvalidCompilerCase{"checksum_width_mismatch",
                            &make_checksum_width_mismatch,
                            upr::StatusCode::kInvalidArgument,
                            "must match the width of algorithm"},
        InvalidCompilerCase{"checksum_unknown_algorithm",
                            &make_checksum_unknown_algorithm,
                            upr::StatusCode::kNotFound,
                            "Unknown checksum algorithm"},
        InvalidCompilerCase{"checksum_non_scalar_field",
                            &make_checksum_non_scalar_field,
                            upr::StatusCode::kInvalidArgument,
                            "Checksum fields must use an unsigned or enum scalar type"},
        InvalidCompilerCase{"checksum_unknown_start_anchor",
                            &make_checksum_unknown_start_anchor,
                            upr::StatusCode::kInvalidArgument,
                            "Unknown checksum anchor field"},
        InvalidCompilerCase{"checksum_unknown_end_anchor",
                            &make_checksum_unknown_end_anchor,
                            upr::StatusCode::kInvalidArgument,
                            "Unknown checksum anchor field"},
        InvalidCompilerCase{"checksum_unsupported_anchor",
                            &make_checksum_unsupported_anchor,
                            upr::StatusCode::kInvalidArgument,
                            "Unsupported checksum anchor"}),
    [](const ::testing::TestParamInfo<InvalidCompilerCase>& info) { return info.param.name; });

}  // namespace
