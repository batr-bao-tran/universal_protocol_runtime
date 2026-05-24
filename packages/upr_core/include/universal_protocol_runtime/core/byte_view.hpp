#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__BYTE_VIEW_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__BYTE_VIEW_HPP_
#include <cstddef>
#include <cstdint>
#include <span>

namespace universal_protocol_runtime {

/**
 * @brief Borrowed immutable byte span used by runtime APIs.
 */
using ByteSpan = std::span<const std::byte>;
/**
 * @brief Borrowed mutable byte span used by runtime APIs.
 */
using MutableByteSpan = std::span<std::byte>;

/**
 * @brief Reinterprets a span of octets as an immutable byte span.
 * @param bytes Source octet span.
 * @return Immutable byte span over the same storage.
 */
inline ByteSpan as_byte_span(std::span<const uint8_t> bytes) noexcept { return std::as_bytes(bytes); }

/**
 * @brief Reinterprets a span of octets as a mutable byte span.
 * @param bytes Source octet span.
 * @return Mutable byte span over the same storage.
 */
inline MutableByteSpan as_writable_byte_span(std::span<uint8_t> bytes) noexcept {
  return std::as_writable_bytes(bytes);
}

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__BYTE_VIEW_HPP_
