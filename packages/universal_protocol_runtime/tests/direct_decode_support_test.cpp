#include "universal_protocol_runtime/decoder/direct_decode_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <vector>

#include "universal_protocol_runtime/framing/frame_result.hpp"
#include "universal_protocol_runtime/runtime/stream_runtime.hpp"

namespace upr = universal_protocol_runtime;

namespace {

constexpr std::byte byte(uint8_t value) noexcept { return static_cast<std::byte>(value); }

constexpr std::array<std::byte, 4> kScalarBytes = {byte(0x12), byte(0x34), byte(0x56), byte(0x78)};
constexpr std::array<std::byte, 4> kFloatOneLe = {byte(0x00), byte(0x00), byte(0x80), byte(0x3F)};
constexpr std::array<std::byte, 3> kAsciiBytes = {byte('U'), byte('P'), byte('R')};
constexpr std::array<std::byte, 3> kAsciiInvalidBytes = {byte('U'), byte(0x80), byte('R')};
constexpr std::array<std::byte, 2> kUtf8CentSign = {byte(0xC2), byte(0xA2)};
constexpr std::array<std::byte, 2> kUtf8Invalid = {byte(0xC0), byte(0xAF)};
constexpr std::array<std::byte, 3> kXorBytes = {byte(0x01), byte(0x02), byte(0x03)};
constexpr std::array<std::byte, 2> kPrefix = {byte(0x12), byte(0x34)};

uint8_t scalar_checksum_xor8(upr::ByteSpan bytes) {
  uint8_t value = 0;
  for (const std::byte byte_value : bytes) {
    value ^= std::to_integer<uint8_t>(byte_value);
  }
  return value;
}

uint16_t scalar_checksum_sum16(upr::ByteSpan bytes) {
  uint32_t sum = 0;
  for (const std::byte byte_value : bytes) {
    sum += std::to_integer<uint8_t>(byte_value);
  }
  return static_cast<uint16_t>(sum & 0xFFFFU);
}

uint16_t reference_crc16_ccitt(upr::ByteSpan bytes) {
  uint16_t crc = 0xFFFFU;
  for (const std::byte byte_value : bytes) {
    crc ^= static_cast<uint16_t>(std::to_integer<uint8_t>(byte_value));
    for (uint8_t bit = 0; bit < upr::kBitsPerByte; ++bit) {
      crc = (crc & 1U) != 0U ? static_cast<uint16_t>((crc >> 1U) ^ 0x8408U) : static_cast<uint16_t>(crc >> 1U);
    }
  }
  return static_cast<uint16_t>(~crc);
}

uint32_t reference_crc32(upr::ByteSpan bytes) {
  uint32_t crc = 0xFFFFFFFFU;
  for (const std::byte byte_value : bytes) {
    crc ^= static_cast<uint32_t>(std::to_integer<uint8_t>(byte_value));
    for (uint8_t bit = 0; bit < upr::kBitsPerByte; ++bit) {
      crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
    }
  }
  return ~crc;
}

uint32_t reference_crc32c(upr::ByteSpan bytes) {
  uint32_t crc = 0xFFFFFFFFU;
  for (const std::byte byte_value : bytes) {
    crc ^= static_cast<uint32_t>(std::to_integer<uint8_t>(byte_value));
    for (uint8_t bit = 0; bit < upr::kBitsPerByte; ++bit) {
      crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0x82F63B78U : crc >> 1U;
    }
  }
  return ~crc;
}

std::vector<std::byte> make_large_valid_utf8(std::size_t target_size) {
  constexpr std::array<std::string_view, 4> kUtf8CodePoints = {
      "U",
      "\xC2\xA2",
      "\xE2\x82\xAC",
      "\xF0\x9F\x92\xA9",
  };

  std::vector<std::byte> bytes;
  bytes.reserve(target_size);
  std::size_t index = 0;
  while (bytes.size() < target_size) {
    const std::string_view code_point = kUtf8CodePoints[index % kUtf8CodePoints.size()];
    if (bytes.size() + code_point.size() > target_size) {
      bytes.push_back(byte('U'));
    } else {
      for (const char value : code_point) {
        bytes.push_back(byte(static_cast<uint8_t>(value)));
      }
    }
    ++index;
  }
  return bytes;
}

static_assert(upr::to_string(upr::ByteOrder::kBigEndian) == "big_endian");
static_assert(upr::to_string(upr::FieldKind::kString) == "string");
static_assert(upr::to_string(upr::StringEncoding::kUtf8) == "utf8");
static_assert(upr::to_string(upr::DecodeStatus::kChecksumMismatch) == "checksum_mismatch");
static_assert(upr::to_string(upr::FrameStatus::kNeedMoreData) == "need_more_data");
static_assert(upr::to_string(upr::PollStatus::kDecodeError) == "decode_error");

static_assert(
    upr::direct_decode_support::read_unsigned_scalar<upr::ByteOrder::kBigEndian, 4>(upr::ByteSpan(kScalarBytes))
        .value() == 0x12345678ULL);
static_assert(
    upr::direct_decode_support::read_unsigned_scalar<upr::ByteOrder::kLittleEndian, 4>(upr::ByteSpan(kScalarBytes))
        .value() == 0x78563412ULL);
static_assert(upr::direct_decode_support::read_float32<upr::ByteOrder::kLittleEndian>(upr::ByteSpan(kFloatOneLe))
                  .value() == 1.0F);
static_assert(upr::direct_decode_support::validate_string<upr::StringEncoding::kAscii>(upr::ByteSpan(kAsciiBytes)));
static_assert(
    !upr::direct_decode_support::validate_string<upr::StringEncoding::kAscii>(upr::ByteSpan(kAsciiInvalidBytes)));
static_assert(upr::direct_decode_support::validate_string<upr::StringEncoding::kUtf8>(upr::ByteSpan(kUtf8CentSign)));
static_assert(!upr::direct_decode_support::validate_string<upr::StringEncoding::kUtf8>(upr::ByteSpan(kUtf8Invalid)));
static_assert(upr::direct_decode_support::checksum_xor8(upr::ByteSpan(kXorBytes)) == 0U);
static_assert(upr::direct_decode_support::checksum_sum16(upr::ByteSpan(kXorBytes)) == 6U);
static_assert(upr::direct_decode_support::starts_with(upr::ByteSpan(kScalarBytes), kPrefix));

TEST(DirectDecodeSupportTest, RuntimeOverloadsMatchCompileTimeSpecializations) {
  const upr::ByteSpan bytes = upr::ByteSpan(kScalarBytes);
  const auto expected_big = upr::direct_decode_support::read_unsigned_scalar<upr::ByteOrder::kBigEndian, 4>(bytes);
  const auto expected_little =
      upr::direct_decode_support::read_unsigned_scalar<upr::ByteOrder::kLittleEndian, 4>(bytes);

  EXPECT_EQ(upr::direct_decode_support::read_unsigned_scalar(bytes, upr::ByteOrder::kBigEndian), expected_big);
  EXPECT_EQ(upr::direct_decode_support::read_unsigned_scalar(bytes, upr::ByteOrder::kLittleEndian), expected_little);
}

TEST(DirectDecodeSupportTest, ValidatesStringsAndFloatingPointValues) {
  EXPECT_TRUE(upr::direct_decode_support::validate_string<upr::StringEncoding::kAscii>(upr::ByteSpan(kAsciiBytes)));
  EXPECT_FALSE(upr::direct_decode_support::validate_string<upr::StringEncoding::kUtf8>(upr::ByteSpan(kUtf8Invalid)));
  EXPECT_TRUE(
      upr::direct_decode_support::runtime_validate_string<upr::StringEncoding::kAscii>(upr::ByteSpan(kAsciiBytes)));
  EXPECT_FALSE(
      upr::direct_decode_support::runtime_validate_string<upr::StringEncoding::kUtf8>(upr::ByteSpan(kUtf8Invalid)));

  const auto one = upr::direct_decode_support::read_float32(upr::ByteSpan(kFloatOneLe), upr::ByteOrder::kLittleEndian);
  ASSERT_TRUE(one.has_value());
  EXPECT_FLOAT_EQ(*one, 1.0F);
}

TEST(DirectDecodeSupportTest, LargeAsciiAndChecksumOperationsMatchScalarReference) {
  std::vector<std::byte> bytes(4096);
  for (size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = byte(static_cast<uint8_t>(((index * 37U) + 11U) & 0x7FU));
  }

  const upr::ByteSpan span(bytes);
  EXPECT_TRUE(upr::direct_decode_support::runtime_is_valid_ascii(span));
  EXPECT_EQ(upr::direct_decode_support::runtime_checksum_xor8(span), scalar_checksum_xor8(span));
  EXPECT_EQ(upr::direct_decode_support::runtime_checksum_sum16(span), scalar_checksum_sum16(span));
  EXPECT_EQ(upr::direct_decode_support::runtime_checksum_crc32c(span), reference_crc32c(span));

  bytes[2048] = byte(0xFF);
  EXPECT_FALSE(upr::direct_decode_support::runtime_is_valid_ascii(upr::ByteSpan(bytes)));
}

TEST(DirectDecodeSupportTest, LargeOddSizedBuffersMatchScalarReference) {
  std::vector<std::byte> bytes(4096 + 13U);
  for (size_t index = 0; index < bytes.size(); ++index) {
    bytes[index] = byte(static_cast<uint8_t>(((index * 19U) + 5U) & 0xFFU));
  }

  const upr::ByteSpan span(bytes);
  EXPECT_EQ(upr::direct_decode_support::runtime_checksum_xor8(span), scalar_checksum_xor8(span));
  EXPECT_EQ(upr::direct_decode_support::runtime_checksum_sum16(span), scalar_checksum_sum16(span));
  EXPECT_EQ(upr::direct_decode_support::runtime_checksum_crc32c(span), reference_crc32c(span));
}

TEST(DirectDecodeSupportTest, ScalarEdgeCasesAndCrcHelpersMatchReference) {
  EXPECT_FALSE(upr::direct_decode_support::is_valid_ascii(upr::ByteSpan(kAsciiInvalidBytes)));
  const std::vector<std::byte> width_mismatch_bytes = {byte('U'), byte('P'), byte('R')};
  const std::vector<std::byte> empty_bytes;
  const auto width_mismatch = upr::direct_decode_support::read_unsigned_scalar<upr::ByteOrder::kLittleEndian, 4>(
      upr::ByteSpan(width_mismatch_bytes));
  const auto empty_dynamic =
      upr::direct_decode_support::read_unsigned_scalar<upr::ByteOrder::kLittleEndian>(upr::ByteSpan(empty_bytes));
  EXPECT_FALSE(width_mismatch.has_value());
  EXPECT_FALSE(empty_dynamic.has_value());

  const std::array<std::byte, 7> small_bytes = {
      byte(0x10), byte(0x20), byte(0x30), byte(0x40), byte(0x50), byte(0x60), byte(0x70)};
  const upr::ByteSpan small_span(small_bytes);
  EXPECT_EQ(upr::direct_decode_support::runtime_checksum_xor8(small_span), scalar_checksum_xor8(small_span));
  EXPECT_EQ(upr::direct_decode_support::runtime_checksum_sum16(small_span), scalar_checksum_sum16(small_span));

  const std::array<std::byte, 6> crc_bytes = {byte(0x01), byte(0x23), byte(0x45), byte(0x67), byte(0x89), byte(0xAB)};
  const upr::ByteSpan crc_span(crc_bytes);
  EXPECT_EQ(upr::direct_decode_support::checksum_crc16_ccitt(crc_span), reference_crc16_ccitt(crc_span));
  EXPECT_EQ(upr::direct_decode_support::checksum_crc32(crc_span), reference_crc32(crc_span));
  EXPECT_EQ(upr::direct_decode_support::checksum_crc32c(crc_span), reference_crc32c(crc_span));
  EXPECT_EQ(upr::direct_decode_support::runtime_checksum_crc32c(crc_span), reference_crc32c(crc_span));
}

TEST(DirectDecodeSupportTest, RuntimeAsciiValidationChecksTailBytesAfterSimdChunks) {
  std::vector<std::byte> bytes(upr::direct_decode_support::detail::kAsciiSimdThresholdBytes + 3U, byte('A'));
  bytes.back() = byte(0x80);

  EXPECT_FALSE(upr::direct_decode_support::runtime_is_valid_ascii(upr::ByteSpan(bytes)));
}

TEST(DirectDecodeSupportTest, RuntimeUtf8ValidationMatchesScalarReference) {
  std::vector<std::byte> bytes =
      make_large_valid_utf8(upr::direct_decode_support::detail::kUtf8SimdThresholdBytes + 17U);
  const upr::ByteSpan span(bytes);

  EXPECT_TRUE(upr::direct_decode_support::validate_string<upr::StringEncoding::kUtf8>(span));
  EXPECT_TRUE(upr::direct_decode_support::runtime_validate_string<upr::StringEncoding::kUtf8>(span));

  bytes[bytes.size() / 2U] = byte(0xFF);
  EXPECT_FALSE(upr::direct_decode_support::runtime_validate_string<upr::StringEncoding::kUtf8>(upr::ByteSpan(bytes)));
}

}  // namespace
