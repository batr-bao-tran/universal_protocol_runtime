#include "universal_protocol_runtime/decoder/protocol_decoder.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "detail/test_support.hpp"
#include "universal_protocol_runtime/compiler/schema_compiler.hpp"

namespace upr = universal_protocol_runtime;

namespace {

upr::CompiledProtocol make_decoder_protocol() {
  upr::FieldDefinition header =
      upr_test_support::make_scalar_field("header", upr::FieldKind::kUnsigned, 2, upr::ByteOrder::kBigEndian);
  upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("version", 13, 3));
  upr_test_support::add_bit_field(&header, upr_test_support::make_bit_field("urgent", 12, 1));
  upr_test_support::add_bit_field(
      &header, upr_test_support::make_bit_field("kind", 0, 12, false, {{.value = 0x234, .name = "Order"}}));

  upr::FieldDefinition crc = upr_test_support::make_scalar_field("crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&crc, "xor8");

  upr::FieldDefinition signed_flags = upr_test_support::make_scalar_field("flags", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_bit_field(&signed_flags, upr_test_support::make_bit_field("delta", 4, 4, true));

  upr::FieldDefinition tail_crc = upr_test_support::make_scalar_field("crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&tail_crc, "xor8", "after_self", "frame_end");

  upr::FieldDefinition range_crc = upr_test_support::make_scalar_field("crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&range_crc, "xor8", "payload.start", "tail.end");

  return upr_test_support::compile_protocol_or_throw(upr_test_support::make_protocol(
      "decoder",
      {
          upr_test_support::make_message(
              "Metrics",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 1),
                  header,
                  upr_test_support::make_scalar_field("small_unsigned", upr::FieldKind::kUnsigned, 1),
                  upr_test_support::make_scalar_field(
                      "large_unsigned", upr::FieldKind::kUnsigned, 2, upr::ByteOrder::kBigEndian),
                  upr_test_support::make_scalar_field("signed_small", upr::FieldKind::kSigned, 1),
                  upr_test_support::make_scalar_field("signed_large", upr::FieldKind::kSigned, 8),
                  upr_test_support::make_scalar_field(
                      "positive_signed_be", upr::FieldKind::kSigned, 2, upr::ByteOrder::kBigEndian),
                  upr_test_support::make_scalar_field("float32_value", upr::FieldKind::kFloat32, 4),
                  upr_test_support::make_scalar_field(
                      "float64_value", upr::FieldKind::kFloat64, 8, upr::ByteOrder::kBigEndian),
                  upr_test_support::make_enum_field(
                      "side", 1, {{.value = 1, .name = "Buy"}, {.value = 2, .name = "Sell"}}),
                  upr_test_support::make_enum_field("unknown_side", 1, {{.value = 1, .name = "Known"}}),
                  upr_test_support::make_string_field("symbol", 4),
              }),
          upr_test_support::make_message(
              "Blob",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 2),
                  upr_test_support::make_scalar_field("length", upr::FieldKind::kUnsigned, 1),
                  upr_test_support::make_dynamic_bytes_field("payload", "length"),
              }),
          upr_test_support::make_message(
              "Trailing",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 3),
                  upr_test_support::make_scalar_field("count", upr::FieldKind::kUnsigned, 1),
              },
              true),
          upr_test_support::make_message(
              "Checksummed",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 4),
                  upr_test_support::make_scalar_field("value", upr::FieldKind::kUnsigned, 1),
                  crc,
              }),
          upr_test_support::make_message(
              "Text",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 5),
                  upr_test_support::make_scalar_field("length", upr::FieldKind::kUnsigned, 1),
                  upr_test_support::make_dynamic_string_field("text", "length", upr::StringEncoding::kUtf8),
              }),
          upr_test_support::make_message(
              "NestedOrder",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 6),
                  upr_test_support::make_struct_field("order", "OrderBody"),
              }),
          upr_test_support::make_message(
              "NestedLabel",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 7),
                  upr_test_support::make_struct_field("label", "Label"),
              }),
          upr_test_support::make_message(
              "SignedBits",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 8),
                  signed_flags,
              }),
          upr_test_support::make_message(
              "TailChecksummed",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 9),
                  tail_crc,
                  upr_test_support::make_scalar_field("tail", upr::FieldKind::kUnsigned, 1),
              }),
          upr_test_support::make_message(
              "RangedChecksummed",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 10),
                  upr_test_support::make_scalar_field("payload", upr::FieldKind::kUnsigned, 1),
                  upr_test_support::make_scalar_field("tail", upr::FieldKind::kUnsigned, 1),
                  range_crc,
              }),
      },
      {
          upr_test_support::make_struct("OrderBody",
                                        {
                                            upr_test_support::make_scalar_field("price", upr::FieldKind::kUnsigned, 4),
                                            upr_test_support::make_scalar_field("qty", upr::FieldKind::kUnsigned, 4),
                                        }),
          upr_test_support::make_struct(
              "Label",
              {
                  upr_test_support::make_scalar_field("text_len", upr::FieldKind::kUnsigned, 1),
                  upr_test_support::make_dynamic_string_field("text", "text_len", upr::StringEncoding::kUtf8),
              }),
      }));
}

