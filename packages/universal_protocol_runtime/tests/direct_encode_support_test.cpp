#include "universal_protocol_runtime/encoder/direct_encode_support.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <span>

#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/core/types.hpp"

namespace upr = universal_protocol_runtime;
namespace enc = upr::direct_encode_support;

namespace {

constexpr upr::ByteOrder kLE = upr::ByteOrder::kLittleEndian;
constexpr upr::ByteOrder kBE = upr::ByteOrder::kBigEndian;

// Helpers that avoid macro/template-comma ambiguity.
template <upr::ByteOrder Bo, std::size_t W>
bool write_u(upr::MutableByteSpan buf, uint64_t value) {
  return enc::write_unsigned_scalar<Bo, W>(buf, value);
}
template <upr::ByteOrder Bo>
bool write_f32(upr::MutableByteSpan buf, float value) {
  return enc::write_float32<Bo>(buf, value);
}
template <upr::ByteOrder Bo>
bool write_f64(upr::MutableByteSpan buf, double value) {
  return enc::write_float64<Bo>(buf, value);
}

TEST(WriteUnsignedScalarTest, WritesU8LittleEndian) {
  std::array<std::byte, 1> buf{};
  EXPECT_TRUE((write_u<kLE, 1>(buf, 0xABU)));
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0xABU);
}

TEST(WriteUnsignedScalarTest, WritesU16LittleEndian) {
  std::array<std::byte, 2> buf{};
  EXPECT_TRUE((write_u<kLE, 2>(buf, 0x1234U)));
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x34U);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x12U);
}

TEST(WriteUnsignedScalarTest, WritesU16BigEndian) {
  std::array<std::byte, 2> buf{};
  EXPECT_TRUE((write_u<kBE, 2>(buf, 0x1234U)));
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x12U);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x34U);
}

TEST(WriteUnsignedScalarTest, WritesU32LittleEndian) {
  std::array<std::byte, 4> buf{};
  EXPECT_TRUE((write_u<kLE, 4>(buf, 0xDEADBEEFU)));
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0xEFU);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0xBEU);
  EXPECT_EQ(static_cast<uint8_t>(buf[2]), 0xADU);
  EXPECT_EQ(static_cast<uint8_t>(buf[3]), 0xDEU);
}

TEST(WriteUnsignedScalarTest, WritesU32BigEndian) {
  std::array<std::byte, 4> buf{};
  EXPECT_TRUE((write_u<kBE, 4>(buf, 0xDEADBEEFU)));
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0xDEU);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0xADU);
  EXPECT_EQ(static_cast<uint8_t>(buf[2]), 0xBEU);
  EXPECT_EQ(static_cast<uint8_t>(buf[3]), 0xEFU);
}

TEST(WriteUnsignedScalarTest, WritesU64LittleEndian) {
  std::array<std::byte, 8> buf{};
  EXPECT_TRUE((write_u<kLE, 8>(buf, 0x0102030405060708ULL)));
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x08U);
  EXPECT_EQ(static_cast<uint8_t>(buf[7]), 0x01U);
}

TEST(WriteUnsignedScalarTest, WritesU64BigEndian) {
  std::array<std::byte, 8> buf{};
  EXPECT_TRUE((write_u<kBE, 8>(buf, 0x0102030405060708ULL)));
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x01U);
  EXPECT_EQ(static_cast<uint8_t>(buf[7]), 0x08U);
}

TEST(WriteUnsignedScalarTest, ReturnsFalseOnSizeMismatch) {
  std::array<std::byte, 3> buf{};
  EXPECT_FALSE((write_u<kLE, 4>(buf, 0)));
}

TEST(WriteUnsignedScalarTest, WritesThreeByteLE) {
  std::array<std::byte, 3> buf{};
  EXPECT_TRUE((write_u<kLE, 3>(buf, 0x010203U)));
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x03U);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x02U);
  EXPECT_EQ(static_cast<uint8_t>(buf[2]), 0x01U);
}

TEST(WriteUnsignedScalarRuntimeTest, DispatchesAllWidthsLE) {
  for (std::size_t w = 1; w <= 8; ++w) {
    std::array<std::byte, 8> buf{};
    upr::MutableByteSpan span(buf.data(), w);
    EXPECT_TRUE(enc::write_unsigned_scalar<upr::ByteOrder::kLittleEndian>(span, 1ULL));
  }
}

TEST(WriteUnsignedScalarRuntimeTest, DispatchesAllWidthsBE) {
  for (std::size_t w = 1; w <= 8; ++w) {
    std::array<std::byte, 8> buf{};
    upr::MutableByteSpan span(buf.data(), w);
    EXPECT_TRUE(enc::write_unsigned_scalar<upr::ByteOrder::kBigEndian>(span, 1ULL));
  }
}

TEST(WriteUnsignedScalarRuntimeTest, ReturnsFalseForWidthZero) {
  std::array<std::byte, 1> buf{};
  EXPECT_FALSE(enc::write_unsigned_scalar<upr::ByteOrder::kLittleEndian>(upr::MutableByteSpan(buf.data(), 0), 1ULL));
}

TEST(WriteUnsignedScalarRuntimeTest, ReturnsFalseForWidthNine) {
  std::array<std::byte, 9> buf{};
  EXPECT_FALSE(enc::write_unsigned_scalar<upr::ByteOrder::kLittleEndian>(upr::MutableByteSpan(buf.data(), 9), 1ULL));
}

TEST(WriteUnsignedScalarFullRuntimeTest, LittleEndian) {
  std::array<std::byte, 2> buf{};
  EXPECT_TRUE(enc::write_unsigned_scalar(buf, 0x0102U, upr::ByteOrder::kLittleEndian));
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x02U);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x01U);
}

