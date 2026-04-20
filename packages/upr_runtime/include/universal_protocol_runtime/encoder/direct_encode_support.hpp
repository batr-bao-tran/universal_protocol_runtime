#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__DIRECT_ENCODE_SUPPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__DIRECT_ENCODE_SUPPORT_HPP_

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/core/types.hpp"
#include "universal_protocol_runtime/core/unreachable.hpp"
#include "universal_protocol_runtime/decoder/direct_decode_support.hpp"

// Low-level write primitives that mirror direct_decode_support.hpp's read
// primitives. All template variants are constexpr / always-inline so the
// compiler can constant-fold widths and emit a single scalar store or a pair
// of SIMD stores on wide platforms—no overhead beyond a bounds check.

namespace universal_protocol_runtime::direct_encode_support {

inline constexpr uint8_t kMaxScalarWidthBytes = sizeof(uint64_t);

[[nodiscard]] constexpr uint16_t byteswap16(uint16_t value) noexcept {
  return static_cast<uint16_t>((value << 8U) | (value >> 8U));
}

[[nodiscard]] constexpr uint32_t byteswap32(uint32_t value) noexcept {
  return ((value & 0x000000FFU) << 24U) | ((value & 0x0000FF00U) << 8U) | ((value & 0x00FF0000U) >> 8U) |
         ((value & 0xFF000000U) >> 24U);
}

[[nodiscard]] constexpr uint64_t byteswap64(uint64_t value) noexcept {
  return ((value & 0x00000000000000FFULL) << 56U) | ((value & 0x000000000000FF00ULL) << 40U) |
         ((value & 0x0000000000FF0000ULL) << 24U) | ((value & 0x00000000FF000000ULL) << 8U) |
         ((value & 0x000000FF00000000ULL) >> 8U) | ((value & 0x0000FF0000000000ULL) >> 24U) |
         ((value & 0x00FF000000000000ULL) >> 40U) | ((value & 0xFF00000000000000ULL) >> 56U);
}

template <std::size_t WidthBytes>
consteval void validate_scalar_width() {
  static_assert(WidthBytes > 0, "Scalar width must be at least one byte.");
  static_assert(WidthBytes <= kMaxScalarWidthBytes, "Scalar width exceeds the direct-encode limit.");
}

template <ByteOrder byte_order, std::size_t WidthBytes>
[[gnu::always_inline]] constexpr void write_unsigned_scalar_unchecked(MutableByteSpan bytes, uint64_t value) noexcept {
  validate_scalar_width<WidthBytes>();
  if constexpr (byte_order == ByteOrder::kLittleEndian) {
    if constexpr (WidthBytes == 1) {
      bytes[0] = static_cast<std::byte>(value);
    } else if constexpr (WidthBytes == 2) {
      const auto narrowed = static_cast<uint16_t>(value);
      std::memcpy(bytes.data(), &narrowed, sizeof(narrowed));
    } else if constexpr (WidthBytes == 4) {
      const auto narrowed = static_cast<uint32_t>(value);
      std::memcpy(bytes.data(), &narrowed, sizeof(narrowed));
    } else if constexpr (WidthBytes == 8) {
      const auto narrowed = static_cast<uint64_t>(value);
      std::memcpy(bytes.data(), &narrowed, sizeof(narrowed));
    } else {
      for (std::size_t i = 0; i < WidthBytes; ++i) {
        bytes[i] = static_cast<std::byte>(value & 0xFFU);
        value >>= 8U;
      }
    }
  } else {
    if constexpr (WidthBytes == 1) {
      bytes[0] = static_cast<std::byte>(value);
    } else if constexpr (WidthBytes == 2) {
      const auto narrowed = byteswap16(static_cast<uint16_t>(value));
      std::memcpy(bytes.data(), &narrowed, sizeof(narrowed));
    } else if constexpr (WidthBytes == 4) {
      const auto narrowed = byteswap32(static_cast<uint32_t>(value));
      std::memcpy(bytes.data(), &narrowed, sizeof(narrowed));
    } else if constexpr (WidthBytes == 8) {
      const auto narrowed = byteswap64(static_cast<uint64_t>(value));
      std::memcpy(bytes.data(), &narrowed, sizeof(narrowed));
    } else {
      for (std::size_t i = WidthBytes; i > 0; --i) {
        bytes[i - 1] = static_cast<std::byte>(value & 0xFFU);
        value >>= 8U;
      }
    }
  }
}

// Returns false (and writes nothing) if the span has the wrong size.
template <ByteOrder byte_order, std::size_t WidthBytes>
[[nodiscard]] constexpr bool write_unsigned_scalar(MutableByteSpan bytes, uint64_t value) noexcept {
  validate_scalar_width<WidthBytes>();
  if (bytes.size() != WidthBytes) {
    return false;
  }
  write_unsigned_scalar_unchecked<byte_order, WidthBytes>(bytes, value);
  return true;
}

// Runtime-dispatch version: width determined at run time.
template <ByteOrder byte_order>
[[nodiscard]] constexpr bool write_unsigned_scalar(MutableByteSpan bytes, uint64_t value) noexcept {
  switch (bytes.size()) {
    case 1:
      return write_unsigned_scalar<byte_order, 1>(bytes, value);
    case 2:
      return write_unsigned_scalar<byte_order, 2>(bytes, value);
    case 3:
      return write_unsigned_scalar<byte_order, 3>(bytes, value);
    case 4:
      return write_unsigned_scalar<byte_order, 4>(bytes, value);
    case 5:
      return write_unsigned_scalar<byte_order, 5>(bytes, value);
    case 6:
      return write_unsigned_scalar<byte_order, 6>(bytes, value);
    case 7:
      return write_unsigned_scalar<byte_order, 7>(bytes, value);
    case 8:
      return write_unsigned_scalar<byte_order, 8>(bytes, value);
    default:
      return false;
  }
}

template <ByteOrder byte_order>
[[gnu::always_inline]] constexpr void write_unsigned_scalar_unchecked(MutableByteSpan bytes, uint64_t value) noexcept {
  switch (bytes.size()) {
    case 1:
      write_unsigned_scalar_unchecked<byte_order, 1>(bytes, value);
      return;
    case 2:
      write_unsigned_scalar_unchecked<byte_order, 2>(bytes, value);
      return;
    case 3:
      write_unsigned_scalar_unchecked<byte_order, 3>(bytes, value);
      return;
    case 4:
      write_unsigned_scalar_unchecked<byte_order, 4>(bytes, value);
      return;
    case 5:
      write_unsigned_scalar_unchecked<byte_order, 5>(bytes, value);
      return;
    case 6:
      write_unsigned_scalar_unchecked<byte_order, 6>(bytes, value);
      return;
    case 7:
      write_unsigned_scalar_unchecked<byte_order, 7>(bytes, value);
      return;
    case 8:
      write_unsigned_scalar_unchecked<byte_order, 8>(bytes, value);
      return;
    default:
      unreachable();
  }
}

// Full runtime dispatch (byte_order and width both dynamic).
[[nodiscard]] constexpr bool write_unsigned_scalar(MutableByteSpan bytes,
                                                   uint64_t value,
                                                   ByteOrder byte_order) noexcept {
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      return write_unsigned_scalar<ByteOrder::kLittleEndian>(bytes, value);
    case ByteOrder::kBigEndian:
      return write_unsigned_scalar<ByteOrder::kBigEndian>(bytes, value);
  }
  unreachable();
}

