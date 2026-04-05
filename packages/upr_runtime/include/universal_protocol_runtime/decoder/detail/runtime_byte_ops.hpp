#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER_DETAIL__RUNTIME_BYTE_OPS_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER_DETAIL__RUNTIME_BYTE_OPS_HPP_

#include <cstddef>
#include <cstdint>

#include "universal_protocol_runtime/core/byte_view.hpp"

namespace universal_protocol_runtime::direct_decode_support::detail {

inline constexpr std::size_t kAsciiSimdThresholdBytes = 32U;
inline constexpr std::size_t kUtf8SimdThresholdBytes = 32U;
inline constexpr std::size_t kChecksumSimdThresholdBytes = 64U;
inline constexpr std::size_t kCrc32cSimdThresholdBytes = 64U;

bool runtime_is_valid_ascii(ByteSpan bytes) noexcept;
bool runtime_is_valid_utf8(ByteSpan bytes) noexcept;
uint8_t runtime_checksum_xor8(ByteSpan bytes) noexcept;
uint16_t runtime_checksum_sum16(ByteSpan bytes) noexcept;
uint32_t runtime_checksum_crc32c(ByteSpan bytes) noexcept;

}  // namespace universal_protocol_runtime::direct_decode_support::detail

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER_DETAIL__RUNTIME_BYTE_OPS_HPP_
