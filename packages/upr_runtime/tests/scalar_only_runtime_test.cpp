// Verifies the header-only scalar runtime path compiles and links without the
// SIMD stack (simdutf / Highway / crc32c). This translation unit intentionally
// depends only on the runtime headers, never on //packages/upr_runtime, so a
// link failure here means a generated-bindings consumer would be forced to pull
// in the SIMD libraries.
#define UPR_RUNTIME_SCALAR_ONLY 1

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "gtest/gtest.h"
#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/decoder/direct_decode_support.hpp"
#include "universal_protocol_runtime/encoder/direct_encode_support.hpp"

namespace {

namespace dd = universal_protocol_runtime::direct_decode_support;
namespace de = universal_protocol_runtime::direct_encode_support;

universal_protocol_runtime::ByteSpan as_span(std::string_view text) {
  return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

TEST(ScalarOnlyRuntime, MacroIsActive) {
#ifdef UPR_RUNTIME_SCALAR_ONLY
  SUCCEED();
#else
  FAIL() << "UPR_RUNTIME_SCALAR_ONLY should be defined in this translation unit";
#endif
}

TEST(ScalarOnlyRuntime, ValidatesAsciiAndUtf8) {
  EXPECT_TRUE(dd::runtime_is_valid_ascii(as_span("hello world")));
  EXPECT_FALSE(dd::runtime_is_valid_ascii(as_span("caf\xC3\xA9")));
  EXPECT_TRUE(dd::runtime_is_valid_utf8(as_span("caf\xC3\xA9")));
  EXPECT_FALSE(dd::runtime_is_valid_utf8(as_span("\xC3\x28")));
}

TEST(ScalarOnlyRuntime, ChecksumsMatchConstexprReference) {
  const auto span = as_span("123456789");
  EXPECT_EQ(dd::runtime_checksum_xor8(span), dd::checksum_xor8(span));
  EXPECT_EQ(dd::runtime_checksum_sum16(span), dd::checksum_sum16(span));
  EXPECT_EQ(dd::runtime_checksum_crc32c(span), dd::checksum_crc32c(span));
  // CRC32C of the canonical "123456789" check value.
  EXPECT_EQ(dd::runtime_checksum_crc32c(span), 0xE3069283ULL);
}

TEST(ScalarOnlyRuntime, EncodeDecodeRoundTrip) {
  std::array<std::byte, sizeof(uint32_t)> buffer{};
  const bool wrote = de::write_unsigned_scalar<universal_protocol_runtime::ByteOrder::kLittleEndian, sizeof(uint32_t)>(
      universal_protocol_runtime::MutableByteSpan(buffer.data(), buffer.size()), 0x11223344U);
  EXPECT_TRUE(wrote);
  const auto decoded = dd::read_unsigned_scalar<universal_protocol_runtime::ByteOrder::kLittleEndian, sizeof(uint32_t)>(
      universal_protocol_runtime::ByteSpan(buffer.data(), buffer.size()));
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded, 0x11223344U);
}

}  // namespace
