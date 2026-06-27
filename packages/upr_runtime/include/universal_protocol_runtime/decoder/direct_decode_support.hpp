#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DIRECT_DECODE_SUPPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DIRECT_DECODE_SUPPORT_HPP_

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/core/types.hpp"
#include "universal_protocol_runtime/core/unreachable.hpp"
#include "universal_protocol_runtime/decoder/detail/runtime_byte_ops.hpp"

namespace universal_protocol_runtime::direct_decode_support {
namespace detail {

constexpr bool is_valid_ascii_scalar(ByteSpan bytes) noexcept {
  for (const std::byte byte : bytes) {
    if ((std::to_integer<uint8_t>(byte) & 0x80U) != 0U) {
      return false;
    }
  }
  return true;
}

constexpr uint8_t checksum_xor8_scalar(ByteSpan bytes) noexcept {
  uint8_t value = 0;
  for (const std::byte byte : bytes) {
    value ^= std::to_integer<uint8_t>(byte);
  }
  return value;
}

constexpr uint16_t checksum_sum16_scalar(ByteSpan bytes) noexcept {
  uint32_t sum = 0;
  for (const std::byte byte : bytes) {
    sum += std::to_integer<uint8_t>(byte);
  }
  return static_cast<uint16_t>(sum & 0xFFFFU);
}

}  // namespace detail

inline constexpr uint8_t kMaxScalarWidthBytes = sizeof(uint64_t);
inline constexpr uint8_t kMaxBitWidth = 64U;
inline constexpr uint8_t kAsciiHighBitMask = 0x80U;
inline constexpr uint8_t kUtf8TwoByteLeadMask = 0xE0U;
inline constexpr uint8_t kUtf8ThreeByteLeadMask = 0xF0U;
inline constexpr uint8_t kUtf8FourByteLeadMask = 0xF8U;
inline constexpr uint8_t kUtf8ContinuationMask = 0xC0U;
inline constexpr uint8_t kUtf8ContinuationTag = 0x80U;
inline constexpr uint8_t kUtf8TwoByteLeadTag = 0xC0U;
inline constexpr uint8_t kUtf8ThreeByteLeadTag = 0xE0U;
inline constexpr uint8_t kUtf8FourByteLeadTag = 0xF0U;
inline constexpr uint8_t kUtf8SingleByteMask = 0x80U;
inline constexpr uint8_t kUtf8TwoBytePayloadMask = 0x1FU;
inline constexpr uint8_t kUtf8ThreeBytePayloadMask = 0x0FU;
inline constexpr uint8_t kUtf8FourBytePayloadMask = 0x07U;
inline constexpr uint8_t kUtf8ContinuationPayloadMask = 0x3FU;
inline constexpr uint32_t kUtf8MaxCodePoint = 0x10FFFFU;
inline constexpr uint32_t kUtf8SurrogateStart = 0xD800U;
inline constexpr uint32_t kUtf8SurrogateEnd = 0xDFFFU;
inline constexpr uint32_t kUtf8MinCodePointTwoByte = 0x80U;
inline constexpr uint32_t kUtf8MinCodePointThreeByte = 0x800U;
inline constexpr uint32_t kUtf8MinCodePointFourByte = 0x10000U;

inline constexpr size_t kCrcTableEntries = 256U;
inline constexpr uint16_t kCrc16CcittPolynomial = 0x8408U;
inline constexpr uint32_t kCrc32Polynomial = 0xEDB88320U;
inline constexpr uint32_t kCrc32cPolynomial = 0x82F63B78U;
inline constexpr uint16_t kCrc16InitialValue = 0xFFFFU;
inline constexpr uint32_t kCrc32InitialValue = 0xFFFFFFFFU;

template <std::size_t WidthBytes>
consteval void validate_scalar_width() {
  static_assert(WidthBytes > 0, "Scalar width must be at least one byte.");
  static_assert(WidthBytes <= kMaxScalarWidthBytes, "Scalar width exceeds the direct-decode limit.");
}

constexpr uint64_t byte_to_u64(std::byte byte) noexcept {
  return static_cast<uint64_t>(std::to_integer<uint8_t>(byte));
}

template <typename UIntType>
constexpr std::array<UIntType, kCrcTableEntries> make_crc_table(UIntType polynomial) {
  std::array<UIntType, kCrcTableEntries> table{};
  for (size_t index = 0; index < table.size(); ++index) {
    auto value = static_cast<UIntType>(index);
    for (size_t bit = 0; bit < kBitsPerByte; ++bit) {
      value =
          (value & 1U) != 0U ? static_cast<UIntType>((value >> 1U) ^ polynomial) : static_cast<UIntType>(value >> 1U);
    }
    table[index] = value;
  }
  return table;
}

inline constexpr std::array<uint16_t, kCrcTableEntries> kCrc16CcittTable =
    make_crc_table<uint16_t>(kCrc16CcittPolynomial);
inline constexpr std::array<uint32_t, kCrcTableEntries> kCrc32Table = make_crc_table<uint32_t>(kCrc32Polynomial);
inline constexpr std::array<uint32_t, kCrcTableEntries> kCrc32cTable = make_crc_table<uint32_t>(kCrc32cPolynomial);

template <ByteOrder byte_order, std::size_t WidthBytes>
constexpr uint64_t read_unsigned_scalar_unchecked(ByteSpan bytes) noexcept {
  validate_scalar_width<WidthBytes>();
  uint64_t value = 0;
  if constexpr (byte_order == ByteOrder::kLittleEndian) {
    for (std::size_t index = 0; index < WidthBytes; ++index) {
      value |= byte_to_u64(bytes[index]) << (index * kBitsPerByte);
    }
  } else {
    for (std::size_t index = 0; index < WidthBytes; ++index) {
      value = (value << kBitsPerByte) | byte_to_u64(bytes[index]);
    }
  }
  return value;
}

/**
 * @brief Reads an unsigned scalar with a compile-time byte order and width.
 * @tparam byte_order Wire byte order.
 * @tparam WidthBytes Scalar width in bytes.
 * @param bytes Source bytes to decode.
 * @return Decoded scalar when the span has the expected width.
 */
template <ByteOrder byte_order, std::size_t WidthBytes>
constexpr std::optional<uint64_t> read_unsigned_scalar(ByteSpan bytes) noexcept {
  validate_scalar_width<WidthBytes>();
  if (bytes.size() != WidthBytes) {
    return std::nullopt;
  }
  return read_unsigned_scalar_unchecked<byte_order, WidthBytes>(bytes);
}

/**
 * @brief Reads an unsigned scalar with a compile-time byte order and runtime width.
 * @tparam byte_order Wire byte order.
 * @param bytes Source bytes to decode.
 * @return Decoded scalar when the width is supported.
 */
template <ByteOrder byte_order>
constexpr std::optional<uint64_t> read_unsigned_scalar(ByteSpan bytes) noexcept {
  switch (bytes.size()) {
    case 1:
      return read_unsigned_scalar<byte_order, 1>(bytes);
    case 2:
      return read_unsigned_scalar<byte_order, 2>(bytes);
    case 3:
      return read_unsigned_scalar<byte_order, 3>(bytes);
    case 4:
      return read_unsigned_scalar<byte_order, 4>(bytes);
    case 5:
      return read_unsigned_scalar<byte_order, 5>(bytes);
    case 6:
      return read_unsigned_scalar<byte_order, 6>(bytes);
    case 7:
      return read_unsigned_scalar<byte_order, 7>(bytes);
    case 8:
      return read_unsigned_scalar<byte_order, 8>(bytes);
    default:
      return std::nullopt;
  }
}

/**
 * @brief Reads an unsigned scalar with runtime byte-order dispatch.
 * @param bytes Source bytes to decode.
 * @param byte_order Wire byte order.
 * @return Decoded scalar when the width and order are supported.
 */
constexpr std::optional<uint64_t> read_unsigned_scalar(ByteSpan bytes, ByteOrder byte_order) noexcept {
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      return read_unsigned_scalar<ByteOrder::kLittleEndian>(bytes);
    case ByteOrder::kBigEndian:
      return read_unsigned_scalar<ByteOrder::kBigEndian>(bytes);
  }
  unreachable();  // LCOV_EXCL_LINE
}

