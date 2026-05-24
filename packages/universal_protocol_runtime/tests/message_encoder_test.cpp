#include "universal_protocol_runtime/encoder/message_encoder.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <span>
#include <string_view>

#include "universal_protocol_runtime/compiler/checksum_registry.hpp"
#include "universal_protocol_runtime/compiler/schema_compiler.hpp"
#include "universal_protocol_runtime/decoder/direct_decode_support.hpp"
#include "universal_protocol_runtime/encoder/encode_status.hpp"
#include "universal_protocol_runtime/pdl/yaml_loader.hpp"

namespace upr = universal_protocol_runtime;

namespace {

upr::CompiledProtocol compile_yaml(std::string_view yaml) {
  auto definition = upr::load_protocol_definition_from_yaml(std::string(yaml));
  EXPECT_TRUE(definition.ok()) << definition.status().message();
  auto compiled = upr::compile_protocol(definition.value());
  EXPECT_TRUE(compiled.ok()) << compiled.status().message();
  return std::move(compiled.value());
}

upr::CompiledProtocol compile_upr(std::string_view schema) {
  auto definition = upr::load_protocol_definition_from_upr(std::string(schema));
  EXPECT_TRUE(definition.ok()) << definition.status().message();
  auto compiled = upr::compile_protocol(definition.value());
  EXPECT_TRUE(compiled.ok()) << compiled.status().message();
  return std::move(compiled.value());
}

TEST(EncodeStatusTest, ToStringCoversAllValues) {
  EXPECT_EQ(upr::to_string(upr::EncodeStatus::kOk), "ok");
  EXPECT_EQ(upr::to_string(upr::EncodeStatus::kBufferTooSmall), "buffer_too_small");
  EXPECT_EQ(upr::to_string(upr::EncodeStatus::kInvalidData), "invalid_data");
  EXPECT_EQ(upr::to_string(upr::EncodeStatus::kSchemaMismatch), "schema_mismatch");
  EXPECT_EQ(upr::to_string(upr::EncodeStatus::kFieldLimitExceeded), "field_limit_exceeded");
}

TEST(ProtocolEncoderTest, FindsExistingMessage) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  EXPECT_NE(encoder.find_message("Packet"), nullptr);
  EXPECT_EQ(encoder.find_message("Missing"), nullptr);
  EXPECT_EQ(encoder.protocol(), &protocol);
}

TEST(ProtocolEncoderTest, BuildReturnsNulloptForUnknownMessage) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 16> buf{};
  EXPECT_FALSE(encoder.build("Unknown", buf).has_value());
}

TEST(ProtocolEncoderTest, BuildReturnsValidBuilderForKnownMessage) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 16> buf{};
  const auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());
  EXPECT_TRUE(builder->valid());
}

TEST(MessageBuilderTest, EncodesSimpleUnsignedField) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 42U), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 1U);
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 42U);
}

TEST(MessageBuilderTest, EncodesU16LittleEndian) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: value
        type: uint16
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 2> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 0x0102U), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 2U);
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x02U);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x01U);
}

TEST(MessageBuilderTest, EncodesU16BigEndian) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: value
        type: uint16_be
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 2> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 0x0102U), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x01U);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x02U);
}

TEST(MessageBuilderTest, EncodesSigned) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: delta
        type: int8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_signed(0, -1), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 1U);
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0xFFU);
}

TEST(MessageBuilderTest, EncodesFloat32) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: price
        type: float32
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 4> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  const float val = 3.14F;
  EXPECT_EQ(builder->set_float32(0, val), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 4U);
  float recovered = 0.0F;
  std::memcpy(&recovered, buf.data(), sizeof(recovered));
  EXPECT_EQ(recovered, val);
}

TEST(MessageBuilderTest, EncodesFloat64) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: price
        type: float64
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 8> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  const double val = std::numbers::e;
  EXPECT_EQ(builder->set_float64(0, val), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 8U);
  double recovered = 0.0;
  std::memcpy(&recovered, buf.data(), sizeof(recovered));
  EXPECT_EQ(recovered, val);
}

TEST(MessageBuilderTest, EncodesDynamicBytes) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: length
        type: uint16
      - name: payload
        type: bytes
        size_from: length
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 7> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  const std::array<std::byte, 5> payload = {
      std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};
  EXPECT_EQ(builder->set_unsigned(0, 5U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_bytes(1, payload), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 7U);
  EXPECT_EQ(static_cast<uint8_t>(buf[2]), 0x01U);
  EXPECT_EQ(static_cast<uint8_t>(buf[6]), 0x05U);
}

TEST(MessageBuilderTest, EncodesFixedString) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: symbol
        type: string
        encoding: ascii
        size: 4
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 4> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_string(0, "AAPL"), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 4U);
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), static_cast<uint8_t>('A'));
  EXPECT_EQ(static_cast<uint8_t>(buf[3]), static_cast<uint8_t>('L'));
}

TEST(MessageBuilderTest, AutoWritesFixedValueFieldOnAdvance) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 7
  value: uint8
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 2> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  // Don't explicitly set message_type; set_unsigned for value should auto-advance
  EXPECT_EQ(builder->set_unsigned(1, 99U), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 2U);
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 7U);  // message_type auto-written
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 99U);
}

TEST(MessageBuilderTest, ComputesCrc16CcittChecksum) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  value: uint8
  checksum: uint16 checksum(crc16_ccitt)
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 3> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 0x42U), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 3U);
  // Checksum field must be non-zero (crc16 of 0x42)
  const auto cs =
      static_cast<uint16_t>(static_cast<uint8_t>(buf[1]) | (static_cast<uint16_t>(static_cast<uint8_t>(buf[2])) << 8U));
  EXPECT_NE(cs, 0U);
}

