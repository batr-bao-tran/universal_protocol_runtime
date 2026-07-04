#include "universal_protocol_runtime/codegen/bindings_generator.hpp"

#include <gtest/gtest.h>

#include <string>

#include "detail/test_support.hpp"

namespace upr = universal_protocol_runtime;

namespace {

upr::ProtocolDefinition make_codegen_definition() {
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
      },
      {
          upr_test_support::make_struct("OrderBody",
                                        {
                                            upr_test_support::make_scalar_field("price", upr::FieldKind::kUnsigned, 4),
                                            upr_test_support::make_scalar_field("qty", upr::FieldKind::kUnsigned, 4),
                                        }),
      });
}

upr::ProtocolDefinition make_fallback_codegen_definition() {
  return upr_test_support::make_protocol(
      "!!!",
      {
          upr_test_support::make_message("???",
                                         {
                                             upr_test_support::make_scalar_field("***", upr::FieldKind::kUnsigned, 1),
                                         }),
      });
}

upr::ProtocolDefinition make_rich_codegen_definition() {
  upr::FieldDefinition tail_crc = upr_test_support::make_scalar_field("tail_crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&tail_crc, "xor8", "after_self", "frame_end");

  upr::FieldDefinition range_crc = upr_test_support::make_scalar_field("range_crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&range_crc, "xor8", "payload.start", "tail.end");

  return upr_test_support::make_protocol(
      "rich_codegen",
      {
          upr_test_support::make_message(
              "Escaped\"\n\r\t\\\x01",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 9),
                  upr_test_support::make_scalar_field("signed_value", upr::FieldKind::kSigned, 1),
                  upr_test_support::make_scalar_field("price64", upr::FieldKind::kFloat64, 8),
                  upr_test_support::make_scalar_field("text_length", upr::FieldKind::kUnsigned, 1),
                  upr_test_support::make_dynamic_string_field(
                      "field\"\n\r\t\\\x01", "text_length", upr::StringEncoding::kUtf8),
                  upr_test_support::make_scalar_field("payload", upr::FieldKind::kUnsigned, 1),
                  tail_crc,
                  upr_test_support::make_scalar_field("tail", upr::FieldKind::kUnsigned, 1),
                  range_crc,
              }),
      });
}

TEST(BindingsGeneratorTest, GeneratesCppBindingsForMessagesAndStructs) {
  const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(make_codegen_definition());

  upr::StatusOr<std::string> generated = upr::generate_cpp_bindings_header(compiled);

  ASSERT_TRUE(generated.ok()) << generated.status().message();
  EXPECT_NE(generated.value().find("namespace upr_generated {"), std::string::npos);
  EXPECT_NE(generated.value().find("namespace feeds {"), std::string::npos);
  EXPECT_NE(generated.value().find("inline constexpr std::string_view kProtocolName = \"feeds\";"), std::string::npos);
  EXPECT_NE(generated.value().find("struct OrderBody final {"), std::string::npos);
  EXPECT_NE(generated.value().find("struct Order final {"), std::string::npos);
  EXPECT_NE(generated.value().find("static constexpr universal_protocol_runtime::FieldId kMessageType"),
            std::string::npos);
  EXPECT_NE(generated.value().find("static constexpr universal_protocol_runtime::BitFieldId kVersion"),
            std::string::npos);
  EXPECT_NE(generated.value().find(
                "static constexpr auto kDispatchPrefix = std::array<std::byte, 1>{static_cast<std::byte>(0x01)};"),
            std::string::npos);
  EXPECT_NE(generated.value().find(".algorithm_name = \"xor8\""), std::string::npos);
}

TEST(BindingsGeneratorTest, GeneratesPythonBindingsWithMetadataClasses) {
  const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(make_codegen_definition());

  upr::StatusOr<std::string> generated = upr::generate_python_bindings_module(compiled);

  ASSERT_TRUE(generated.ok()) << generated.status().message();
  EXPECT_NE(generated.value().find("from dataclasses import dataclass"), std::string::npos);
  EXPECT_NE(generated.value().find("PROTOCOL_NAME = \"feeds\""), std::string::npos);
  EXPECT_NE(generated.value().find("class Order:"), std::string::npos);
  EXPECT_NE(generated.value().find("class Fields:"), std::string::npos);
  EXPECT_NE(generated.value().find("MESSAGE_TYPE = 0"), std::string::npos);
  EXPECT_NE(generated.value().find("DISPATCH_PREFIX = b\"\\x01\""), std::string::npos);
  EXPECT_NE(generated.value().find("FieldBinding(id=2, name=\"price\", kind=\"float32\""), std::string::npos);
  EXPECT_NE(generated.value().find("ChecksumBinding(field_id=3, result_width_bytes=1, algorithm_name=\"xor8\""),
            std::string::npos);
}