/**
 * @brief Sign-extends a raw unsigned value to a signed integer width.
 * @param value Raw field value.
 * @param width_bits Significant width in bits.
 * @return Sign-extended value when the width is valid.
 */
constexpr std::optional<int64_t> sign_extend(uint64_t value, uint8_t width_bits) noexcept {
  if (width_bits == 0 || width_bits > kMaxBitWidth) {
    return std::nullopt;
  }
  if (width_bits == kMaxBitWidth) {
    return static_cast<int64_t>(value);
  }
  const uint64_t sign_mask = 1ULL << (width_bits - 1U);
  const uint64_t full_mask = (1ULL << width_bits) - 1ULL;
  value &= full_mask;
  if ((value & sign_mask) == 0U) {
    return static_cast<int64_t>(value);
  }
  return static_cast<int64_t>(value | ~full_mask);
}

/**
 * @brief Reads a 32-bit floating-point value with a compile-time byte order.
 * @tparam byte_order Wire byte order.
 * @param bytes Source bytes to decode.
 * @return Decoded float when the span has the expected width.
 */
template <ByteOrder byte_order>
constexpr std::optional<float> read_float32(ByteSpan bytes) noexcept {
  const auto raw = read_unsigned_scalar<byte_order, sizeof(uint32_t)>(bytes);
  if (!raw.has_value() || bytes.size() != sizeof(uint32_t)) {
    return std::nullopt;
  }
  return std::bit_cast<float>(static_cast<uint32_t>(*raw));
}