TEST(MessageBuilderTest, ComputesCrc32Checksum) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  value: uint16
  checksum: uint32 checksum(crc32)
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 6> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 0xABCDU), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 6U);
  // Checksum occupies last 4 bytes; verify it was written (non-zero for this input).
  uint32_t cs = 0;
  std::memcpy(&cs, buf.data() + 2, sizeof(cs));
  EXPECT_NE(cs, 0U);
}

TEST(MessageBuilderTest, ComputesCrc32cChecksum) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  value: uint16
  checksum: uint32 checksum(crc32c)
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 6> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 0x1234U), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 6U);
  uint32_t cs = 0;
  std::memcpy(&cs, buf.data() + 2, sizeof(cs));
  EXPECT_NE(cs, 0U);
}

TEST(MessageBuilderTest, ComputesCustomChecksum) {
  // Register a trivial sum-of-bytes algorithm so we exercise the kCustom
  // branch in compute_builtin_checksum().
  constexpr uint64_t (*kSimpleSum)(upr::ByteSpan) noexcept = [](upr::ByteSpan data) noexcept -> uint64_t {
    uint64_t s = 0;
    for (auto b : data) {
      s += static_cast<uint8_t>(b);
    }
    return s & 0xFFU;
  };
  auto reg = upr::register_checksum_algorithm(
      upr::ChecksumAlgorithmSpec{.name = "enc_test_custom", .result_width_bytes = 1, .function = kSimpleSum});
  ASSERT_TRUE(reg.ok()) << reg.message();

  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  value: uint8
  checksum: uint8 checksum(enc_test_custom)
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 2> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 0x07U), upr::EncodeStatus::kOk);
  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 2U);
  // value = 0x07 is at buf[0]; sum of [0x07] = 0x07, stored at buf[1]
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x07U);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x07U);
}

TEST(MessageBuilderTest, ComputesXor8Checksum) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 1
  value: uint8
  checksum: uint8 checksum(xor8)
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 3> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(1, 0xABU), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 3U);

  // message_type=1, value=0xAB -> xor = 1 XOR 0xAB = 0xAA
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x01U);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0xABU);
  EXPECT_EQ(static_cast<uint8_t>(buf[2]), static_cast<uint8_t>(0x01U ^ 0xABU));
}

TEST(MessageBuilderTest, ComputesSum16Checksum) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 2
  value: uint16
  checksum: uint16 checksum(sum16)
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 5> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(1, 0x0100U), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 5U);

  // message_type=2, value=0x0100 LE -> bytes [0x02, 0x00, 0x01]
  // sum16 = 2 + 0 + 1 = 3
  const auto cs_lo = static_cast<uint8_t>(buf[3]);
  const auto cs_hi = static_cast<uint8_t>(buf[4]);
  const auto checksum = static_cast<uint16_t>(cs_lo | (static_cast<uint16_t>(cs_hi) << 8U));
  EXPECT_EQ(checksum, 3U);
}

TEST(MessageBuilderTest, ReturnsInvalidDataForOutOfOrderWrite) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: a
        type: uint8
      - name: b
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 2> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  // Skipping field 0 and writing field 1 first is an out-of-order skip
  EXPECT_EQ(builder->set_unsigned(1, 0U), upr::EncodeStatus::kInvalidData);

  // After an error, build a fresh builder and verify re-writing is rejected
  auto builder2 = encoder.build("Packet", buf);
  ASSERT_TRUE(builder2.has_value());
  EXPECT_EQ(builder2->set_unsigned(0, 0U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder2->set_unsigned(1, 0U), upr::EncodeStatus::kOk);
  // Writing field 0 again after advancing past it is out-of-order
  EXPECT_EQ(builder2->set_unsigned(0, 0U), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, ReturnsInvalidDataForWrongFieldType) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: price
        type: float32
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 4> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  // price is float32 but we call set_unsigned
  EXPECT_EQ(builder->set_unsigned(0, 42U), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, ReturnsBufferTooSmallWhenBufferInsufficient) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: value
        type: uint32
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 2> buf{};  // too small for uint32
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 0U), upr::EncodeStatus::kBufferTooSmall);
}

TEST(MessageBuilderTest, ReturnsInvalidDataForOutOfRangeFieldId) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(99, 0U), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SubsequentCallsReturnErrorAfterFailure) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: value
        type: uint32
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 2> buf{};  // too small
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 0U), upr::EncodeStatus::kBufferTooSmall);
  // After failure, subsequent calls should also fail
  EXPECT_NE(builder->finalize(), upr::EncodeStatus::kOk);
}

TEST(MessageBuilderTest, FinalizeIsIdempotent) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 5U), upr::EncodeStatus::kOk);

  std::size_t written1 = 0;
  std::size_t written2 = 0;
  EXPECT_EQ(builder->finalize(&written1), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->finalize(&written2), upr::EncodeStatus::kOk);
  EXPECT_EQ(written1, written2);
  EXPECT_TRUE(builder->finalized());
}

TEST(MessageBuilderTest, FinalizeFailsIfRequiredFieldNotSet) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: a
        type: uint8
      - name: b
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 2> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  // Set field 'a' but skip 'b', then try to finalize
  EXPECT_EQ(builder->set_unsigned(0, 1U), upr::EncodeStatus::kOk);
  // finalize tries to advance past remaining fields; 'b' is not fixed-value -> error
  EXPECT_EQ(builder->finalize(), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, ViewReturnsWrittenBytes) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 4> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->view().size(), 0U);
  EXPECT_EQ(builder->set_unsigned(0, 9U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->view().size(), 1U);
}