std::vector<std::byte> make_metrics_frame() {
  std::vector<std::byte> frame;
  upr_test_support::append_integral<uint8_t>(frame, 1, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<uint16_t>(frame, 0xB234, upr::ByteOrder::kBigEndian);
  upr_test_support::append_integral<uint8_t>(frame, 42, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<uint16_t>(frame, 300, upr::ByteOrder::kBigEndian);
  upr_test_support::append_integral<int8_t>(frame, -7, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<int64_t>(
      frame, static_cast<int64_t>(-1234567890123LL), upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<int16_t>(frame, 321, upr::ByteOrder::kBigEndian);
  upr_test_support::append_float32(frame, 1.25F, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_float64(frame, -2.5, upr::ByteOrder::kBigEndian);
  upr_test_support::append_integral<uint8_t>(frame, 2, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<uint8_t>(frame, 9, upr::ByteOrder::kLittleEndian);
  frame.push_back(std::byte{'A'});
  frame.push_back(std::byte{'B'});
  frame.push_back(std::byte{'C'});
  frame.push_back(std::byte{'D'});
  return frame;
}

std::vector<std::byte> make_blob_frame(uint8_t payload_length, std::initializer_list<uint8_t> payload) {
  std::vector<std::byte> frame;
  upr_test_support::append_integral<uint8_t>(frame, 2, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<uint8_t>(frame, payload_length, upr::ByteOrder::kLittleEndian);
  for (const uint8_t value : payload) {
    frame.push_back(std::byte{value});
  }
  return frame;
}

std::vector<std::byte> make_trailing_frame() { return upr_test_support::make_bytes({0x03, 0x05, 0xAA, 0xBB}); }

std::vector<std::byte> make_checksummed_frame(uint8_t value) {
  const auto checksum = static_cast<uint8_t>(0x04U ^ value);
  return upr_test_support::make_bytes({0x04, value, checksum});
}

std::vector<std::byte> make_text_frame(std::initializer_list<uint8_t> text_bytes) {
  std::vector<std::byte> frame;
  upr_test_support::append_integral<uint8_t>(frame, 5, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<uint8_t>(
      frame, static_cast<uint8_t>(text_bytes.size()), upr::ByteOrder::kLittleEndian);
  for (const uint8_t value : text_bytes) {
    frame.push_back(std::byte{value});
  }
  return frame;
}

std::vector<std::byte> make_nested_order_frame(uint32_t price, uint32_t qty) {
  std::vector<std::byte> frame;
  upr_test_support::append_integral<uint8_t>(frame, 6, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<uint32_t>(frame, price, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<uint32_t>(frame, qty, upr::ByteOrder::kLittleEndian);
  return frame;
}

std::vector<std::byte> make_nested_label_frame(std::initializer_list<uint8_t> utf8_bytes) {
  std::vector<std::byte> frame;
  upr_test_support::append_integral<uint8_t>(frame, 7, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<uint8_t>(
      frame, static_cast<uint8_t>(utf8_bytes.size()), upr::ByteOrder::kLittleEndian);
  for (const uint8_t value : utf8_bytes) {
    frame.push_back(std::byte{value});
  }
  return frame;
}

std::vector<std::byte> make_signed_bits_frame(uint8_t flags) { return upr_test_support::make_bytes({0x08, flags}); }

std::vector<std::byte> make_tail_checksummed_frame(uint8_t tail) {
  return upr_test_support::make_bytes({0x09, tail, tail});
}

std::vector<std::byte> make_ranged_checksummed_frame(uint8_t payload, uint8_t tail) {
  return upr_test_support::make_bytes({0x0A, payload, tail, static_cast<uint8_t>(payload ^ tail)});
}

struct InvalidUtf8Case {
  std::string name;
  std::vector<std::byte> frame;
};

template <typename T>
class UnsignedDecodedMessageTypedTest : public ::testing::Test {
 protected:
  ~UnsignedDecodedMessageTypedTest() noexcept override = default;

  upr::CompiledProtocol protocol_ = make_decoder_protocol();
  upr::ProtocolDecoder decoder_{protocol_};
  std::vector<std::byte> frame_ = make_metrics_frame();
  upr::DecodedMessage message_;

  void SetUp() override {
    ASSERT_EQ(decoder_.decode_as("Metrics", upr::ByteSpan(frame_.data(), frame_.size()), &message_),
              upr::DecodeStatus::kOk);
  }
};

using UnsignedTypes = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(UnsignedDecodedMessageTypedTest, UnsignedTypes);

TYPED_TEST(UnsignedDecodedMessageTypedTest, ConvertsUnsignedValuesToRequestedType) {
  EXPECT_EQ(this->message_.template get<TypeParam>("small_unsigned"), static_cast<TypeParam>(42));
}

template <typename T>
class SignedDecodedMessageTypedTest : public ::testing::Test {
 protected:
  ~SignedDecodedMessageTypedTest() noexcept override = default;

  upr::CompiledProtocol protocol_ = make_decoder_protocol();
  upr::ProtocolDecoder decoder_{protocol_};
  std::vector<std::byte> frame_ = make_metrics_frame();
  upr::DecodedMessage message_;

  void SetUp() override {
    ASSERT_EQ(decoder_.decode_as("Metrics", upr::ByteSpan(frame_.data(), frame_.size()), &message_),
              upr::DecodeStatus::kOk);
  }
};

using SignedTypes = ::testing::Types<int8_t, int16_t, int32_t, int64_t>;
TYPED_TEST_SUITE(SignedDecodedMessageTypedTest, SignedTypes);

TYPED_TEST(SignedDecodedMessageTypedTest, ConvertsSignedValuesToRequestedType) {
  EXPECT_EQ(this->message_.template get<TypeParam>("signed_small"), static_cast<TypeParam>(-7));
}

TEST(ProtocolDecoderTest, DecodesMixedFieldTypesAndSupportsAccessors) {
  const upr::CompiledProtocol protocol = make_decoder_protocol();
  upr::ProtocolDecoder decoder(protocol);
  const std::vector<std::byte> frame = make_metrics_frame();
  upr::DecodedMessage message;

  ASSERT_EQ(decoder.decode_any(upr::ByteSpan(frame.data(), frame.size()), &message), upr::DecodeStatus::kOk);
  ASSERT_TRUE(message.valid());
  EXPECT_EQ(message.message_name(), "Metrics");
  ASSERT_NE(message.schema(), nullptr);
  EXPECT_EQ(message.schema()->name(), "Metrics");
  EXPECT_EQ(message.raw().size(), frame.size());

  EXPECT_EQ(message.get_unsigned("small_unsigned"), 42U);
  EXPECT_EQ(message.get_unsigned("large_unsigned"), 300U);
  EXPECT_EQ(message.get_signed("signed_small"), -7);
  EXPECT_EQ(message.get_signed("positive_signed_be"), 321);
  ASSERT_TRUE(message.get_float32("float32_value").has_value());
  EXPECT_FLOAT_EQ(*message.get_float32("float32_value"), 1.25F);
  ASSERT_TRUE(message.get_float64("float64_value").has_value());
  EXPECT_DOUBLE_EQ(*message.get_float64("float64_value"), -2.5);
  ASSERT_TRUE(message.get_enum_name("side").has_value());
  EXPECT_EQ(*message.get_enum_name("side"), "Sell");
  ASSERT_TRUE(message.get_string_view("symbol").has_value());
  EXPECT_EQ(*message.get_string_view("symbol"), "ABCD");
  const auto fixed_symbol = message.get_fixed_string<4>("symbol");
  ASSERT_TRUE(fixed_symbol.has_value());
  EXPECT_EQ((*fixed_symbol)[0], 'A');
  EXPECT_EQ((*fixed_symbol)[3], 'D');
  const auto fixed_header_bytes = message.get_fixed_bytes<2>("header");
  ASSERT_TRUE(fixed_header_bytes.has_value());
  EXPECT_EQ((*fixed_header_bytes)[0], std::byte{0xB2});
  EXPECT_EQ((*fixed_header_bytes)[1], std::byte{0x34});
  EXPECT_EQ(message.get_bit<uint8_t>("version"), 5U);
  EXPECT_EQ(message.get_bit<uint8_t>("urgent"), 1U);
  EXPECT_EQ(message.get_bit<uint16_t>("kind"), 0x234U);
  ASSERT_TRUE(message.get_bit_enum_name("kind").has_value());
  EXPECT_EQ(*message.get_bit_enum_name("kind"), "Order");
}

TEST(ProtocolDecoderTest, SupportsDynamicStringsChecksumsAndNestedStructViews) {
  const upr::CompiledProtocol protocol = make_decoder_protocol();
  upr::ProtocolDecoder decoder(protocol);

  const std::vector<std::byte> checksummed_frame = make_checksummed_frame(0xAA);
  upr::DecodedMessage checksummed;
  ASSERT_EQ(decoder.decode_any(upr::ByteSpan(checksummed_frame.data(), checksummed_frame.size()), &checksummed),
            upr::DecodeStatus::kOk);
  EXPECT_EQ(checksummed.message_name(), "Checksummed");
  EXPECT_EQ(checksummed.get<uint8_t>("value"), 0xAA);

  const std::vector<std::byte> text_frame = make_text_frame({0xE2, 0x82, 0xAC});
  upr::DecodedMessage text;
  ASSERT_EQ(decoder.decode_as("Text", upr::ByteSpan(text_frame.data(), text_frame.size()), &text),
            upr::DecodeStatus::kOk);
  ASSERT_TRUE(text.get_string_view("text").has_value());
  EXPECT_EQ(*text.get_string_view("text"), "\xE2\x82\xAC");

  const std::vector<std::byte> nested_order_frame = make_nested_order_frame(1234, 55);
  upr::DecodedMessage nested_order;
  ASSERT_EQ(decoder.decode_as(
                "NestedOrder", upr::ByteSpan(nested_order_frame.data(), nested_order_frame.size()), &nested_order),
            upr::DecodeStatus::kOk);
  const auto order = nested_order.get_struct("order");
  ASSERT_TRUE(order.has_value());
  EXPECT_EQ(order->message_name(), "OrderBody");
  EXPECT_EQ(order->get<uint32_t>("price"), 1234U);
  EXPECT_EQ(order->get<uint32_t>("qty"), 55U);

  const std::vector<std::byte> nested_label_frame = make_nested_label_frame({0xE2, 0x82, 0xAC});
  upr::DecodedMessage nested_label;
  ASSERT_EQ(decoder.decode_as(
                "NestedLabel", upr::ByteSpan(nested_label_frame.data(), nested_label_frame.size()), &nested_label),
            upr::DecodeStatus::kOk);
  const auto label = nested_label.get_struct("label");
  ASSERT_TRUE(label.has_value());
  ASSERT_TRUE(label->get_string_view("text").has_value());
  EXPECT_EQ(*label->get_string_view("text"), "\xE2\x82\xAC");

  const std::vector<std::byte> signed_bits_frame = make_signed_bits_frame(0xF2);
  upr::DecodedMessage signed_bits;
  ASSERT_EQ(
      decoder.decode_as("SignedBits", upr::ByteSpan(signed_bits_frame.data(), signed_bits_frame.size()), &signed_bits),
      upr::DecodeStatus::kOk);
  EXPECT_EQ(signed_bits.get_bit<int8_t>("delta"), -1);

  const std::vector<std::byte> tail_checksummed_frame = make_tail_checksummed_frame(0x5A);
  upr::DecodedMessage tail_checksummed;
  ASSERT_EQ(decoder.decode_as("TailChecksummed",
                              upr::ByteSpan(tail_checksummed_frame.data(), tail_checksummed_frame.size()),
                              &tail_checksummed),
            upr::DecodeStatus::kOk);
  EXPECT_EQ(tail_checksummed.get<uint8_t>("tail"), 0x5A);

  const std::vector<std::byte> ranged_checksummed_frame = make_ranged_checksummed_frame(0x11, 0x22);
  upr::DecodedMessage ranged_checksummed;
  ASSERT_EQ(decoder.decode_as("RangedChecksummed",
                              upr::ByteSpan(ranged_checksummed_frame.data(), ranged_checksummed_frame.size()),
                              &ranged_checksummed),
            upr::DecodeStatus::kOk);
  EXPECT_EQ(ranged_checksummed.get<uint8_t>("payload"), 0x11);
  EXPECT_EQ(ranged_checksummed.get<uint8_t>("tail"), 0x22);
}

TEST(ProtocolDecoderTest, SupportsDynamicBytesAndTrailingMessages) {
  const upr::CompiledProtocol protocol = make_decoder_protocol();
  upr::ProtocolDecoder decoder(protocol);

  const std::vector<std::byte> blob_frame = make_blob_frame(3, {0xAA, 0xBB, 0xCC});
  upr::DecodedMessage blob;
  ASSERT_EQ(decoder.decode_any(upr::ByteSpan(blob_frame.data(), blob_frame.size()), &blob), upr::DecodeStatus::kOk);
  EXPECT_EQ(blob.message_name(), "Blob");
  EXPECT_EQ(blob.get<uint8_t>("length"), 3U);
  const auto payload = blob.get_bytes("payload");
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ((*payload)[1], std::byte{0xBB});

  const std::vector<std::byte> trailing_frame = make_trailing_frame();
  upr::DecodedMessage trailing;
  ASSERT_EQ(decoder.decode_as("Trailing", upr::ByteSpan(trailing_frame.data(), trailing_frame.size()), &trailing),
            upr::DecodeStatus::kOk);
  EXPECT_EQ(trailing.get<uint8_t>("count"), 5U);
}

TEST(ProtocolDecoderTest, ReturnsNulloptForWrongKindsMissingNamesAndOverflow) {
  const upr::CompiledProtocol protocol = make_decoder_protocol();
  upr::ProtocolDecoder decoder(protocol);
  const std::vector<std::byte> frame = make_metrics_frame();
  upr::DecodedMessage message;

  ASSERT_EQ(decoder.decode_as("Metrics", upr::ByteSpan(frame.data(), frame.size()), &message), upr::DecodeStatus::kOk);

  EXPECT_FALSE(message.get_unsigned("missing").has_value());
  EXPECT_FALSE(message.get_signed("missing").has_value());
  EXPECT_FALSE(message.get_float32("missing").has_value());
  EXPECT_FALSE(message.get_float64("missing").has_value());
  EXPECT_FALSE(message.get_bytes("missing").has_value());
  EXPECT_FALSE(message.get_string_view("small_unsigned").has_value());
  EXPECT_FALSE(message.get_enum_name("missing").has_value());
  EXPECT_FALSE(message.get_struct("small_unsigned").has_value());
  EXPECT_FALSE(message.get_struct("missing").has_value());
  EXPECT_FALSE(message.get<uint8_t>("large_unsigned").has_value());
  EXPECT_FALSE(message.get<int8_t>("signed_large").has_value());
  EXPECT_FALSE(message.get<long double>("float32_value").has_value());
  EXPECT_FALSE(message.get_signed("small_unsigned").has_value());
  EXPECT_FALSE(message.get_float32("small_unsigned").has_value());
  EXPECT_FALSE(message.get_float64("small_unsigned").has_value());
  EXPECT_FALSE(message.get_unsigned("symbol").has_value());
  EXPECT_FALSE(message.get_enum_name("small_unsigned").has_value());
  EXPECT_FALSE(message.get_unsigned(999).has_value());
  EXPECT_FALSE(message.get_bytes(999).has_value());
  EXPECT_FALSE(message.get_fixed_bytes<1>("missing").has_value());
  EXPECT_FALSE(message.get_fixed_bytes<4>("header").has_value());
  EXPECT_FALSE(message.get_fixed_string<1>("small_unsigned").has_value());
  EXPECT_FALSE(message.get_fixed_string<1>("missing").has_value());
  EXPECT_FALSE(message.get_fixed_string<8>("symbol").has_value());
  EXPECT_FALSE(message.get<int32_t>("missing").has_value());
  EXPECT_FALSE(message.get_enum_name("unknown_side").has_value());
  EXPECT_FALSE(message.get_bit_unsigned("missing").has_value());
  EXPECT_FALSE(message.get_bit<unsigned char>("missing").has_value());
  EXPECT_FALSE(message.get_bit_signed("kind").has_value());
  EXPECT_FALSE(message.get_bit_signed("missing").has_value());
  EXPECT_FALSE(message.get_bit_enum_name("missing").has_value());
  EXPECT_FALSE(message.get_bit<uint8_t>("kind").has_value());
}

TEST(ProtocolDecoderTest, DecodesManualScalarWidthsAcrossLittleAndBigEndianLayouts) {
  std::vector<upr::CompiledField> fields;
  auto add_field = [&](std::string name, uint8_t width, upr::ByteOrder order) {
    upr::CompiledField field;
    field.id = static_cast<upr::FieldId>(fields.size());
    field.name = std::move(name);
    field.kind = upr::FieldKind::kUnsigned;
    field.width_bytes = width;
    field.byte_order = order;
    fields.push_back(std::move(field));
  };

  add_field("u16_le", 2, upr::ByteOrder::kLittleEndian);
  add_field("u24_le", 3, upr::ByteOrder::kLittleEndian);
  add_field("u40_le", 5, upr::ByteOrder::kLittleEndian);
  add_field("u48_le", 6, upr::ByteOrder::kLittleEndian);
  add_field("u56_le", 7, upr::ByteOrder::kLittleEndian);
  add_field("u8_be", 1, upr::ByteOrder::kBigEndian);
  add_field("u16_be", 2, upr::ByteOrder::kBigEndian);
  add_field("u24_be", 3, upr::ByteOrder::kBigEndian);
  add_field("u32_be", 4, upr::ByteOrder::kBigEndian);
  add_field("u40_be", 5, upr::ByteOrder::kBigEndian);
  add_field("u48_be", 6, upr::ByteOrder::kBigEndian);
  add_field("u56_be", 7, upr::ByteOrder::kBigEndian);

  std::vector<std::byte> frame;
  auto append_raw = [&](std::initializer_list<uint8_t> values) {
    for (const uint8_t value : values) {
      frame.push_back(std::byte{value});
    }
  };

  append_raw({0x34, 0x12});
  append_raw({0x56, 0x34, 0x12});
  append_raw({0x9A, 0x78, 0x56, 0x34, 0x12});
  append_raw({0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12});
  append_raw({0xDE, 0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12});
  append_raw({0x7F});
  append_raw({0x12, 0x34});
  append_raw({0x12, 0x34, 0x56});
  append_raw({0x12, 0x34, 0x56, 0x78});
  append_raw({0x12, 0x34, 0x56, 0x78, 0x9A});
  append_raw({0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC});
  append_raw({0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE});

  upr::CompiledMessage message_schema("Widths", std::move(fields), {}, {}, frame.size(), false);
  upr::CompiledProtocol protocol("widths", 55, {}, {message_schema});
  upr::ProtocolDecoder decoder(protocol);
  upr::DecodedMessage message;

  ASSERT_EQ(decoder.decode_as("Widths", upr::ByteSpan(frame.data(), frame.size()), &message), upr::DecodeStatus::kOk);
  EXPECT_EQ(message.get_unsigned("u16_le"), 0x1234U);
  EXPECT_EQ(message.get_unsigned("u24_le"), 0x123456U);
  EXPECT_EQ(message.get_unsigned("u40_le"), 0x123456789AU);
  EXPECT_EQ(message.get_unsigned("u48_le"), 0x123456789ABCU);
  EXPECT_EQ(message.get_unsigned("u56_le"), 0x123456789ABCDEULL);
  EXPECT_EQ(message.get_unsigned("u8_be"), 0x7FU);
  EXPECT_EQ(message.get_unsigned("u16_be"), 0x1234U);
  EXPECT_EQ(message.get_unsigned("u24_be"), 0x123456U);
  EXPECT_EQ(message.get_unsigned("u32_be"), 0x12345678U);
  EXPECT_EQ(message.get_unsigned("u40_be"), 0x123456789AU);
  EXPECT_EQ(message.get_unsigned("u48_be"), 0x123456789ABCU);
  EXPECT_EQ(message.get_unsigned("u56_be"), 0x123456789ABCDEULL);
}

TEST(ProtocolDecoderTest, HandlesTemplateOverflowAndMalformedManualLayoutsDefensively) {
  upr::CompiledField container;
  container.id = 0;
  container.name = "flags";
  container.kind = upr::FieldKind::kUnsigned;
  container.width_bytes = 2;

  upr::CompiledBitField signed_bit_field{
      .id = 0,
      .name = "wide_signed",
      .container_field_id = 0,
      .shift_bits = 0,
      .width_bits = 12,
      .mask = 0x0FFFU,
      .is_signed = true,
      .enum_values = {},
  };

  upr::CompiledMessage bits_message("Bits", {container}, {signed_bit_field}, {}, 2, false);
  upr::CompiledProtocol bits_protocol("bits", 77, {}, {bits_message});
  upr::ProtocolDecoder bits_decoder(bits_protocol);
  upr::DecodedMessage bits;
  const std::vector<std::byte> bit_frame = upr_test_support::make_bytes({0xC8, 0x00});
  ASSERT_EQ(bits_decoder.decode_as("Bits", upr::ByteSpan(bit_frame.data(), bit_frame.size()), &bits),
            upr::DecodeStatus::kOk);
  EXPECT_FALSE(bits.get_bit<int8_t>("wide_signed").has_value());

  upr::CompiledField short_field;
  short_field.id = 0;
  short_field.name = "short";
  short_field.kind = upr::FieldKind::kUnsigned;
  short_field.width_bytes = 3;
  upr::CompiledMessage short_message("Short", {short_field}, {}, {}, 0, false);
  upr::CompiledProtocol short_protocol("short", 78, {}, {short_message});
  upr::ProtocolDecoder short_decoder(short_protocol);
  upr::DecodedMessage short_decoded;
  const std::vector<std::byte> short_frame = upr_test_support::make_bytes({0x01, 0x02});
  EXPECT_EQ(short_decoder.decode_as("Short", upr::ByteSpan(short_frame.data(), short_frame.size()), &short_decoded),
            upr::DecodeStatus::kSchemaMismatch);
}

TEST(ProtocolDecoderTest, ReturnsNulloptForMalformedManualAccessorDefinitions) {
  upr::CompiledField wide_signed;
  wide_signed.id = 0;
  wide_signed.name = "wide_signed";
  wide_signed.kind = upr::FieldKind::kSigned;
  wide_signed.width_bytes = 9;

  upr::CompiledField wide_enum;
  wide_enum.id = 1;
  wide_enum.name = "wide_enum";
  wide_enum.kind = upr::FieldKind::kEnum;
  wide_enum.width_bytes = 9;
  wide_enum.enum_values = {{.value = 1, .name = "One"}};

  upr::CompiledField float_container;
  float_container.id = 2;
  float_container.name = "float_container";
  float_container.kind = upr::FieldKind::kFloat32;
  float_container.width_bytes = 4;

  upr::CompiledField enum_container;
  enum_container.id = 3;
  enum_container.name = "enum_container";
  enum_container.kind = upr::FieldKind::kUnsigned;
  enum_container.width_bytes = 1;

  upr::CompiledBitField float_bits{
      .id = 0,
      .name = "float_bits",
      .container_field_id = 2,
      .shift_bits = 0,
      .width_bits = 4,
      .mask = 0x0FU,
      .is_signed = false,
      .enum_values = {},
  };
  upr::CompiledBitField float_bits_signed{
      .id = 1,
      .name = "float_bits_signed",
      .container_field_id = 2,
      .shift_bits = 0,
      .width_bits = 4,
      .mask = 0x0FU,
      .is_signed = true,
      .enum_values = {},
  };
  upr::CompiledBitField float_bits_enum{
      .id = 2,
      .name = "float_bits_enum",
      .container_field_id = 2,
      .shift_bits = 0,
      .width_bits = 4,
      .mask = 0x0FU,
      .is_signed = false,
      .enum_values = {{.value = 1, .name = "One"}},
  };
  upr::CompiledBitField enum_bits{
      .id = 3,
      .name = "enum_bits",
      .container_field_id = 3,
      .shift_bits = 0,
      .width_bits = 4,
      .mask = 0x0FU,
      .is_signed = false,
      .enum_values = {{.value = 1, .name = "One"}},
  };

  upr::CompiledMessage message_schema("MalformedAccessors",
                                      {wide_signed, wide_enum, float_container, enum_container},
                                      {float_bits, float_bits_signed, float_bits_enum, enum_bits},
                                      {},
                                      23,
                                      false);
  upr::CompiledProtocol protocol("malformed_accessors", 79, {}, {message_schema});
  upr::ProtocolDecoder decoder(protocol);
  upr::DecodedMessage decoded;
  const std::vector<std::byte> frame = upr_test_support::make_bytes({
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
  });

  ASSERT_EQ(decoder.decode_as("MalformedAccessors", upr::ByteSpan(frame.data(), frame.size()), &decoded),
            upr::DecodeStatus::kOk);
  EXPECT_FALSE(decoded.get_signed("wide_signed").has_value());
  EXPECT_FALSE(decoded.get_enum_name("wide_enum").has_value());
  EXPECT_FALSE(decoded.get_bit_unsigned(99).has_value());
  EXPECT_FALSE(decoded.get_bit_unsigned("float_bits").has_value());
  EXPECT_FALSE(decoded.get_bit_signed("float_bits_signed").has_value());
  EXPECT_FALSE(decoded.get_bit_enum_name(99).has_value());
  EXPECT_FALSE(decoded.get_bit_enum_name("float_bits_enum").has_value());
  EXPECT_FALSE(decoded.get_bit_enum_name("enum_bits").has_value());
}

TEST(ProtocolDecoderTest, DecodeAnyHandlesMessagesWithoutDispatchPrefix) {
  const upr::CompiledProtocol protocol = upr_test_support::compile_protocol_or_throw(upr_test_support::make_protocol(
      "plain",
      {upr_test_support::make_message("Plain",
                                      {
                                          upr_test_support::make_scalar_field("value", upr::FieldKind::kUnsigned, 1),
                                          upr_test_support::make_scalar_field("count", upr::FieldKind::kUnsigned, 1),
                                      })}));
  upr::ProtocolDecoder decoder(protocol);
  const std::vector<std::byte> frame = upr_test_support::make_bytes({0x2A, 0x05});
  upr::DecodedMessage message;

  ASSERT_EQ(decoder.decode_any(upr::ByteSpan(frame.data(), frame.size()), &message), upr::DecodeStatus::kOk);
  EXPECT_EQ(message.message_name(), "Plain");
  EXPECT_EQ(message.get<uint8_t>("value"), 0x2A);
  EXPECT_EQ(message.get<uint8_t>("count"), 0x05);
}

TEST(ProtocolDecoderTest, ReportsDecodeFailuresForDifferentErrorCases) {
  const upr::CompiledProtocol protocol = make_decoder_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::DecodedMessage message;

  const std::vector<std::byte> unknown_frame = upr_test_support::make_bytes({0x0B, 0x01});
  EXPECT_EQ(decoder.decode_any(upr::ByteSpan(unknown_frame.data(), unknown_frame.size()), &message),
            upr::DecodeStatus::kMessageNotFound);

  const std::vector<std::byte> short_metrics_frame = upr_test_support::make_bytes({0x01, 0xB2});
  EXPECT_EQ(
      decoder.decode_as("Metrics", upr::ByteSpan(short_metrics_frame.data(), short_metrics_frame.size()), &message),
      upr::DecodeStatus::kSchemaMismatch);

  std::vector<std::byte> wrong_expect_frame = make_metrics_frame();
  wrong_expect_frame[0] = std::byte{0xFF};
  EXPECT_EQ(decoder.decode_as("Metrics", upr::ByteSpan(wrong_expect_frame.data(), wrong_expect_frame.size()), &message),
            upr::DecodeStatus::kSchemaMismatch);

  const std::vector<std::byte> overflow_blob_frame = make_blob_frame(5, {0xAA, 0xBB});
  EXPECT_EQ(decoder.decode_as("Blob", upr::ByteSpan(overflow_blob_frame.data(), overflow_blob_frame.size()), &message),
            upr::DecodeStatus::kInvalidData);

  const std::vector<std::byte> extra_trailing = upr_test_support::make_bytes({0x02, 0x01, 0xAA, 0xBB});
  EXPECT_EQ(decoder.decode_as("Blob", upr::ByteSpan(extra_trailing.data(), extra_trailing.size()), &message),
            upr::DecodeStatus::kSchemaMismatch);

  std::vector<std::byte> bad_checksum = make_checksummed_frame(0xAA);
  bad_checksum[2] = std::byte{0x00};
  EXPECT_EQ(decoder.decode_as("Checksummed", upr::ByteSpan(bad_checksum.data(), bad_checksum.size()), &message),
            upr::DecodeStatus::kChecksumMismatch);

  std::vector<std::byte> bad_tail_checksum = make_tail_checksummed_frame(0x5A);
  bad_tail_checksum[1] = std::byte{0x00};
  EXPECT_EQ(
      decoder.decode_as("TailChecksummed", upr::ByteSpan(bad_tail_checksum.data(), bad_tail_checksum.size()), &message),
      upr::DecodeStatus::kChecksumMismatch);

  std::vector<std::byte> bad_ranged_checksum = make_ranged_checksummed_frame(0x11, 0x22);
  bad_ranged_checksum[3] = std::byte{0x00};
  EXPECT_EQ(decoder.decode_as(
                "RangedChecksummed", upr::ByteSpan(bad_ranged_checksum.data(), bad_ranged_checksum.size()), &message),
            upr::DecodeStatus::kChecksumMismatch);

  const std::vector<std::byte> invalid_text = make_text_frame({0xC3, 0x28});
  EXPECT_EQ(decoder.decode_as("Text", upr::ByteSpan(invalid_text.data(), invalid_text.size()), &message),
            upr::DecodeStatus::kInvalidData);

  const std::vector<std::byte> invalid_ascii_metrics = [] {
    std::vector<std::byte> frame = make_metrics_frame();
    frame[31] = std::byte{0xFF};
    return frame;
  }();
  EXPECT_EQ(
      decoder.decode_as("Metrics", upr::ByteSpan(invalid_ascii_metrics.data(), invalid_ascii_metrics.size()), &message),
      upr::DecodeStatus::kInvalidData);

  const std::vector<std::byte> invalid_nested_label = make_nested_label_frame({0xC0, 0x80});
  EXPECT_EQ(decoder.decode_as(
                "NestedLabel", upr::ByteSpan(invalid_nested_label.data(), invalid_nested_label.size()), &message),
            upr::DecodeStatus::kInvalidData);

  EXPECT_EQ(decoder.decode_as("Missing", upr::ByteSpan{}, &message), upr::DecodeStatus::kMessageNotFound);
}

class InvalidUtf8ProtocolDecoderTest : public ::testing::TestWithParam<InvalidUtf8Case> {
 public:
  ~InvalidUtf8ProtocolDecoderTest() noexcept override = default;
};

TEST_P(InvalidUtf8ProtocolDecoderTest, RejectsMalformedUtf8Sequences) {
  const upr::CompiledProtocol protocol = make_decoder_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::DecodedMessage message;

  const InvalidUtf8Case& param = GetParam();

  EXPECT_EQ(decoder.decode_as("Text", upr::ByteSpan(param.frame.data(), param.frame.size()), &message),
            upr::DecodeStatus::kInvalidData)
      << param.name;
}

INSTANTIATE_TEST_SUITE_P(Coverage,
                         InvalidUtf8ProtocolDecoderTest,
                         ::testing::Values(
                             InvalidUtf8Case{
                                 .name = "overlong_two_byte",
                                 .frame = make_text_frame({0xC0, 0x80}),
                             },
                             InvalidUtf8Case{
                                 .name = "invalid_lead_byte",
                                 .frame = make_text_frame({0xFF}),
                             },
                             InvalidUtf8Case{
                                 .name = "truncated_sequence",
                                 .frame = make_text_frame({0xE2, 0x82}),
                             },
                             InvalidUtf8Case{
                                 .name = "surrogate_half",
                                 .frame = make_text_frame({0xED, 0xA0, 0x80}),
                             },
                             InvalidUtf8Case{
                                 .name = "overlong_four_byte",
                                 .frame = make_text_frame({0xF0, 0x80, 0x80, 0x80}),
                             }),
                         [](const ::testing::TestParamInfo<InvalidUtf8Case>& info) { return info.param.name; });

TEST(ProtocolDecoderTest, AcceptsAsciiAndFourByteUtf8Payloads) {
  const upr::CompiledProtocol protocol = make_decoder_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::DecodedMessage message;

  const std::vector<std::byte> ascii_text = make_text_frame({0x41, 0x42, 0x43});
  ASSERT_EQ(decoder.decode_as("Text", upr::ByteSpan(ascii_text.data(), ascii_text.size()), &message),
            upr::DecodeStatus::kOk);
  ASSERT_TRUE(message.get_string_view("text").has_value());
  EXPECT_EQ(*message.get_string_view("text"), "ABC");

  const std::vector<std::byte> four_byte_utf8 = make_text_frame({0xF0, 0x9F, 0x98, 0x80});
  ASSERT_EQ(decoder.decode_as("Text", upr::ByteSpan(four_byte_utf8.data(), four_byte_utf8.size()), &message),
            upr::DecodeStatus::kOk);
  ASSERT_TRUE(message.get_string_view("text").has_value());
  EXPECT_EQ(*message.get_string_view("text"), "\xF0\x9F\x98\x80");
}

TEST(ProtocolDecoderTest, HandlesNullProtocolAndMalformedCompiledLayoutsDefensively) {
  upr::DecodedMessage decoded;

  upr::CompiledField wide_unsigned;
  wide_unsigned.id = 0;
  wide_unsigned.name = "wide";
  wide_unsigned.kind = upr::FieldKind::kUnsigned;
  wide_unsigned.width_bytes = 9;
  upr::CompiledMessage wide_message("Wide", {wide_unsigned}, {}, {}, 9, false);
  upr::CompiledProtocol wide_protocol("wide", 1, {}, {wide_message});
  upr::ProtocolDecoder wide_decoder(wide_protocol);
  const std::vector<std::byte> wide_frame(9, std::byte{0xFF});
  ASSERT_EQ(wide_decoder.decode_as("Wide", upr::ByteSpan(wide_frame.data(), wide_frame.size()), &decoded),
            upr::DecodeStatus::kOk);
  EXPECT_FALSE(decoded.get_unsigned("wide").has_value());

  upr::CompiledField float_field;
  float_field.id = 0;
  float_field.name = "value";
  float_field.kind = upr::FieldKind::kFloat32;
  float_field.width_bytes = 9;
  upr::CompiledMessage float_message("WideFloat", {float_field}, {}, {}, 9, false);
  upr::CompiledProtocol float_protocol("float", 2, {}, {float_message});
  upr::ProtocolDecoder float_decoder(float_protocol);
  ASSERT_EQ(float_decoder.decode_as("WideFloat", upr::ByteSpan(wide_frame.data(), wide_frame.size()), &decoded),
            upr::DecodeStatus::kOk);
  EXPECT_FALSE(decoded.get_float32("value").has_value());

  upr::CompiledField flag_field;
  flag_field.id = 0;
  flag_field.name = "flags";
  flag_field.kind = upr::FieldKind::kUnsigned;
  flag_field.width_bytes = 1;
  upr::CompiledBitField broken_bit_field{
      .id = 0,
      .name = "delta",
      .container_field_id = 0,
      .shift_bits = 0,
      .width_bits = 0,
      .mask = 0,
      .is_signed = true,
      .enum_values = {},
  };
  upr::CompiledMessage broken_bits("BrokenBits", {flag_field}, {broken_bit_field}, {}, 1, false);
  upr::CompiledProtocol broken_bits_protocol("broken_bits", 3, {}, {broken_bits});
  upr::ProtocolDecoder broken_bits_decoder(broken_bits_protocol);
  const std::vector<std::byte> flags_frame = upr_test_support::make_bytes({0x7F});
  ASSERT_EQ(
      broken_bits_decoder.decode_as("BrokenBits", upr::ByteSpan(flags_frame.data(), flags_frame.size()), &decoded),
      upr::DecodeStatus::kOk);
  EXPECT_FALSE(decoded.get_bit_signed("delta").has_value());
}

TEST(ProtocolDecoderTest, ReturnsSchemaMismatchForMalformedCompiledChecksumAnchors) {
  const auto xor8 = upr::find_checksum_algorithm("xor8");
  ASSERT_TRUE(xor8.ok()) << xor8.status().message();

  upr::CompiledField crc_field;
  crc_field.id = 0;
  crc_field.name = "crc";
  crc_field.kind = upr::FieldKind::kUnsigned;
  crc_field.width_bytes = 1;

  upr::CompiledChecksum checksum{
      .field_id = 0,
      .result_width_bytes = 1,
      .function = xor8.value().function,
      .algorithm_name = xor8.value().name,
      .from =
          {
              .kind = upr::ChecksumAnchorKind::kFieldStart,
              .field_id = 99,
          },
      .to =
          {
              .kind = upr::ChecksumAnchorKind::kFieldEnd,
              .field_id = 99,
          },
  };

  upr::CompiledMessage message_schema("BrokenChecksum", {crc_field}, {}, {checksum}, 1, false);
  upr::CompiledProtocol protocol("broken_checksum", 4, {}, {message_schema});
  upr::ProtocolDecoder decoder(protocol);
  upr::DecodedMessage message;
  const std::vector<std::byte> frame = upr_test_support::make_bytes({0x00});

  EXPECT_EQ(decoder.decode_as("BrokenChecksum", upr::ByteSpan(frame.data(), frame.size()), &message),
            upr::DecodeStatus::kSchemaMismatch);
}

TEST(ProtocolDecoderTest, ReportsFieldLimitExceededForOversizedManualProtocol) {
  std::vector<upr::CompiledField> fields;
  for (size_t index = 0; index <= upr::kMaxFieldsPerMessage; ++index) {
    upr::CompiledField field;
    field.id = static_cast<upr::FieldId>(index);
    field.name = "field_" + std::to_string(index);
    field.kind = upr::FieldKind::kUnsigned;
    field.width_bytes = 1;
    fields.push_back(std::move(field));
  }
  upr::CompiledMessage message("Oversized", std::move(fields), {}, {}, 0, false);
  upr::CompiledProtocol protocol("manual", 123, {}, {message});
  upr::ProtocolDecoder decoder(protocol);
  upr::DecodedMessage decoded;

  EXPECT_EQ(decoder.decode_as("Oversized", upr::ByteSpan{}, &decoded), upr::DecodeStatus::kFieldLimitExceeded);
}

TEST(ProtocolDecoderTest, DefaultConstructedDecodedMessageIsEmpty) {
  upr::DecodedMessage message;

  EXPECT_FALSE(message.valid());
  EXPECT_TRUE(message.message_name().empty());
  EXPECT_FALSE(message.field_id("anything").has_value());
  EXPECT_FALSE(message.bit_field_id("anything").has_value());
  EXPECT_FALSE(message.protocol());
}

}  // namespace