TEST(BindingsGeneratorTest, SanitizesIdentifiersForGeneratedBindings) {
  upr::ProtocolDefinition definition = upr_test_support::make_protocol(
      "feed capture 2",
      {
          upr_test_support::make_message(
              "trade update",
              {
                  upr_test_support::make_scalar_field("message type", upr::FieldKind::kUnsigned, 1),
              }),
      });

  const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(definition);

  upr::StatusOr<std::string> cpp_generated = upr::generate_cpp_bindings_header(compiled);
  upr::StatusOr<std::string> python_generated = upr::generate_python_bindings_module(compiled);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  ASSERT_TRUE(python_generated.ok()) << python_generated.status().message();
  EXPECT_NE(cpp_generated.value().find("namespace feed_capture_2 {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("struct TradeUpdate final {"), std::string::npos);
  EXPECT_NE(python_generated.value().find("class TradeUpdate:"), std::string::npos);
  EXPECT_NE(python_generated.value().find("MESSAGE_TYPE = 0"), std::string::npos);
}

TEST(BindingsGeneratorTest, SupportsFallbackIdentifiersAndCustomGenerationOptions) {
  const upr::CompiledProtocol compiled =
      upr_test_support::compile_protocol_or_throw(make_fallback_codegen_definition());

  upr::StatusOr<std::string> cpp_generated = upr::generate_cpp_bindings_header(compiled,
                                                                               {
                                                                                   .namespace_prefix = "!!!",
                                                                                   .protocol_namespace = "???",
                                                                                   .header_guard = "***",
                                                                               });
  upr::StatusOr<std::string> python_generated =
      upr::generate_python_bindings_module(compiled, {.module_name = "Custom Module 9"});

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  ASSERT_TRUE(python_generated.ok()) << python_generated.status().message();
  EXPECT_NE(cpp_generated.value().find("#ifndef UNIVERSAL_PROTOCOL_RUNTIME__GENERATED_BINDINGS_HPP_"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("namespace generated {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("struct GeneratedBinding final {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("static constexpr universal_protocol_runtime::FieldId kUnnamed"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("static constexpr bool kEmpty = true;"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("std::array<std::byte, 0>{}"), std::string::npos);
  EXPECT_NE(python_generated.value().find("module 'custom_module_9'"), std::string::npos);
  EXPECT_NE(python_generated.value().find("class GeneratedBinding:"), std::string::npos);
  EXPECT_NE(python_generated.value().find("UNNAMED = 0"), std::string::npos);
  EXPECT_NE(python_generated.value().find("DISPATCH_PREFIX = b\"\""), std::string::npos);
  EXPECT_NE(python_generated.value().find("class BitFields:\n    pass"), std::string::npos);
}

TEST(BindingsGeneratorTest, EmitsEscapedNamesAdditionalFieldKindsAndChecksumAnchors) {
  const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(make_rich_codegen_definition());

  upr::StatusOr<std::string> cpp_generated = upr::generate_cpp_bindings_header(compiled);
  upr::StatusOr<std::string> python_generated = upr::generate_python_bindings_module(compiled);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  ASSERT_TRUE(python_generated.ok()) << python_generated.status().message();
  EXPECT_NE(cpp_generated.value().find("Escaped\\\"\\n\\r\\t\\\\\\x01"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("field\\\"\\n\\r\\t\\\\\\x01"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("FieldKind::kSigned"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("FieldKind::kFloat64"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("StringEncoding::kUtf8"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("ChecksumAnchorKind::kAfterSelf"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("ChecksumAnchorKind::kFrameEnd"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("ChecksumAnchorKind::kFieldStart"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("ChecksumAnchorKind::kFieldEnd"), std::string::npos);

  EXPECT_NE(python_generated.value().find("kind=\"signed\""), std::string::npos);
  EXPECT_NE(python_generated.value().find("kind=\"float64\""), std::string::npos);
  EXPECT_NE(python_generated.value().find("string_encoding=\"utf8\""), std::string::npos);
  EXPECT_NE(python_generated.value().find("after_self"), std::string::npos);
  EXPECT_NE(python_generated.value().find("frame_end"), std::string::npos);
  EXPECT_NE(python_generated.value().find("field_start"), std::string::npos);
  EXPECT_NE(python_generated.value().find("field_end"), std::string::npos);
}

}  // namespace