TEST(MessageBuilderTest, DefaultConstructedBuilderIsInvalid) {
  const upr::MessageBuilder builder;
  EXPECT_FALSE(builder.valid());
}

TEST(MessageBuilderTest, MultiFieldRoundTrip) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Trade {
  message_type: uint8 = 3
  instrument_id: uint32
  price: float64
  quantity: uint32
  checksum: uint8 checksum(xor8)
}
)upr");

  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 18> buf{};
  auto builder = encoder.build("Trade", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(1, 12345U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_float64(2, 99.5), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_unsigned(3, 100U), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 18U);

  // Verify by decoding
  const auto& compiled_msg = *encoder.protocol()->find_message("Trade");
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 3U);  // message_type

  // Compute expected checksum manually
  uint8_t expected_xor = 0;
  for (std::size_t i = 0; i < 17; ++i) {
    expected_xor ^= static_cast<uint8_t>(buf[i]);
  }
  EXPECT_EQ(static_cast<uint8_t>(buf[17]), expected_xor);
  (void)compiled_msg;
}

TEST(MessageBuilderTest, SetBytesRejectsWrongType) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_bytes(0, upr::ByteSpan{}), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SetStringRejectsWrongType) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_string(0, "x"), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SetSignedRejectsWrongType) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_signed(0, -1), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SetFloat32RejectsWrongType) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_float32(0, 1.0F), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SetFloat64RejectsWrongType) {
  const auto protocol = compile_yaml(R"yaml(
protocol: test
messages:
  - name: Packet
    fields:
      - name: id
        type: uint8
)yaml");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_float64(0, 1.0), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SetStringBufferTooSmall) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  length: uint8
  symbol: ascii[length]
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 2> buf{};  // only room for length + 1 byte, but we'll try to write 5
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 1U), upr::EncodeStatus::kOk);  // length=1
  // Now write 5-byte string into a 1-byte remaining space
  EXPECT_EQ(builder->set_string(1, "HELLO"), upr::EncodeStatus::kBufferTooSmall);
}

TEST(MessageBuilderTest, ChecksumComputedOnCorrectRange) {
  // checksum covers only the payload field, not the id
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  id: uint8
  value: uint8
  checksum: uint8 checksum(xor8, id.start, checksum.start)
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 3> buf{};
  auto built_opt = encoder.build("Packet", buf);
  ASSERT_TRUE(built_opt.has_value());
  auto& builder = *built_opt;

  EXPECT_EQ(builder.set_unsigned(0, 0x05U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder.set_unsigned(1, 0x0AU), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder.finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 3U);

  // checksum = xor8([id_byte, value_byte]) = 0x05 XOR 0x0A = 0x0F
  EXPECT_EQ(static_cast<uint8_t>(buf[2]), static_cast<uint8_t>(0x05U ^ 0x0AU));
}

TEST(MessageBuilderTest, BytesFieldNoSize) {
  // Test fixed bytes field
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  data: bytes[4]
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 4> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  const std::array<std::byte, 4> payload = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}, std::byte{0xEF}};
  EXPECT_EQ(builder->set_bytes(0, payload), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 4U);
  EXPECT_EQ(buf[0], std::byte{0xDE});
  EXPECT_EQ(buf[3], std::byte{0xEF});
}

TEST(MessageBuilderTest, ManualBuilderRejectsOversizedFieldLayouts) {
  std::vector<upr::CompiledField> fields;
  fields.reserve(upr::kMaxFieldsPerMessage + 1U);
  for (size_t index = 0; index <= upr::kMaxFieldsPerMessage; ++index) {
    fields.push_back(upr::CompiledField{
        .id = static_cast<upr::FieldId>(index),
        .name = "f" + std::to_string(index),
        .kind = upr::FieldKind::kUnsigned,
        .width_bytes = 1,
    });
  }
  upr::CompiledMessage layout("Oversized", std::move(fields), {}, {}, upr::kMaxFieldsPerMessage + 1U, false);
  upr::CompiledProtocol protocol("manual", 1, {}, {layout});
  std::array<std::byte, 8> buffer{};

  upr::MessageBuilder builder(protocol, protocol.messages().front(), buffer);
  EXPECT_TRUE(builder.valid());
  EXPECT_EQ(builder.finalize(nullptr), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, FinalizePropagatesExpectedFieldAndChecksumPlaceholderBufferFailures) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 1
}
)upr");
  {
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 0> buffer{};
    auto builder = encoder.build("Packet", buffer);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->finalize(nullptr), upr::EncodeStatus::kBufferTooSmall);
  }

  const auto checksum_protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 1
  checksum: uint16 checksum(sum16)
}
)upr");
  {
    const upr::ProtocolEncoder encoder(checksum_protocol);
    std::array<std::byte, 2> buffer{};
    auto builder = encoder.build("Packet", buffer);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->finalize(nullptr), upr::EncodeStatus::kBufferTooSmall);
  }
}

