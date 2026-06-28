#include "universal_protocol_runtime/codegen/bindings_generator.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

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
                  upr_test_support::make_scalar_field("price64", upr::FieldKind::kFloat64, 8),
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

  upr::FieldDefinition detail_crc = upr_test_support::make_scalar_field("detail_crc", upr::FieldKind::kUnsigned, 4);
  upr_test_support::add_checksum(&detail_crc, "crc32");

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
           revision,
           detail_crc})},
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

upr::ProtocolDefinition make_simple_direct_exclusion_definition() {
  upr::FieldDefinition aligned = upr_test_support::make_scalar_field("aligned", upr::FieldKind::kUnsigned, 1);
  upr_test_support::set_alignment(&aligned, 4);

  return upr_test_support::make_protocol(
      "simple_direct_exclusion",
      {
          upr_test_support::make_message(
              "Validated",
              {upr_test_support::make_scalar_field("value", upr::FieldKind::kUnsigned, 1)},
              {upr_test_support::make_validation_against_value("value", upr::ValidationOperator::kEq, 1)}),
          upr_test_support::make_message(
              "Aligned", {upr_test_support::make_scalar_field("prefix", upr::FieldKind::kUnsigned, 1), aligned}),
          upr_test_support::make_message("Reserved",
                                         {upr_test_support::make_scalar_field("prefix", upr::FieldKind::kUnsigned, 1),
                                          upr_test_support::make_reserved_field("pad", 2, 0xAA)}),
      });
}

