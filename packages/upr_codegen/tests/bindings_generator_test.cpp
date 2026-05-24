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

upr::ProtocolDefinition make_numeric_codegen_definition() {
  return upr_test_support::make_protocol(
      "9 proto",
      {
          upr_test_support::make_message(
              "123 snapshot",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 3),
                  upr_test_support::make_scalar_field(
                      "revision", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 7),
                  upr_test_support::make_fixed_bytes_field("4 raw bytes", 3),
                  upr_test_support::make_scalar_field("big_total", upr::FieldKind::kUnsigned, 8),
                  upr_test_support::make_scalar_field("signed_short", upr::FieldKind::kSigned, 2),
                  upr_test_support::make_scalar_field("signed_word", upr::FieldKind::kSigned, 4),
                  upr_test_support::make_scalar_field("signed_long", upr::FieldKind::kSigned, 8),
                  upr_test_support::make_struct_field("7 body", "1 nested body"),
              }),
      },
      {
          upr_test_support::make_struct("1 nested body",
                                        {
                                            upr_test_support::make_scalar_field("id", upr::FieldKind::kUnsigned, 4),
                                        }),
      });
}

upr::ProtocolDefinition make_advanced_codegen_definition() {
  upr::FieldDefinition note_length = upr_test_support::make_scalar_field("note_len", upr::FieldKind::kUnsigned, 1);
  upr_test_support::set_presence(&note_length, "presence", 0);

  upr::FieldDefinition note =
      upr_test_support::make_dynamic_string_field("note", "note_len", upr::StringEncoding::kUtf8);
  upr_test_support::set_presence(&note, "presence", 0);

  upr::FieldDefinition revision = upr_test_support::make_scalar_field("revision", upr::FieldKind::kUnsigned, 1);
  upr_test_support::set_condition(&revision, "kind", 2);

  return upr_test_support::make_protocol(
      "advanced_codegen",
      {upr_test_support::make_message(
          "Snapshot",
          {upr_test_support::make_scalar_field(
               "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 11),
           upr_test_support::make_scalar_field("kind", upr::FieldKind::kUnsigned, 1),
           upr_test_support::make_scalar_field("presence", upr::FieldKind::kUnsigned, 1),
           upr_test_support::make_scalar_field("level_count", upr::FieldKind::kUnsigned, 1),
           upr_test_support::make_collection_field("levels", "Level", "level_count"),
           upr_test_support::make_variant_field("detail",
                                                "kind",
                                                {{.tag_value = 1, .referenced_type = "QuoteDetail"},
                                                 {.tag_value = 2, .referenced_type = "TradeDetail"}}),
           note_length,
           note,
           revision})},
      {upr_test_support::make_struct("Level",
                                     {upr_test_support::make_scalar_field("price", upr::FieldKind::kUnsigned, 2),
                                      upr_test_support::make_scalar_field("qty", upr::FieldKind::kUnsigned, 2)}),
       upr_test_support::make_struct("QuoteDetail",
                                     {upr_test_support::make_scalar_field("best_bid", upr::FieldKind::kUnsigned, 2),
                                      upr_test_support::make_scalar_field("best_ask", upr::FieldKind::kUnsigned, 2)}),
       upr_test_support::make_struct("TradeDetail",
                                     {upr_test_support::make_scalar_field("trade_id", upr::FieldKind::kUnsigned, 4)})});
}

upr::ProtocolDefinition make_checksum_codegen_definition() {
  upr::FieldDefinition sum16 = upr_test_support::make_scalar_field("sum16", upr::FieldKind::kUnsigned, 2);
  upr_test_support::add_checksum(&sum16, "sum16");

  upr::FieldDefinition crc16 = upr_test_support::make_scalar_field("crc16", upr::FieldKind::kUnsigned, 2);
  upr_test_support::add_checksum(&crc16, "crc16_ccitt");

  upr::FieldDefinition crc32 = upr_test_support::make_scalar_field("crc32", upr::FieldKind::kUnsigned, 4);
  upr_test_support::add_checksum(&crc32, "crc32");

  upr::FieldDefinition crc32c = upr_test_support::make_scalar_field("crc32c", upr::FieldKind::kUnsigned, 4);
  upr_test_support::add_checksum(&crc32c, "crc32c");

  return upr_test_support::make_protocol(
      "checksum_codegen",
      {upr_test_support::make_message(
           "Checksummed",
           {upr_test_support::make_scalar_field(
                "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 7),
            upr_test_support::make_scalar_field("payload", upr::FieldKind::kUnsigned, 1),
            sum16}),
       upr_test_support::make_message(
           "Crc16Message",
           {upr_test_support::make_scalar_field(
                "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 8),
            upr_test_support::make_scalar_field("payload", upr::FieldKind::kUnsigned, 1),
            crc16}),
       upr_test_support::make_message(
           "Crc32Message",
           {upr_test_support::make_scalar_field(
                "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 9),
            upr_test_support::make_scalar_field("payload", upr::FieldKind::kUnsigned, 1),
            crc32}),
       upr_test_support::make_message(
           "Crc32cMessage",
           {upr_test_support::make_scalar_field(
                "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 10),
            upr_test_support::make_scalar_field("payload", upr::FieldKind::kUnsigned, 1),
            crc32c})});
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
  EXPECT_NE(generated.value().find("class View final {"), std::string::npos);
  EXPECT_NE(generated.value().find("class Decoder final {"), std::string::npos);
  EXPECT_NE(generated.value().find("struct Value final {"), std::string::npos);
  EXPECT_NE(generated.value().find("std::optional<float> price() const {"), std::string::npos);
  EXPECT_NE(generated.value().find("std::optional<uint32_t> quantity() const {"), std::string::npos);
  EXPECT_NE(generated.value().find("std::optional<std::span<const char, 4>> symbol() const {"), std::string::npos);
  EXPECT_NE(generated.value().find("#include \"universal_protocol_runtime/decoder/direct_decode_support.hpp\""),
            std::string::npos);
  EXPECT_NE(generated.value().find("static constexpr bool kSupportsDirectValueDecode = true;"), std::string::npos);
  EXPECT_NE(generated.value().find("decode_value_direct("), std::string::npos);
  EXPECT_NE(generated.value().find("if constexpr (kSupportsDirectValueDecode) {"), std::string::npos);
  EXPECT_NE(generated.value().find("direct_decode_support::starts_with(frame, kDispatchPrefix)"), std::string::npos);
  EXPECT_NE(generated.value().find("direct_decode_support::read_unsigned_scalar<"), std::string::npos);
  EXPECT_NE(generated.value().find("direct_decode_support::runtime_validate_string<"), std::string::npos);
  EXPECT_NE(generated.value().find("return decode_value_direct(frame, value);"), std::string::npos);
  EXPECT_NE(generated.value().find("return decoder_->decode_as(*layout_, frame, message);"), std::string::npos);
  EXPECT_NE(generated.value().find("universal_protocol_runtime::DecodeStatus decode_value("), std::string::npos);
  EXPECT_NE(generated.value().find("value->quantity = *quantity_value;"), std::string::npos);
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

TEST(BindingsGeneratorTest, SupportsNestedNamespaceOptionsAndMessageAliases) {
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

  upr::CppBindingsOptions options;
  options.namespace_prefix = "universal_protocol_runtime";
  options.protocol_namespace = "benchmarks::generated";
  upr::StatusOr<std::string> cpp_generated = upr::generate_cpp_bindings_header(compiled, options);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  EXPECT_NE(cpp_generated.value().find("namespace universal_protocol_runtime {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("namespace benchmarks::generated {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("using TradeUpdate = messages::TradeUpdate;"), std::string::npos);
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

TEST(BindingsGeneratorTest, PreservesExplicitValidHeaderGuards) {
  const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(make_codegen_definition());

  upr::CppBindingsOptions options;
  options.header_guard = "UPR__BENCHMARKS__GENERATED_HPP_";
  upr::StatusOr<std::string> cpp_generated = upr::generate_cpp_bindings_header(compiled, options);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  EXPECT_NE(cpp_generated.value().find("#ifndef UPR__BENCHMARKS__GENERATED_HPP_"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("#define UPR__BENCHMARKS__GENERATED_HPP_"), std::string::npos);
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

TEST(BindingsGeneratorTest, GeneratesTypedBindingsForDigitPrefixedNamesStructsAndFixedBytes) {
  const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(make_numeric_codegen_definition());

  upr::StatusOr<std::string> cpp_generated = upr::generate_cpp_bindings_header(compiled);
  upr::StatusOr<std::string> python_generated = upr::generate_python_bindings_module(compiled);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  ASSERT_TRUE(python_generated.ok()) << python_generated.status().message();

  EXPECT_NE(cpp_generated.value().find("namespace generated_9_proto {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("struct N123Snapshot final {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("struct N1NestedBody final {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("static constexpr auto kDispatchPrefix = std::array<std::byte, 2>{"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("static constexpr universal_protocol_runtime::FieldId kN4RawBytes"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("std::optional<std::span<const std::byte, 3>> generated_4_raw_bytes() const {"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("std::optional<uint64_t> big_total() const {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("std::optional<int16_t> signed_short() const {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("std::optional<int32_t> signed_word() const {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("std::optional<int64_t> signed_long() const {"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find(
                "std::optional<universal_protocol_runtime::DecodedMessage> generated_7_body() const {"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("universal_protocol_runtime::ByteSpan generated_4_raw_bytes;"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("universal_protocol_runtime::DecodedMessage generated_7_body;"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find(
                "const auto generated_4_raw_bytes_value = source_message->get_fixed_bytes<3>(Fields::kN4RawBytes);"),
            std::string::npos);
  EXPECT_NE(
      cpp_generated.value().find(
          "value->generated_4_raw_bytes = universal_protocol_runtime::ByteSpan(generated_4_raw_bytes_value->data(), "
          "generated_4_raw_bytes_value->size());"),
      std::string::npos);
  EXPECT_NE(
      cpp_generated.value().find("const auto generated_7_body_value = source_message->get_struct(Fields::kN7Body);"),
      std::string::npos);
  EXPECT_NE(cpp_generated.value().find("value->generated_7_body = *generated_7_body_value;"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("FieldKind::kStruct"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("static constexpr bool kSupportsDirectValueDecode = false;"), std::string::npos);

  EXPECT_NE(python_generated.value().find("class N123Snapshot:"), std::string::npos);
  EXPECT_NE(python_generated.value().find("N_4_RAW_BYTES = 2"), std::string::npos);
}

TEST(BindingsGeneratorTest, GeneratesBindingsForCollectionsVariantsAndConditionalFields) {
  const upr::CompiledProtocol compiled =
      upr_test_support::compile_protocol_or_throw(make_advanced_codegen_definition());

  upr::StatusOr<std::string> cpp_generated = upr::generate_cpp_bindings_header(compiled);
  upr::StatusOr<std::string> python_generated = upr::generate_python_bindings_module(compiled);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  ASSERT_TRUE(python_generated.ok()) << python_generated.status().message();

  EXPECT_NE(cpp_generated.value().find("FieldKind::kCollection"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("FieldKind::kVariant"), std::string::npos);
  EXPECT_NE(
      cpp_generated.value().find("std::optional<universal_protocol_runtime::DecodedCollectionView> levels() const"),
      std::string::npos);
  EXPECT_NE(cpp_generated.value().find("std::optional<universal_protocol_runtime::DecodedMessage> detail() const"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("universal_protocol_runtime::DecodedCollectionView levels;"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("const auto levels_value = source_message->get_collection(Fields::kLevels);"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("const auto detail_value = source_message->get_variant(Fields::kDetail);"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("static constexpr bool kSupportsDirectValueDecode = false;"), std::string::npos);
  EXPECT_NE(python_generated.value().find("kind=\"collection\""), std::string::npos);
  EXPECT_NE(python_generated.value().find("kind=\"variant\""), std::string::npos);
}

TEST(BindingsGeneratorTest, GeneratesChecksumHelpersForBuiltInAlgorithms) {
  const upr::CompiledProtocol compiled =
      upr_test_support::compile_protocol_or_throw(make_checksum_codegen_definition());

  const auto cpp_generated = upr::generate_cpp_bindings_header(compiled);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  EXPECT_NE(cpp_generated.value().find("runtime_checksum_sum16"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("runtime_checksum_crc16_ccitt"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("runtime_checksum_crc32"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("runtime_checksum_crc32c"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("checksum_sum16_fixed<"), std::string::npos);
}

}  // namespace