TEST(MessageBuilderTest, SetterAdvanceFailuresPropagateFromImplicitFields) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 1
  delta: int8
  ratio: float32
  score: float64
  payload: bytes[1]
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);

  {
    std::array<std::byte, 0> buffer{};
    auto builder = encoder.build("Packet", buffer);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_signed(1, -1), upr::EncodeStatus::kBufferTooSmall);
  }
  {
    std::array<std::byte, 0> buffer{};
    auto builder = encoder.build("Packet", buffer);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_float32(2, 1.0F), upr::EncodeStatus::kBufferTooSmall);
  }
  {
    std::array<std::byte, 0> buffer{};
    auto builder = encoder.build("Packet", buffer);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_float64(3, 1.0), upr::EncodeStatus::kBufferTooSmall);
  }
  {
    std::array<std::byte, 0> buffer{};
    auto builder = encoder.build("Packet", buffer);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_bytes(4, upr::ByteSpan{}), upr::EncodeStatus::kBufferTooSmall);
  }
}

TEST(MessageBuilderTest, ScalarSettersReturnBufferTooSmallWhenFieldWriteFails) {
  {
    const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  delta: int8
}
)upr");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 0> buffer{};
    auto builder = encoder.build("Packet", buffer);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_signed(0, -1), upr::EncodeStatus::kBufferTooSmall);
  }
  {
    const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  price: float32
}
)upr");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 0> buffer{};
    auto builder = encoder.build("Packet", buffer);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_float32(0, 1.0F), upr::EncodeStatus::kBufferTooSmall);
  }
  {
    const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  price: float64
}
)upr");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 0> buffer{};
    auto builder = encoder.build("Packet", buffer);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_float64(0, 1.0), upr::EncodeStatus::kBufferTooSmall);
  }
}

TEST(MessageBuilderTest, ComputesChecksumUsingAfterSelfAnchor) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  value: uint8
  checksum: uint8 checksum(xor8, after_self, frame_end)
  tail: uint8
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 3> buffer{};
  auto builder = encoder.build("Packet", buffer);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 0x11U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_unsigned(2, 0x7AU), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->finalize(nullptr), upr::EncodeStatus::kOk);
  EXPECT_EQ(buffer[1], std::byte{0x7A});
}

TEST(MessageBuilderTest, ManualMalformedChecksumAnchorsFailFinalize) {
  upr::CompiledField value{
      .id = 0,
      .name = "value",
      .kind = upr::FieldKind::kUnsigned,
      .width_bytes = 1,
  };
  upr::CompiledField checksum_field{
      .id = 1,
      .name = "checksum",
      .kind = upr::FieldKind::kUnsigned,
      .width_bytes = 1,
  };

  upr::CompiledChecksum invalid_anchor{
      .field_id = 1,
      .result_width_bytes = 1,
      .function = nullptr,
      .algorithm_name = "xor8",
      .builtin_kind = upr::CompiledChecksum::BuiltinKind::kXor8,
      .from = {.kind = upr::ChecksumAnchorKind::kFieldStart, .field_id = 99},
      .to = {.kind = upr::ChecksumAnchorKind::kBeforeSelf, .field_id = 1},
  };
  upr::CompiledChecksum inverted_range{
      .field_id = 1,
      .result_width_bytes = 1,
      .function = nullptr,
      .algorithm_name = "xor8",
      .builtin_kind = upr::CompiledChecksum::BuiltinKind::kXor8,
      .from = {.kind = upr::ChecksumAnchorKind::kFrameEnd, .field_id = 0},
      .to = {.kind = upr::ChecksumAnchorKind::kFrameStart, .field_id = 0},
  };

  {
    upr::CompiledMessage layout("InvalidAnchor", {value, checksum_field}, {}, {invalid_anchor}, 2, false);
    upr::CompiledProtocol protocol("manual", 1, {}, {layout});
    std::array<std::byte, 2> buffer{};
    upr::MessageBuilder builder(protocol, protocol.messages().front(), buffer);
    ASSERT_TRUE(builder.valid());
    EXPECT_EQ(builder.set_unsigned(0, 0x11U), upr::EncodeStatus::kOk);
    EXPECT_EQ(builder.finalize(nullptr), upr::EncodeStatus::kInvalidData);
  }

  {
    upr::CompiledMessage layout("InvertedRange", {value, checksum_field}, {}, {inverted_range}, 2, false);
    upr::CompiledProtocol protocol("manual", 2, {}, {layout});
    std::array<std::byte, 2> buffer{};
    upr::MessageBuilder builder(protocol, protocol.messages().front(), buffer);
    ASSERT_TRUE(builder.valid());
    EXPECT_EQ(builder.set_unsigned(0, 0x22U), upr::EncodeStatus::kOk);
    EXPECT_EQ(builder.finalize(nullptr), upr::EncodeStatus::kInvalidData);
  }
}

TEST(MessageBuilderTest, ManualCustomChecksumWithoutFunctionWritesZero) {
  upr::CompiledField value{
      .id = 0,
      .name = "value",
      .kind = upr::FieldKind::kUnsigned,
      .width_bytes = 1,
  };
  upr::CompiledField checksum_field{
      .id = 1,
      .name = "checksum",
      .kind = upr::FieldKind::kUnsigned,
      .width_bytes = 1,
  };
  upr::CompiledChecksum custom_checksum{
      .field_id = 1,
      .result_width_bytes = 1,
      .function = nullptr,
      .algorithm_name = "custom",
      .builtin_kind = upr::CompiledChecksum::BuiltinKind::kCustom,
      .from = {.kind = upr::ChecksumAnchorKind::kFrameStart, .field_id = 0},
      .to = {.kind = upr::ChecksumAnchorKind::kBeforeSelf, .field_id = 1},
  };

  upr::CompiledMessage layout("CustomChecksum", {value, checksum_field}, {}, {custom_checksum}, 2, false);
  upr::CompiledProtocol protocol("manual", 3, {}, {layout});
  std::array<std::byte, 2> buffer{};
  upr::MessageBuilder builder(protocol, protocol.messages().front(), buffer);
  ASSERT_TRUE(builder.valid());
  EXPECT_EQ(builder.set_unsigned(0, 0x5AU), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder.finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 2U);
  EXPECT_EQ(buffer[1], std::byte{0x00});
}

