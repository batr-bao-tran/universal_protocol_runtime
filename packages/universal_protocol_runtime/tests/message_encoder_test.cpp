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

}  // namespace