upr::ProtocolDefinition make_fallback_all_kinds_codegen_definition() {
  return upr_test_support::make_protocol(
      "fallback_all_kinds",
      {upr_test_support::make_message(
          "FallbackAllKinds",
          {
              upr_test_support::make_scalar_field(
                  "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 12),
              upr_test_support::make_scalar_field("validated", upr::FieldKind::kUnsigned, 1),
              upr_test_support::make_scalar_field("signed_value", upr::FieldKind::kSigned, 2),
              upr_test_support::make_scalar_field("price32", upr::FieldKind::kFloat32, 4),
              upr_test_support::make_scalar_field("price64", upr::FieldKind::kFloat64, 8),
              upr_test_support::make_fixed_bytes_field("raw", 2),
              upr_test_support::make_string_field("symbol", 3),
              upr_test_support::make_struct_field("body", "OuterBody"),
              upr_test_support::make_scalar_field("level_count", upr::FieldKind::kUnsigned, 1),
              upr_test_support::make_collection_field("levels", "Level", "level_count"),
              upr_test_support::make_scalar_field("kind", upr::FieldKind::kUnsigned, 1),
              upr_test_support::make_variant_field("detail",
                                                   "kind",
                                                   {{.tag_value = 1, .referenced_type = "QuoteDetail"},
                                                    {.tag_value = 2, .referenced_type = "TradeDetail"}}),
          },
          {upr_test_support::make_validation_against_value("validated", upr::ValidationOperator::kEq, 1)})},
      {
          upr_test_support::make_struct("InnerBody",
                                        {upr_test_support::make_scalar_field("id", upr::FieldKind::kUnsigned, 1)}),
          upr_test_support::make_struct(
              "OuterBody",
              {upr_test_support::make_struct_field("inner", "InnerBody"),
               upr_test_support::make_scalar_field("body_kind", upr::FieldKind::kUnsigned, 1),
               upr_test_support::make_variant_field("body_detail",
                                                    "body_kind",
                                                    {{.tag_value = 1, .referenced_type = "QuoteDetail"},
                                                     {.tag_value = 2, .referenced_type = "TradeDetail"}})}),
          upr_test_support::make_struct("Level",
                                        {upr_test_support::make_scalar_field("qty", upr::FieldKind::kUnsigned, 1)}),
          upr_test_support::make_struct("QuoteDetail",
                                        {upr_test_support::make_scalar_field("bid", upr::FieldKind::kUnsigned, 1)}),
          upr_test_support::make_struct("TradeDetail",
                                        {upr_test_support::make_scalar_field("size", upr::FieldKind::kUnsigned, 1)}),
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
  EXPECT_NE(generated.value().find("class View final {"), std::string::npos);
  EXPECT_NE(generated.value().find("class Decoder final {"), std::string::npos);
  EXPECT_NE(generated.value().find("struct Value final {"), std::string::npos);
  EXPECT_NE(generated.value().find("std::optional<float> price() const {"), std::string::npos);
  EXPECT_NE(generated.value().find("std::optional<double> price64() const {"), std::string::npos);
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
  EXPECT_NE(generated.value().find("return decode_value_direct(frame, value, nullptr, error);"), std::string::npos);
  EXPECT_NE(generated.value().find("universal_protocol_runtime::DecodeError* error = nullptr"), std::string::npos);
  EXPECT_NE(generated.value().find("static universal_protocol_runtime::DecodeStatus decode_sequence("),
            std::string::npos);
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

TEST(BindingsGeneratorTest, SimpleDirectPathExcludesLayoutsWithExtraSemantics) {
  const upr::CompiledProtocol compiled =
      upr_test_support::compile_protocol_or_throw(make_simple_direct_exclusion_definition());

  upr::StatusOr<std::string> generated = upr::generate_cpp_bindings_header(compiled);

  ASSERT_TRUE(generated.ok()) << generated.status().message();
  const std::string& text = generated.value();
  const std::size_t validated = text.find("struct Validated final {");
  const std::size_t aligned = text.find("struct Aligned final {");
  const std::size_t reserved = text.find("struct Reserved final {");
  ASSERT_NE(validated, std::string::npos);
  ASSERT_NE(aligned, std::string::npos);
  ASSERT_NE(reserved, std::string::npos);

  const std::size_t validated_direct =
      text.find("static constexpr bool kSupportsDirectValueDecode = false;", validated);
  EXPECT_LT(validated_direct, aligned);

  const std::size_t aligned_direct = text.find("static constexpr bool kSupportsDirectValueDecode = true;", aligned);
  const std::size_t aligned_align = text.find("const std::size_t aligned_offset", aligned);
  EXPECT_LT(aligned_direct, reserved);
  EXPECT_LT(aligned_align, reserved);

  const std::size_t reserved_direct = text.find("static constexpr bool kSupportsDirectValueDecode = true;", reserved);
  const std::size_t reserved_check = text.find("_reserved_byte", reserved);
  EXPECT_NE(reserved_direct, std::string::npos);
  EXPECT_NE(reserved_check, std::string::npos);
}

TEST(BindingsGeneratorTest, FallbackValueBindingsCoverAllFieldKinds) {
  const upr::CompiledProtocol compiled =
      upr_test_support::compile_protocol_or_throw(make_fallback_all_kinds_codegen_definition());

  upr::StatusOr<std::string> cpp_generated = upr::generate_cpp_bindings_header(compiled);
  upr::StatusOr<std::string> python_generated = upr::generate_python_bindings_module(compiled);
  upr::StatusOr<std::string> ts_generated = upr::generate_typescript_bindings_module(compiled);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  ASSERT_TRUE(python_generated.ok()) << python_generated.status().message();
  ASSERT_TRUE(ts_generated.ok()) << ts_generated.status().message();

  const std::string& cpp = cpp_generated.value();
  EXPECT_NE(cpp.find("static constexpr bool kSupportsDirectValueDecode = false;"), std::string::npos);
  EXPECT_NE(cpp.find("int16_t signed_value = 0;"), std::string::npos);
  EXPECT_NE(cpp.find("float price32 = 0.0;"), std::string::npos);
  EXPECT_NE(cpp.find("double price64 = 0.0;"), std::string::npos);
  EXPECT_NE(cpp.find("universal_protocol_runtime::ByteSpan raw;"), std::string::npos);
  EXPECT_NE(cpp.find("std::string_view symbol;"), std::string::npos);
  EXPECT_NE(cpp.find("universal_protocol_runtime::DecodedMessage body;"), std::string::npos);
  EXPECT_NE(cpp.find("universal_protocol_runtime::DecodedCollectionView levels;"), std::string::npos);
  EXPECT_NE(cpp.find("universal_protocol_runtime::DecodedMessage detail;"), std::string::npos);

  EXPECT_NE(python_generated.value().find("body: Optional[\"OuterBody\"] = None"), std::string::npos);
  EXPECT_NE(python_generated.value().find("levels: List[\"Level\"] = _field(default_factory=list)"), std::string::npos);
  EXPECT_NE(ts_generated.value().find("body?: OuterBody;"), std::string::npos);
  EXPECT_NE(ts_generated.value().find("levels?: Level[];"), std::string::npos);
}

TEST(BindingsGeneratorTest, GeneratesPythonBindingsWithMetadataClasses) {
  const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(make_codegen_definition());

  upr::StatusOr<std::string> generated = upr::generate_python_bindings_module(compiled);

  ASSERT_TRUE(generated.ok()) << generated.status().message();
  EXPECT_NE(generated.value().find("from universal_protocol_runtime import Codec as _Codec"), std::string::npos);
  EXPECT_NE(generated.value().find("from universal_protocol_runtime import metadata as _md"), std::string::npos);
  EXPECT_NE(generated.value().find("PROTOCOL_NAME = \"feeds\""), std::string::npos);
  EXPECT_NE(generated.value().find("PROTOCOL = _md.Protocol("), std::string::npos);
  EXPECT_NE(generated.value().find("import _feeds_native as _native"), std::string::npos);
  EXPECT_NE(generated.value().find("class _NativeCodec(_Codec):"), std::string::npos);
  EXPECT_NE(generated.value().find("CODEC = _NativeCodec(PROTOCOL)"), std::string::npos);
  EXPECT_NE(generated.value().find("@dataclass"), std::string::npos);
  EXPECT_NE(generated.value().find("class Order:"), std::string::npos);
  EXPECT_NE(generated.value().find("class Fields:"), std::string::npos);
  EXPECT_NE(generated.value().find("MESSAGE_TYPE = 0"), std::string::npos);
  EXPECT_NE(generated.value().find("dispatch_prefix=b\"\\x01\""), std::string::npos);
  EXPECT_NE(generated.value().find("_md.Field(id=2, name=\"price\", kind=\"float32\""), std::string::npos);
  EXPECT_NE(generated.value().find("_md.Checksum(field_id=3, result_width_bytes=1, algorithm_name=\"xor8\""),
            std::string::npos);
  EXPECT_NE(generated.value().find("CODEC.register_dataclass(\"Order\", Order)"), std::string::npos);
  EXPECT_NE(generated.value().find("def encode(self) -> bytes:"), std::string::npos);
}

TEST(BindingsGeneratorTest, GeneratesTypescriptBindingsWithMetadataLiterals) {
  const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(make_codegen_definition());

  upr::StatusOr<std::string> generated = upr::generate_typescript_bindings_module(compiled);

  ASSERT_TRUE(generated.ok()) << generated.status().message();
  EXPECT_NE(generated.value().find("import { Codec } from \"universal-protocol-runtime\""), std::string::npos);
  EXPECT_NE(generated.value().find("import type { Protocol } from \"universal-protocol-runtime\""), std::string::npos);
  EXPECT_NE(generated.value().find("export const PROTOCOL_NAME = \"feeds\""), std::string::npos);
  EXPECT_NE(generated.value().find("export const PROTOCOL: Protocol = {"), std::string::npos);
  EXPECT_NE(generated.value().find("export const CODEC = new Codec(PROTOCOL)"), std::string::npos);
  EXPECT_NE(generated.value().find("export interface Order {"), std::string::npos);
  EXPECT_NE(generated.value().find("dispatchPrefix: new Uint8Array([0x01])"), std::string::npos);
  EXPECT_NE(generated.value().find("{ id: 2, name: \"price\", kind: \"float32\""), std::string::npos);
  EXPECT_NE(generated.value().find("{ fieldId: 3, resultWidthBytes: 1, algorithmName: \"xor8\""), std::string::npos);
  EXPECT_NE(generated.value().find("encode(value: Order): Uint8Array"), std::string::npos);
  EXPECT_NE(generated.value().find("decodeSequence(frame: Uint8Array): Order[]"), std::string::npos);
}

TEST(BindingsGeneratorTest, SupportsCustomTypescriptRuntimeImport) {
  const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(make_codegen_definition());

  upr::StatusOr<std::string> generated =
      upr::generate_typescript_bindings_module(compiled, {.runtime_import = "../dist/index.js"});

  ASSERT_TRUE(generated.ok()) << generated.status().message();
  EXPECT_NE(generated.value().find("import { Codec } from \"../dist/index.js\""), std::string::npos);
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
  upr::StatusOr<std::string> ts_generated = upr::generate_typescript_bindings_module(compiled);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  ASSERT_TRUE(python_generated.ok()) << python_generated.status().message();
  ASSERT_TRUE(ts_generated.ok()) << ts_generated.status().message();
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
      upr::generate_python_bindings_module(compiled,
                                           {
                                               .module_name = "Custom Module 9",
                                               .native_module_name = "",
                                               .native_header_include = "",
                                           });

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
  EXPECT_NE(python_generated.value().find("dispatch_prefix=b\"\""), std::string::npos);
  EXPECT_NE(python_generated.value().find("CODEC.register_dataclass(\"GeneratedBinding\", GeneratedBinding)"),
            std::string::npos);
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
  upr::StatusOr<std::string> ts_generated = upr::generate_typescript_bindings_module(compiled);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  ASSERT_TRUE(python_generated.ok()) << python_generated.status().message();
  ASSERT_TRUE(ts_generated.ok()) << ts_generated.status().message();
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
  // Fixed bytes stay as borrowed spans; nested struct fields become owned
  // values in the general direct path.
  EXPECT_NE(cpp_generated.value().find("universal_protocol_runtime::ByteSpan generated_4_raw_bytes;"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("structs::N1NestedBody::Value generated_7_body;"), std::string::npos);
  EXPECT_NE(
      cpp_generated.value().find("value->generated_4_raw_bytes = frame.subspan(offset, generated_4_raw_bytes_size);"),
      std::string::npos);
  EXPECT_NE(cpp_generated.value().find(
                "structs::N1NestedBody::decode_value_direct(frame.subspan(offset), &value->generated_7_body,"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("structs::N1NestedBody::encode_value_direct(value.generated_7_body,"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("FieldKind::kStruct"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("static constexpr bool kSupportsDirectValueDecode = true;"), std::string::npos);

  EXPECT_NE(python_generated.value().find("class N123Snapshot:"), std::string::npos);
  EXPECT_NE(python_generated.value().find("N_4_RAW_BYTES = 2"), std::string::npos);
}

TEST(BindingsGeneratorTest, GeneratesBindingsForCollectionsVariantsAndConditionalFields) {
  const upr::CompiledProtocol compiled =
      upr_test_support::compile_protocol_or_throw(make_advanced_codegen_definition());

  upr::StatusOr<std::string> cpp_generated = upr::generate_cpp_bindings_header(compiled);
  upr::StatusOr<std::string> python_generated = upr::generate_python_bindings_module(compiled);
  upr::StatusOr<std::string> ts_generated = upr::generate_typescript_bindings_module(compiled);

  ASSERT_TRUE(cpp_generated.ok()) << cpp_generated.status().message();
  ASSERT_TRUE(python_generated.ok()) << python_generated.status().message();
  ASSERT_TRUE(ts_generated.ok()) << ts_generated.status().message();

  EXPECT_NE(cpp_generated.value().find("FieldKind::kCollection"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("FieldKind::kVariant"), std::string::npos);
  // Borrowed View accessors are unchanged.
  EXPECT_NE(
      cpp_generated.value().find("std::optional<universal_protocol_runtime::DecodedCollectionView> levels() const"),
      std::string::npos);
  EXPECT_NE(cpp_generated.value().find("std::optional<universal_protocol_runtime::DecodedMessage> detail() const"),
            std::string::npos);
  // The general direct path materializes owned collections and variants.
  EXPECT_NE(cpp_generated.value().find("std::vector<structs::Level::Value> levels;"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find(
                "std::variant<std::monostate, structs::QuoteDetail::Value, structs::TradeDetail::Value> detail;"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("value->levels.push_back(std::move(levels_element));"), std::string::npos);
  EXPECT_NE(cpp_generated.value().find("structs::Level::decode_value_direct(frame.subspan(offset),"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("value->detail.emplace<1>("), std::string::npos);
  // Presence- and condition-gated fields are emitted inline.
  EXPECT_NE(cpp_generated.value().find("const bool note_present = (((static_cast<uint64_t>(value->presence)"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("const bool revision_present = (static_cast<uint64_t>(value->kind) == 2ULL)"),
            std::string::npos);
  EXPECT_NE(cpp_generated.value().find("static constexpr bool kSupportsDirectValueDecode = true;"), std::string::npos);
  EXPECT_NE(python_generated.value().find("kind=\"collection\""), std::string::npos);
  EXPECT_NE(python_generated.value().find("kind=\"variant\""), std::string::npos);
  EXPECT_NE(ts_generated.value().find("detail?: QuoteDetail | TradeDetail;"), std::string::npos);
}

TEST(BindingsGeneratorTest, GeneratesPythonNativeExtensionModuleAcrossProtocolShapes) {
  const std::vector<upr::ProtocolDefinition> definitions = {
      make_codegen_definition(),
      make_numeric_codegen_definition(),
      make_advanced_codegen_definition(),
      make_checksum_codegen_definition(),
      make_fallback_all_kinds_codegen_definition(),
  };

  for (const upr::ProtocolDefinition& definition : definitions) {
    const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(definition);

    upr::StatusOr<std::string> native = upr::generate_python_native_extension_module(compiled);

    ASSERT_TRUE(native.ok()) << native.status().message();
    const std::string& text = native.value();
    EXPECT_NE(text.find("#include <pybind11/pybind11.h>"), std::string::npos);
    EXPECT_NE(text.find("std::optional<universal_protocol_runtime::ByteSpan> buffer_span("), std::string::npos);
    EXPECT_NE(text.find("py::object encode(std::string_view name, py::object values) {"), std::string::npos);
    EXPECT_NE(text.find("py::object decode(std::string_view name, PyObject* frame) {"), std::string::npos);
    EXPECT_NE(text.find("py::object decode_sequence(std::string_view name, PyObject* frame) {"), std::string::npos);
    EXPECT_NE(text.find("PyMethodDef kMethods[] = {"), std::string::npos);
    EXPECT_NE(text.find("set_encode_error(\"layout not found\");"), std::string::npos);
    EXPECT_NE(text.find("set_decode_status(universal_protocol_runtime::DecodeStatus::kMessageNotFound);"),
              std::string::npos);
  }
}

TEST(BindingsGeneratorTest, NativeExtensionEmitsConvertersForEveryFieldKind) {
  const upr::CompiledProtocol compiled =
      upr_test_support::compile_protocol_or_throw(make_fallback_all_kinds_codegen_definition());

  upr::StatusOr<std::string> native = upr::generate_python_native_extension_module(compiled);

  ASSERT_TRUE(native.ok()) << native.status().message();
  const std::string& text = native.value();
  EXPECT_NE(text.find("PYBIND11_MODULE(_fallback_all_kinds_native, m)"), std::string::npos);
  // Scalars cast straight from the Python object and back via py::cast.
  EXPECT_NE(text.find("value.signed_value = signed_value_object.cast<int16_t>();"), std::string::npos);
  EXPECT_NE(text.find("result[\"signed_value\"] = py::cast(value.signed_value);"), std::string::npos);
  // Bytes and strings borrow into the storage deque and re-materialize as py::bytes / py::str.
  EXPECT_NE(text.find("storage.emplace_back(py::bytes(raw_object));"), std::string::npos);
  EXPECT_NE(text.find("result[\"raw\"] = py::bytes(reinterpret_cast<const char*>(value.raw"), std::string::npos);
  EXPECT_NE(text.find("storage.emplace_back(py::str(symbol_object));"), std::string::npos);
  EXPECT_NE(text.find("result[\"symbol\"] = py::str(std::string(value.symbol"), std::string::npos);
  // Nested structs recurse through the dedicated converters (names are lower-cased, joined identifiers).
  EXPECT_NE(text.find("value.body = value_from_py_outerbody(body_object, storage);"), std::string::npos);
  EXPECT_NE(text.find("result[\"body\"] = value_to_py_outerbody(value.body);"), std::string::npos);
  // Collections iterate the Python sequence and rebuild a py::list.
  EXPECT_NE(text.find(".push_back(value_from_py_level("), std::string::npos);
  EXPECT_NE(text.find("levels_items.append(value_to_py_level(item));"), std::string::npos);
  // Variants dispatch on the tag field and emplace the active alternative.
  EXPECT_NE(text.find("switch (static_cast<uint64_t>(value.kind)) {"), std::string::npos);
  EXPECT_NE(text.find(".emplace<1>(value_from_py_quotedetail("), std::string::npos);
  EXPECT_NE(text.find("result[\"detail\"] = value_to_py_quotedetail(std::get<1>(value.detail));"), std::string::npos);
}

TEST(BindingsGeneratorTest, NativeExtensionDerivesLengthsCountsAndGatesPresence) {
  const upr::CompiledProtocol compiled =
      upr_test_support::compile_protocol_or_throw(make_advanced_codegen_definition());

  upr::StatusOr<std::string> native = upr::generate_python_native_extension_module(compiled);

  ASSERT_TRUE(native.ok()) << native.status().message();
  const std::string& text = native.value();
  // Length and count fields are derived from the payload sizes on the way in.
  EXPECT_NE(text.find("value.note_len = static_cast<decltype(value.note_len)>(value.note.size());"), std::string::npos);
  EXPECT_NE(text.find("value.level_count = static_cast<decltype(value.level_count)>(value.levels.size());"),
            std::string::npos);
  // Presence- and condition-gated fields are only converted back to Python when present.
  EXPECT_NE(text.find("static_cast<uint64_t>(value.presence)"), std::string::npos);
  EXPECT_NE(text.find("static_cast<uint64_t>(value.kind) == 2ULL"), std::string::npos);
  // The dispatch table routes by message name into the generated layout.
  EXPECT_NE(text.find("if (name == \"Snapshot\") {"), std::string::npos);
  EXPECT_NE(text.find("encode_layout<gen::messages::Snapshot>("), std::string::npos);
  EXPECT_NE(text.find("case 1ULL:"), std::string::npos);
}

TEST(BindingsGeneratorTest, NativeExtensionHonoursCustomModuleAndHeaderOptions) {
  const upr::CompiledProtocol compiled = upr_test_support::compile_protocol_or_throw(make_codegen_definition());

  upr::PythonBindingsOptions options;
  options.native_module_name = "custom native 7";
  options.native_header_include = "some/dir/custom_codec.hpp";
  upr::StatusOr<std::string> native = upr::generate_python_native_extension_module(compiled, options);

  ASSERT_TRUE(native.ok()) << native.status().message();
  EXPECT_NE(native.value().find("PYBIND11_MODULE(custom_native_7, m)"), std::string::npos);
  EXPECT_NE(native.value().find("#include \"some/dir/custom_codec.hpp\""), std::string::npos);
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