TEST(MessageBuilderTest, SetterMethodsReturnInvalidDataAfterFailure) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  value: uint32
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};  // too small for uint32
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 1U), upr::EncodeStatus::kBufferTooSmall);

  std::array<std::byte, 1> dummy{};
  EXPECT_EQ(builder->set_unsigned(0, 1U), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_signed(0, -1), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_float32(0, 1.0F), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_float64(0, 1.0), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_bytes(0, dummy), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_string(0, "x"), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SetterMethodsReturnInvalidDataForOutOfRangeFieldId) {
  // Field ID beyond the message's field count returns kInvalidData
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  value: uint8
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  std::array<std::byte, 1> dummy{};
  EXPECT_EQ(builder->set_signed(99, 0), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_float32(99, 0.0F), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_float64(99, 0.0), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_bytes(99, dummy), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_string(99, "x"), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SetterMethodsReturnInvalidDataForAlreadyWrittenField) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  value: uint8
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 5U), upr::EncodeStatus::kOk);

  std::array<std::byte, 1> dummy{};
  EXPECT_EQ(builder->set_signed(0, 0), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_float32(0, 0.0F), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_float64(0, 0.0), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_bytes(0, dummy), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_string(0, "x"), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SetBytesReturnsTooSmallWhenBufferInsufficient) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  data: bytes[4]
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> buf{};  // too small for 4-byte field
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  const std::array<std::byte, 4> payload{};
  EXPECT_EQ(builder->set_bytes(0, payload), upr::EncodeStatus::kBufferTooSmall);
}