[[gnu::always_inline]] constexpr void write_unsigned_scalar_unchecked(MutableByteSpan bytes,
                                                                      uint64_t value,
                                                                      ByteOrder byte_order) noexcept {
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      write_unsigned_scalar_unchecked<ByteOrder::kLittleEndian>(bytes, value);
      return;
    case ByteOrder::kBigEndian:
      write_unsigned_scalar_unchecked<ByteOrder::kBigEndian>(bytes, value);
      return;
  }
  unreachable();
}

template <ByteOrder byte_order>
[[nodiscard]] constexpr bool write_float32(MutableByteSpan bytes, float value) noexcept {
  return write_unsigned_scalar<byte_order, sizeof(uint32_t)>(bytes, std::bit_cast<uint32_t>(value));
}

template <ByteOrder byte_order>
[[gnu::always_inline]] constexpr void write_float32_unchecked(MutableByteSpan bytes, float value) noexcept {
  write_unsigned_scalar_unchecked<byte_order, sizeof(uint32_t)>(bytes, std::bit_cast<uint32_t>(value));
}

[[nodiscard]] constexpr bool write_float32(MutableByteSpan bytes, float value, ByteOrder byte_order) noexcept {
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      return write_float32<ByteOrder::kLittleEndian>(bytes, value);
    case ByteOrder::kBigEndian:
      return write_float32<ByteOrder::kBigEndian>(bytes, value);
  }
  unreachable();
}

