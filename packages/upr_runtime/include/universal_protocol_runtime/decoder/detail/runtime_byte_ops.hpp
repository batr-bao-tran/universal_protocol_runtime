#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER_DETAIL__RUNTIME_BYTE_OPS_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER_DETAIL__RUNTIME_BYTE_OPS_HPP_

#include <cstddef>
#include <cstdint>

#include "universal_protocol_runtime/core/byte_view.hpp"

// Scalar-only selection.
//
// The runtime validation and checksum helpers below have two implementations:
//   * An out-of-line SIMD-accelerated definition in runtime_byte_ops.cpp that
//     links against simdutf, Highway and crc32c.
//   * A header-only scalar fallback (see direct_decode_support.hpp).
//
// A consumer of the *generated* bindings only ever includes headers, so it must
// be able to compile and link without the SIMD stack. Defining
// `UPR_RUNTIME_SCALAR_ONLY` forces the header-only scalar path. When the macro
// is left unset we auto-select: if the SIMD headers are not visible on the
// include path we fall back to scalar automatically, so the fast path is opt-in
// rather than mandatory. Define `UPR_RUNTIME_FORCE_SIMD` to require the
// out-of-line SIMD definitions even when the headers are not directly visible
// (used by the runtime library translation units themselves).
#if !defined(UPR_RUNTIME_SCALAR_ONLY) && !defined(UPR_RUNTIME_FORCE_SIMD)
#if defined(__has_include)
#if !__has_include("simdutf.h")
#define UPR_RUNTIME_SCALAR_ONLY 1
#endif
#endif
#endif

namespace universal_protocol_runtime::direct_decode_support::detail {

/**
 * @brief Minimum size at which runtime ASCII validation may switch to SIMD helpers.
 */
inline constexpr std::size_t kAsciiSimdThresholdBytes = 32U;
/**
 * @brief Minimum size at which runtime UTF-8 validation may switch to SIMD helpers.
 */
inline constexpr std::size_t kUtf8SimdThresholdBytes = 32U;
/**
 * @brief Minimum size at which runtime checksum helpers may switch to SIMD helpers.
 */
inline constexpr std::size_t kChecksumSimdThresholdBytes = 64U;
/**
 * @brief Minimum size at which runtime CRC32C helpers may switch to SIMD helpers.
 */
inline constexpr std::size_t kCrc32cSimdThresholdBytes = 64U;

/**
 * @brief Validates ASCII using runtime-specific optimized routines.
 * @param bytes Candidate byte span.
 * @return `true` when the span is valid ASCII.
 */
bool runtime_is_valid_ascii(ByteSpan bytes) noexcept;
/**
 * @brief Validates UTF-8 using runtime-specific optimized routines.
 * @param bytes Candidate byte span.
 * @return `true` when the span is valid UTF-8.
 */
bool runtime_is_valid_utf8(ByteSpan bytes) noexcept;
/**
 * @brief Computes an XOR-8 checksum using runtime-specific optimized routines.
 * @param bytes Source bytes to checksum.
 * @return XOR-8 checksum value.
 */
uint8_t runtime_checksum_xor8(ByteSpan bytes) noexcept;
/**
 * @brief Computes a SUM-16 checksum using runtime-specific optimized routines.
 * @param bytes Source bytes to checksum.
 * @return SUM-16 checksum value.
 */
uint16_t runtime_checksum_sum16(ByteSpan bytes) noexcept;
/**
 * @brief Computes a CRC32C checksum using runtime-specific optimized routines.
 * @param bytes Source bytes to checksum.
 * @return CRC32C checksum value.
 */
uint32_t runtime_checksum_crc32c(ByteSpan bytes) noexcept;

}  // namespace universal_protocol_runtime::direct_decode_support::detail

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER_DETAIL__RUNTIME_BYTE_OPS_HPP_