/**
 * @brief Reads a 32-bit floating-point value with runtime byte-order dispatch.
 * @param bytes Source bytes to decode.
 * @param byte_order Wire byte order.
 * @return Decoded float when the span has the expected width.
 */
constexpr std::optional<float> read_float32(ByteSpan bytes, ByteOrder byte_order) noexcept {
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      return read_float32<ByteOrder::kLittleEndian>(bytes);
    case ByteOrder::kBigEndian:
      return read_float32<ByteOrder::kBigEndian>(bytes);
  }
  unreachable();
}

/**
 * @brief Reads a 64-bit floating-point value with a compile-time byte order.
 * @tparam byte_order Wire byte order.
 * @param bytes Source bytes to decode.
 * @return Decoded double when the span has the expected width.
 */
template <ByteOrder byte_order>
constexpr std::optional<double> read_float64(ByteSpan bytes) noexcept {
  const auto raw = read_unsigned_scalar<byte_order, sizeof(uint64_t)>(bytes);
  if (!raw.has_value() || bytes.size() != sizeof(uint64_t)) {
    return std::nullopt;
  }
  return std::bit_cast<double>(*raw);
}

/**
 * @brief Reads a 64-bit floating-point value with runtime byte-order dispatch.
 * @param bytes Source bytes to decode.
 * @param byte_order Wire byte order.
 * @return Decoded double when the span has the expected width.
 */
constexpr std::optional<double> read_float64(ByteSpan bytes, ByteOrder byte_order) noexcept {
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      return read_float64<ByteOrder::kLittleEndian>(bytes);
    case ByteOrder::kBigEndian:
      return read_float64<ByteOrder::kBigEndian>(bytes);
  }
  unreachable();
}

/**
 * @brief Tests whether a byte span starts with a fixed prefix.
 * @tparam Extent Prefix width in bytes.
 * @param bytes Candidate byte span.
 * @param prefix Required prefix bytes.
 * @return `true` when the prefix matches.
 */