template <ByteOrder byte_order>
[[nodiscard]] constexpr bool write_float64(MutableByteSpan bytes, double value) noexcept {
  return write_unsigned_scalar<byte_order, sizeof(uint64_t)>(bytes, std::bit_cast<uint64_t>(value));
}

template <ByteOrder byte_order>
[[gnu::always_inline]] constexpr void write_float64_unchecked(MutableByteSpan bytes, double value) noexcept {
  write_unsigned_scalar_unchecked<byte_order, sizeof(uint64_t)>(bytes, std::bit_cast<uint64_t>(value));
}

[[nodiscard]] constexpr bool write_float64(MutableByteSpan bytes, double value, ByteOrder byte_order) noexcept {
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      return write_float64<ByteOrder::kLittleEndian>(bytes, value);
    case ByteOrder::kBigEndian:
      return write_float64<ByteOrder::kBigEndian>(bytes, value);
  }
  unreachable();
}

// Copy src bytes into dest. Returns false if sizes don't match.
[[nodiscard]] constexpr bool write_bytes(MutableByteSpan dest, ByteSpan src) noexcept {
  if (dest.size() != src.size()) {
    return false;
  }
  if (!src.empty()) {
    std::memcpy(dest.data(), src.data(), src.size());
  }
  return true;
}

[[gnu::always_inline]] constexpr void write_bytes_unchecked(MutableByteSpan dest, ByteSpan src) noexcept {
  if (!src.empty()) {
    std::memcpy(dest.data(), src.data(), src.size());
  }
}

// Zero-fill a span (used to initialise checksum placeholder slots before
// the final checksum values are computed and written back).
[[gnu::always_inline]] constexpr void fill_zeros(MutableByteSpan bytes) noexcept {
  if (!bytes.empty()) {
    std::memset(bytes.data(), 0, bytes.size());
  }
}

template <std::size_t SizeBytes>
[[gnu::always_inline]] constexpr uint8_t checksum_xor8_fixed(const std::byte* data) noexcept {
  uint8_t value = 0;
  for (std::size_t index = 0; index < SizeBytes; ++index) {
    value ^= std::to_integer<uint8_t>(data[index]);
  }
  return value;
}

template <std::size_t SizeBytes>
[[gnu::always_inline]] constexpr uint16_t checksum_sum16_fixed(const std::byte* data) noexcept {
  uint32_t sum = 0;
  for (std::size_t index = 0; index < SizeBytes; ++index) {
    sum += std::to_integer<uint8_t>(data[index]);
  }
  return static_cast<uint16_t>(sum & 0xFFFFU);
}

}  // namespace universal_protocol_runtime::direct_encode_support

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ENCODER__DIRECT_ENCODE_SUPPORT_HPP_