TEST(MessageBuilderTest, SetStringPropagatesAdvanceError) {
  // Skipping a required field causes advance_to() to fail
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  header: uint8
  label: string[4]
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 5> buf{};
  auto builder = encoder.build("Packet", buf);
  ASSERT_TRUE(builder.has_value());

  // Skip field 0
  EXPECT_EQ(builder->set_string(1, "ABCD"), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, AutoFillsReservedBytesAndAlignment) {
  const auto protocol = compile_upr(R"upr(
protocol hw
message Packet {
  version: uint8
  pad: reserved[3] align(4)
  value: uint8
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 8> buffer{};
  auto builder = encoder.build("Packet", buffer);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 2U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_unsigned(2, 9U), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 8U);
  EXPECT_EQ(buffer[0], std::byte{0x02});
  EXPECT_EQ(buffer[1], std::byte{0x00});
  EXPECT_EQ(buffer[2], std::byte{0x00});
  EXPECT_EQ(buffer[3], std::byte{0x00});
  EXPECT_EQ(buffer[4], std::byte{0x00});
  EXPECT_EQ(buffer[5], std::byte{0x00});
  EXPECT_EQ(buffer[6], std::byte{0x00});
  EXPECT_EQ(buffer[7], std::byte{0x09});
}

TEST(MessageBuilderTest, RejectsValidationFailureOnFinalize) {
  const auto protocol = compile_upr(R"upr(
protocol hw
message Packet {
  version: uint8
  payload_len: uint8
  item_count: uint8
  validate(payload_len == item_count * 4, if(version == 2))
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 3> buffer{};
  auto builder = encoder.build("Packet", buffer);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_unsigned(0, 2U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_unsigned(1, 5U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_unsigned(2, 1U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->finalize(nullptr), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SupportsSegmentedZeroCopyEncodingAndPlans) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 1
  payload_len: uint8
  payload: bytes[payload_len]
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  const auto plan = encoder.make_plan("Packet");
  ASSERT_TRUE(plan.has_value());
  std::array<std::byte, 8> scratch{};
  auto builder = encoder.build_segmented(*plan, scratch);
  ASSERT_TRUE(builder.has_value());

  const std::array<std::byte, 3> payload = {std::byte{0x10}, std::byte{0x11}, std::byte{0x12}};
  EXPECT_EQ(builder->set_unsigned(1, payload.size()), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->attach_bytes(2, payload), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 5U);
  ASSERT_EQ(builder->segments().size(), 3U);
  EXPECT_EQ(builder->segments()[2].bytes.data(), payload.data());

  std::array<std::byte, 5> flattened{};
  EXPECT_EQ(builder->copy_to(flattened, &written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 5U);
  EXPECT_EQ(flattened[0], std::byte{0x01});
  EXPECT_EQ(flattened[1], std::byte{0x03});
  EXPECT_EQ(flattened[4], std::byte{0x12});
}

TEST(MessageBuilderTest, SegmentedBuilderSupportsAdditionalScalarAndStringTypes) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  header: uint8 = 7
  delta: int16
  price: float32
  total: float64
  text_len: uint8
  text: utf8[text_len]
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 64> scratch{};
  auto builder = encoder.build_segmented("Packet", scratch);
  ASSERT_TRUE(builder.has_value());

  EXPECT_EQ(builder->set_signed(1, -12), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_float32(2, 1.5F), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_float64(3, 7.25), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_unsigned(4, 2U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->attach_string(5, "OK"), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->copy_to(scratch), upr::EncodeStatus::kInvalidData);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 18U);

  std::array<std::byte, 17> too_small{};
  EXPECT_EQ(builder->copy_to(too_small), upr::EncodeStatus::kBufferTooSmall);

  std::array<std::byte, 18> flattened{};
  EXPECT_EQ(builder->copy_to(flattened, &written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, flattened.size());
  EXPECT_EQ(flattened[0], std::byte{0x07});
  EXPECT_EQ(flattened.back(), std::byte{'K'});
}

TEST(MessageBuilderTest, SegmentedBuilderComputesChecksumsAndHandlesManualFailures) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 1
  payload_len: uint8
  payload: bytes[payload_len]
  crc: uint8 checksum(xor8)
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 32> scratch{};
  auto builder = encoder.build_segmented("Packet", scratch);
  ASSERT_TRUE(builder.has_value());

  const std::array<std::byte, 3> payload = {std::byte{0x20}, std::byte{0x21}, std::byte{0x22}};
  EXPECT_EQ(builder->set_unsigned(1, payload.size()), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->attach_bytes(2, payload), upr::EncodeStatus::kOk);
  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 6U);

  std::array<std::byte, 6> flattened{};
  EXPECT_EQ(builder->copy_to(flattened), upr::EncodeStatus::kOk);
  EXPECT_EQ(flattened[5], std::byte{0x01 ^ 0x03 ^ 0x20 ^ 0x21 ^ 0x22});

  upr::CompiledField value{
      .id = 0,
      .name = "value",
      .kind = upr::FieldKind::kUnsigned,
      .width_bytes = 1,
  };
  upr::CompiledField checksum_field{
      .id = 1,
      .name = "checksum",
      .kind = upr::FieldKind::kUnsigned,
      .width_bytes = 1,
  };
  upr::CompiledChecksum invalid_anchor{
      .field_id = 1,
      .result_width_bytes = 1,
      .function = nullptr,
      .algorithm_name = "xor8",
      .builtin_kind = upr::CompiledChecksum::BuiltinKind::kXor8,
      .from = {.kind = upr::ChecksumAnchorKind::kFieldStart, .field_id = 99},
      .to = {.kind = upr::ChecksumAnchorKind::kBeforeSelf, .field_id = 1},
  };
  upr::CompiledMessage layout("Broken", {value, checksum_field}, {}, {invalid_anchor}, 2, false);
  upr::CompiledProtocol manual("manual", 1, {}, {layout});
  upr::ProtocolEncoder manual_encoder(manual);
  std::array<std::byte, 4> manual_scratch{};
  auto broken = manual_encoder.build_segmented("Broken", manual_scratch);
  ASSERT_TRUE(broken.has_value());
  EXPECT_EQ(broken->set_unsigned(0, 9U), upr::EncodeStatus::kOk);
  EXPECT_EQ(broken->finalize(nullptr), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, SegmentedBuilderCoversValidationOperatorsAndErrorPaths) {
  const std::array cases = {
      std::pair{"!=", "validate(lhs != rhs)"},
      std::pair{"<", "validate(lhs < rhs)"},
      std::pair{"<=", "validate(lhs <= rhs)"},
      std::pair{">", "validate(lhs > rhs)"},
      std::pair{">=", "validate(lhs >= rhs)"},
  };
  for (const auto& [name, validation] : cases) {
    const auto protocol = compile_upr(std::string("protocol test\nmessage Packet {\n  lhs: uint8\n  rhs: uint8\n  ") +
                                      validation + "\n}\n");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 8> scratch{};
    auto builder = encoder.build_segmented("Packet", scratch);
    ASSERT_TRUE(builder.has_value()) << name;
    EXPECT_EQ(builder->set_unsigned(0, 2U), upr::EncodeStatus::kOk) << name;
    EXPECT_EQ(builder->set_unsigned(1, std::string_view(name) == ">" ? 1U : 3U), upr::EncodeStatus::kOk) << name;
    if (std::string_view(name) == ">=") {
      builder = encoder.build_segmented("Packet", scratch);
      ASSERT_TRUE(builder.has_value());
      EXPECT_EQ(builder->set_unsigned(0, 3U), upr::EncodeStatus::kOk);
      EXPECT_EQ(builder->set_unsigned(1, 3U), upr::EncodeStatus::kOk);
    }
    EXPECT_EQ(builder->finalize(nullptr), upr::EncodeStatus::kOk) << name;
  }

  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  count: uint8
  payload: bytes[count]
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 1> tiny_scratch{};
  auto tiny = encoder.build_segmented("Packet", tiny_scratch);
  ASSERT_TRUE(tiny.has_value());
  EXPECT_EQ(tiny->set_unsigned(0, 4U), upr::EncodeStatus::kOk);
  const std::array<std::byte, 4> payload = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  EXPECT_EQ(tiny->set_bytes(1, payload), upr::EncodeStatus::kBufferTooSmall);
  EXPECT_EQ(tiny->set_unsigned(0, 1U), upr::EncodeStatus::kInvalidData);

  auto unknown_plan = encoder.make_plan("Missing");
  EXPECT_FALSE(unknown_plan.has_value());
  EXPECT_FALSE(encoder.build_segmented("Missing", tiny_scratch).has_value());

  const auto other_protocol = compile_upr(R"upr(
protocol other
message Packet {
  value: uint8
}
)upr");
  const upr::ProtocolEncoder other_encoder(other_protocol);
  const auto other_plan = other_encoder.make_plan("Packet");
  ASSERT_TRUE(other_plan.has_value());
  EXPECT_FALSE(encoder.build(*other_plan, tiny_scratch).has_value());
  EXPECT_FALSE(encoder.build_segmented(*other_plan, tiny_scratch).has_value());
}

TEST(MessageBuilderTest, SegmentedBuilderCoversAlgorithmsAndWrongTypeErrors) {
  struct Case {
    const char* name;
    const char* checksum_decl;
  };
  const std::array<Case, 5> cases = {{
      {.name = "xor8", .checksum_decl = "checksum: uint8 checksum(xor8)"},
      {.name = "sum16", .checksum_decl = "checksum: uint16 checksum(sum16)"},
      {.name = "crc16", .checksum_decl = "checksum: uint16 checksum(crc16_ccitt)"},
      {.name = "crc32", .checksum_decl = "checksum: uint32 checksum(crc32)"},
      {.name = "crc32c", .checksum_decl = "checksum: uint32 checksum(crc32c)"},
  }};
  for (const auto& test_case : cases) {
    const auto protocol = compile_upr(std::string("protocol checks\nmessage Packet {\n  message_type: uint8 = 1\n  "
                                                  "payload_len: uint8\n  payload: bytes[payload_len]\n  ") +
                                      test_case.checksum_decl + "\n}\n");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 32> scratch{};
    auto builder = encoder.build_segmented("Packet", scratch);
    ASSERT_TRUE(builder.has_value()) << test_case.name;
    const std::array<std::byte, 3> payload = {std::byte{0x31}, std::byte{0x32}, std::byte{0x33}};
    EXPECT_EQ(builder->set_unsigned(1, payload.size()), upr::EncodeStatus::kOk) << test_case.name;
    EXPECT_EQ(builder->set_bytes(2, payload), upr::EncodeStatus::kOk) << test_case.name;
    EXPECT_EQ(builder->finalize(nullptr), upr::EncodeStatus::kOk) << test_case.name;
  }

  std::array<std::byte, 1> scratch{};

  const auto protocol = compile_upr(R"upr(
protocol kinds
message Packet {
  value: uint8
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  auto builder = encoder.build_segmented("Packet", scratch);
  ASSERT_TRUE(builder.has_value());
  EXPECT_EQ(builder->set_signed(0, -1), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_float32(0, 1.0F), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_float64(0, 1.0), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_string(0, "x"), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->attach_string(0, "x"), upr::EncodeStatus::kInvalidData);
  std::array<std::byte, 1> one = {std::byte{0x01}};
  EXPECT_EQ(builder->attach_bytes(0, one), upr::EncodeStatus::kInvalidData);
  EXPECT_EQ(builder->set_bytes(0, one), upr::EncodeStatus::kInvalidData);
}

TEST(MessageBuilderTest, PlanAndSegmentedBuildersRejectInvalidInputsAndBufferEdges) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 1
  reserved_gap: reserved[2] align(4)
  label: string[2]
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  const auto plan = encoder.make_plan("Packet");
  ASSERT_TRUE(plan.has_value());

  std::array<std::byte, 8> exact{};
  auto contiguous = encoder.build(*plan, exact);
  ASSERT_TRUE(contiguous.has_value());
  EXPECT_EQ(contiguous->set_string(2, "OK"), upr::EncodeStatus::kOk);
  std::size_t written = 0;
  EXPECT_EQ(contiguous->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 8U);

  std::array<std::byte, 3> tiny{};
  auto segmented = encoder.build_segmented("Packet", tiny);
  ASSERT_TRUE(segmented.has_value());
  EXPECT_EQ(segmented->set_string(2, "OK"), upr::EncodeStatus::kBufferTooSmall);
  EXPECT_EQ(segmented->finalize(nullptr), upr::EncodeStatus::kInvalidData);

  auto invalid_plan_builder = encoder.build(upr::EncodePlan{}, exact);
  EXPECT_FALSE(invalid_plan_builder.has_value());
  auto invalid_segmented_builder = encoder.build_segmented(upr::EncodePlan{}, exact);
  EXPECT_FALSE(invalid_segmented_builder.has_value());

  const auto other_protocol = compile_upr(R"upr(
protocol other
message Packet {
  value: uint8
}
)upr");
  const upr::ProtocolEncoder other_encoder(other_protocol);
  const auto other_plan = other_encoder.make_plan("Packet");
  ASSERT_TRUE(other_plan.has_value());
  EXPECT_FALSE(encoder.build(*other_plan, exact).has_value());
  EXPECT_FALSE(encoder.build_segmented(*other_plan, exact).has_value());
}

TEST(MessageBuilderTest, SegmentedBuilderSupportsAlignedBorrowedPayloadsAndScalarTypes) {
  const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 1
  pad: reserved[2] align(4)
  delta: int8
  ratio: float32
  notional: float64
  payload_len: uint8
  payload: bytes[payload_len]
  text_len: uint8
  text: string[text_len]
}
)upr");
  const upr::ProtocolEncoder encoder(protocol);
  std::array<std::byte, 32> scratch{};
  auto builder = encoder.build_segmented("Packet", scratch);
  ASSERT_TRUE(builder.has_value());

  const std::array<std::byte, 2> payload = {std::byte{0xAA}, std::byte{0xBB}};
  EXPECT_EQ(builder->set_signed(2, -1), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_float32(3, 1.5F), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_float64(4, 2.5), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_unsigned(5, payload.size()), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->attach_bytes(6, payload), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->set_unsigned(7, 3U), upr::EncodeStatus::kOk);
  EXPECT_EQ(builder->attach_string(8, "XYZ"), upr::EncodeStatus::kOk);

  std::size_t written = 0;
  EXPECT_EQ(builder->finalize(&written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 26U);
  EXPECT_EQ(builder->segments().size(), 10U);

  std::array<std::byte, 32> output{};
  EXPECT_EQ(builder->copy_to(output, &written), upr::EncodeStatus::kOk);
  EXPECT_EQ(written, 26U);
  EXPECT_EQ(output[0], std::byte{0x01});
  EXPECT_EQ(output[1], std::byte{0x00});
  EXPECT_EQ(output[4], std::byte{0x00});
  EXPECT_EQ(output[6], std::byte{0xFF});
  EXPECT_EQ(output[20], std::byte{0xAA});
  EXPECT_EQ(output[21], std::byte{0xBB});
  EXPECT_EQ(output[23], std::byte{0x58});
  EXPECT_EQ(output[25], std::byte{0x5A});
}

TEST(MessageBuilderTest, SegmentedBuilderCoversAdditionalFailurePaths) {
  {
    const auto protocol = compile_upr(R"upr(
protocol test
message Packet { value: int8 }
)upr");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 0> scratch{};
    auto builder = encoder.build_segmented("Packet", scratch);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_signed(0, -1), upr::EncodeStatus::kBufferTooSmall);
  }

  {
    const auto protocol = compile_upr(R"upr(
protocol test
message Packet { value: float32 }
)upr");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 0> scratch{};
    auto builder = encoder.build_segmented("Packet", scratch);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_float32(0, 1.0F), upr::EncodeStatus::kBufferTooSmall);
  }

  {
    const auto protocol = compile_upr(R"upr(
protocol test
message Packet { value: float64 }
)upr");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 0> scratch{};
    auto builder = encoder.build_segmented("Packet", scratch);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_float64(0, 1.0), upr::EncodeStatus::kBufferTooSmall);
  }

  {
    const auto protocol = compile_upr(R"upr(
protocol test
message Packet { payload: bytes[2] }
)upr");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 1> scratch{};
    auto builder = encoder.build_segmented("Packet", scratch);
    ASSERT_TRUE(builder.has_value());
    const std::array<std::byte, 2> payload = {std::byte{0x01}, std::byte{0x02}};
    EXPECT_EQ(builder->set_bytes(0, payload), upr::EncodeStatus::kBufferTooSmall);
  }

  {
    const auto protocol = compile_upr(R"upr(
protocol test
message Packet { text: string[2] }
)upr");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 1> scratch{};
    auto builder = encoder.build_segmented("Packet", scratch);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_string(0, "AB"), upr::EncodeStatus::kBufferTooSmall);
  }

  {
    const auto protocol = compile_upr(R"upr(
protocol test
message Packet {
  message_type: uint8 = 1
  pad: reserved[2] align(4)
  payload: bytes[1]
}
)upr");
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 3> scratch{};
    auto builder = encoder.build_segmented("Packet", scratch);
    ASSERT_TRUE(builder.has_value());
    const std::array<std::byte, 1> payload = {std::byte{0xAA}};
    EXPECT_EQ(builder->set_bytes(2, payload), upr::EncodeStatus::kBufferTooSmall);
  }

  {
    std::vector<upr::CompiledField> fields;
    fields.reserve(upr::kMaxFieldsPerMessage + 1U);
    for (size_t index = 0; index <= upr::kMaxFieldsPerMessage; ++index) {
      fields.push_back(upr::CompiledField{
          .id = static_cast<upr::FieldId>(index),
          .name = "f" + std::to_string(index),
          .kind = upr::FieldKind::kUnsigned,
          .width_bytes = 1,
      });
    }
    upr::CompiledMessage layout("Oversized", std::move(fields), {}, {}, upr::kMaxFieldsPerMessage + 1U, false);
    upr::CompiledProtocol protocol("manual", 1, {}, {layout});
    std::array<std::byte, 8> scratch{};
    upr::SegmentedMessageBuilder builder(protocol, protocol.messages().front(), scratch);
    EXPECT_TRUE(builder.valid());
    EXPECT_EQ(builder.finalize(nullptr), upr::EncodeStatus::kInvalidData);
  }

  {
    upr::CompiledField value{.id = 0, .name = "value", .kind = upr::FieldKind::kUnsigned, .width_bytes = 1};
    upr::CompiledField checksum_field{.id = 1, .name = "checksum", .kind = upr::FieldKind::kUnsigned, .width_bytes = 1};
    upr::CompiledChecksum checksum{
        .field_id = 1,
        .result_width_bytes = 1,
        .function = upr::direct_decode_support::runtime_checksum_xor8,
        .algorithm_name = "xor8",
        .builtin_kind = upr::CompiledChecksum::BuiltinKind::kXor8,
        .from = {.kind = upr::ChecksumAnchorKind::kFieldEnd, .field_id = 1},
        .to = {.kind = upr::ChecksumAnchorKind::kFieldStart, .field_id = 0},
    };
    upr::CompiledMessage layout("Broken", {value, checksum_field}, {}, {checksum}, 2, false);
    upr::CompiledProtocol protocol("manual", 2, {}, {layout});
    const upr::ProtocolEncoder encoder(protocol);
    std::array<std::byte, 8> scratch{};
    auto builder = encoder.build_segmented("Broken", scratch);
    ASSERT_TRUE(builder.has_value());
    EXPECT_EQ(builder->set_unsigned(0, 0x42U), upr::EncodeStatus::kOk);
    EXPECT_EQ(builder->finalize(nullptr), upr::EncodeStatus::kInvalidData);
  }
}

}  // namespace