template <std::size_t Extent>
constexpr bool starts_with(ByteSpan bytes, const std::array<std::byte, Extent>& prefix) noexcept {
  if (bytes.size() < Extent) {
    return false;
  }
  for (std::size_t index = 0; index < Extent; ++index) {
    if (bytes[index] != prefix[index]) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Validates that a byte span is pure 7-bit ASCII.
 * @param bytes Candidate byte span.
 * @return `true` when every byte is valid ASCII.
 */
constexpr bool is_valid_ascii(ByteSpan bytes) noexcept { return detail::is_valid_ascii_scalar(bytes); }

/**
 * @brief Validates ASCII using runtime-optimized routines when available.
 * @param bytes Candidate byte span.
 * @return `true` when every byte is valid ASCII.
 */
inline bool runtime_is_valid_ascii(ByteSpan bytes) noexcept { return detail::runtime_is_valid_ascii(bytes); }

/**
 * @brief Validates UTF-8 using runtime-optimized routines when available.
 * @param bytes Candidate byte span.
 * @return `true` when the byte span is valid UTF-8.
 */
inline bool runtime_is_valid_utf8(ByteSpan bytes) noexcept { return detail::runtime_is_valid_utf8(bytes); }

constexpr bool is_valid_utf8(ByteSpan bytes) noexcept;

/**
 * @brief Validates a string field for the configured encoding using runtime helpers.
 * @tparam encoding String encoding to validate.
 * @param bytes Candidate byte span.
 * @return `true` when the bytes satisfy the encoding.
 */
template <StringEncoding encoding>
inline bool runtime_validate_string(ByteSpan bytes) noexcept {
  if constexpr (encoding == StringEncoding::kAscii) {
    return runtime_is_valid_ascii(bytes);
  } else {
    return runtime_is_valid_utf8(bytes);
  }
}

/**
 * @brief Validates that a byte span is well-formed UTF-8.
 * @param bytes Candidate byte span.
 * @return `true` when the byte span is valid UTF-8.
 */
constexpr bool is_valid_utf8(ByteSpan bytes) noexcept {
  size_t index = 0;
  while (index < bytes.size()) {
    const auto lead = std::to_integer<uint8_t>(bytes[index]);
    size_t continuation_count = 0;
    uint32_t code_point = 0;

    if ((lead & kUtf8SingleByteMask) == 0U) {
      continuation_count = 0;
      code_point = lead;
    } else if ((lead & kUtf8TwoByteLeadMask) == kUtf8TwoByteLeadTag) {
      continuation_count = 1;
      code_point = lead & kUtf8TwoBytePayloadMask;
      if (code_point == 0U) {
        return false;
      }
    } else if ((lead & kUtf8ThreeByteLeadMask) == kUtf8ThreeByteLeadTag) {
      continuation_count = 2;
      code_point = lead & kUtf8ThreeBytePayloadMask;
    } else if ((lead & kUtf8FourByteLeadMask) == kUtf8FourByteLeadTag) {
      continuation_count = 3;
      code_point = lead & kUtf8FourBytePayloadMask;
    } else {
      return false;
    }

    if (index + continuation_count >= bytes.size()) {
      return false;
    }
    for (size_t continuation = 0; continuation < continuation_count; ++continuation) {
      const auto next = std::to_integer<uint8_t>(bytes[index + continuation + 1U]);
      if ((next & kUtf8ContinuationMask) != kUtf8ContinuationTag) {
        return false;
      }
      code_point = (code_point << 6U) | static_cast<uint32_t>(next & kUtf8ContinuationPayloadMask);
    }

    if ((continuation_count == 1U && code_point < kUtf8MinCodePointTwoByte) ||
        (continuation_count == 2U && code_point < kUtf8MinCodePointThreeByte) ||
        (continuation_count == 3U && code_point < kUtf8MinCodePointFourByte) || code_point > kUtf8MaxCodePoint ||
        (code_point >= kUtf8SurrogateStart && code_point <= kUtf8SurrogateEnd)) {
      return false;
    }

    index += continuation_count + 1U;
  }
  return true;
}

template <StringEncoding encoding>
constexpr bool validate_string(ByteSpan bytes) noexcept {
  if constexpr (encoding == StringEncoding::kAscii) {
    return is_valid_ascii(bytes);
  } else {
    return is_valid_utf8(bytes);
  }
}

constexpr uint64_t checksum_crc16_ccitt(ByteSpan bytes) noexcept {
  uint16_t crc = kCrc16InitialValue;
  for (const std::byte byte : bytes) {
    const auto index = static_cast<uint8_t>(crc ^ static_cast<uint16_t>(std::to_integer<uint8_t>(byte)));
    crc = static_cast<uint16_t>((crc >> kBitsPerByte) ^ kCrc16CcittTable[index]);
  }
  return static_cast<uint16_t>(~crc);
}

constexpr uint64_t checksum_crc32(ByteSpan bytes) noexcept {
  uint32_t crc = kCrc32InitialValue;
  for (const std::byte byte : bytes) {
    const auto index = static_cast<uint8_t>(crc ^ static_cast<uint32_t>(std::to_integer<uint8_t>(byte)));
    crc = (crc >> kBitsPerByte) ^ kCrc32Table[index];
  }
  return ~crc;
}

constexpr uint64_t checksum_crc32c(ByteSpan bytes) noexcept {
  uint32_t crc = kCrc32InitialValue;
  for (const std::byte byte : bytes) {
    const auto index = static_cast<uint8_t>(crc ^ static_cast<uint32_t>(std::to_integer<uint8_t>(byte)));
    crc = (crc >> kBitsPerByte) ^ kCrc32cTable[index];
  }
  return ~crc;
}

constexpr uint64_t checksum_xor8(ByteSpan bytes) noexcept { return detail::checksum_xor8_scalar(bytes); }

constexpr uint64_t checksum_sum16(ByteSpan bytes) noexcept { return detail::checksum_sum16_scalar(bytes); }

inline uint64_t runtime_checksum_xor8(ByteSpan bytes) noexcept { return detail::runtime_checksum_xor8(bytes); }

inline uint64_t runtime_checksum_sum16(ByteSpan bytes) noexcept { return detail::runtime_checksum_sum16(bytes); }

inline uint64_t runtime_checksum_crc16_ccitt(ByteSpan bytes) noexcept { return checksum_crc16_ccitt(bytes); }

inline uint64_t runtime_checksum_crc32(ByteSpan bytes) noexcept { return checksum_crc32(bytes); }

inline uint64_t runtime_checksum_crc32c(ByteSpan bytes) noexcept { return detail::runtime_checksum_crc32c(bytes); }

#ifdef UPR_RUNTIME_SCALAR_ONLY
namespace detail {

// Header-only scalar definitions of the runtime helpers. These satisfy the
// declarations in runtime_byte_ops.hpp without pulling in simdutf/Highway/crc32c,
// so generated bindings compile and link with only a C++20 compiler.
inline bool runtime_is_valid_ascii(ByteSpan bytes) noexcept { return is_valid_ascii_scalar(bytes); }

inline bool runtime_is_valid_utf8(ByteSpan bytes) noexcept {
  return universal_protocol_runtime::direct_decode_support::is_valid_utf8(bytes);
}

inline uint8_t runtime_checksum_xor8(ByteSpan bytes) noexcept { return checksum_xor8_scalar(bytes); }

inline uint16_t runtime_checksum_sum16(ByteSpan bytes) noexcept { return checksum_sum16_scalar(bytes); }

inline uint32_t runtime_checksum_crc32c(ByteSpan bytes) noexcept {
  return static_cast<uint32_t>(universal_protocol_runtime::direct_decode_support::checksum_crc32c(bytes));
}

}  // namespace detail
#endif  // UPR_RUNTIME_SCALAR_ONLY

}  // namespace universal_protocol_runtime::direct_decode_support

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DIRECT_DECODE_SUPPORT_HPP_