TEST(WriteUnsignedScalarFullRuntimeTest, BigEndian) {
  std::array<std::byte, 2> buf{};
  EXPECT_TRUE(enc::write_unsigned_scalar(buf, 0x0102U, upr::ByteOrder::kBigEndian));
  EXPECT_EQ(static_cast<uint8_t>(buf[0]), 0x01U);
  EXPECT_EQ(static_cast<uint8_t>(buf[1]), 0x02U);
}

TEST(WriteFloat32Test, RoundTripsLE) {
  std::array<std::byte, 4> buf{};
  const float original = std::numbers::pi_v<float>;
  EXPECT_TRUE(write_f32<kLE>(buf, original));
  float recovered = 0.0F;
  std::memcpy(&recovered, buf.data(), sizeof(recovered));
  EXPECT_EQ(recovered, original);
}

TEST(WriteFloat32Test, RoundTripsBE) {
  std::array<std::byte, 4> buf_le{};
  std::array<std::byte, 4> buf_be{};
  const float val = -42.5F;
  EXPECT_TRUE(write_f32<kLE>(buf_le, val));
  EXPECT_TRUE(write_f32<kBE>(buf_be, val));
  // Byte order should be reversed
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(buf_le[i], buf_be[3 - i]);
  }
}

TEST(WriteFloat32Test, ReturnsFalseOnSizeMismatch) {
  std::array<std::byte, 3> buf{};
  EXPECT_FALSE(write_f32<kLE>(buf, 1.0F));
}

TEST(WriteFloat32Test, RuntimeDispatch) {
  std::array<std::byte, 4> buf{};
  const float val = 123.0F;
  EXPECT_TRUE(enc::write_float32(buf, val, upr::ByteOrder::kLittleEndian));
  float recovered = 0.0F;
  std::memcpy(&recovered, buf.data(), sizeof(recovered));
  EXPECT_EQ(recovered, val);
}

TEST(WriteFloat64Test, RoundTripsLE) {
  std::array<std::byte, 8> buf{};
  const double original = std::numbers::e;
  EXPECT_TRUE(write_f64<kLE>(buf, original));
  double recovered = 0.0;
  std::memcpy(&recovered, buf.data(), sizeof(recovered));
  EXPECT_EQ(recovered, original);
}

TEST(WriteFloat64Test, RoundTripsBE) {
  std::array<std::byte, 8> buf_le{};
  std::array<std::byte, 8> buf_be{};
  const double val = -1e100;
  EXPECT_TRUE(write_f64<kLE>(buf_le, val));
  EXPECT_TRUE(write_f64<kBE>(buf_be, val));
  for (std::size_t i = 0; i < 8; ++i) {
    EXPECT_EQ(buf_le[i], buf_be[7 - i]);
  }
}

TEST(WriteFloat64Test, ReturnsFalseOnSizeMismatch) {
  std::array<std::byte, 7> buf{};
  EXPECT_FALSE(write_f64<kLE>(buf, 1.0));
}

TEST(WriteFloat64Test, RuntimeDispatch) {
  std::array<std::byte, 8> buf{};
  const double val = 99999.0;
  EXPECT_TRUE(enc::write_float64(buf, val, upr::ByteOrder::kBigEndian));
  // flip bytes and recover
  std::array<std::byte, 8> flipped{};
  for (std::size_t i = 0; i < 8; ++i) {
    flipped[i] = buf[7 - i];
  }
  double recovered = 0.0;
  std::memcpy(&recovered, flipped.data(), sizeof(recovered));
  EXPECT_EQ(recovered, val);
}

TEST(WriteBytesTest, CopiesBytes) {
  const std::array<std::byte, 4> src = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
  std::array<std::byte, 4> dst{};
  EXPECT_TRUE(enc::write_bytes(dst, src));
  EXPECT_EQ(dst, src);
}

TEST(WriteBytesTest, ReturnsFalseOnSizeMismatch) {
  const std::array<std::byte, 4> src{};
  std::array<std::byte, 3> dst{};
  EXPECT_FALSE(enc::write_bytes(dst, src));
}

TEST(WriteBytesTest, EmptySpanSucceeds) { EXPECT_TRUE(enc::write_bytes(upr::MutableByteSpan{}, upr::ByteSpan{})); }

TEST(FillZerosTest, ZeroesBuffer) {
  std::array<std::byte, 4> buf = {std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
  enc::fill_zeros(buf);
  for (const std::byte& b : buf) {
    EXPECT_EQ(b, std::byte{0x00});
  }
}

TEST(FillZerosTest, EmptySpanIsNoOp) {
  EXPECT_NO_THROW({
    enc::fill_zeros(upr::MutableByteSpan{});  // must not crash
  });
}

TEST(RoundTripTest, U32LERoundTrip) {
  std::array<std::byte, 4> buf{};
  constexpr uint32_t kVal = 0xCAFEBABEU;
  EXPECT_TRUE((write_u<kLE, 4>(buf, kVal)));
  uint32_t recovered = 0;
  std::memcpy(&recovered, buf.data(), sizeof(recovered));
  // bytes are in LE order -> memcpy gives the original value on LE machines;
  // use the LE reader for correctness on any platform:
  uint32_t from_bytes = 0;
  for (int i = 3; i >= 0; --i) {
    from_bytes = (from_bytes << 8U) | static_cast<uint8_t>(buf[i]);
  }
  EXPECT_EQ(from_bytes, kVal);
}

}  // namespace
