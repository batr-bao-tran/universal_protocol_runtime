#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__BYTE_VIEW_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__BYTE_VIEW_HPP_
#include <cstddef>
#include <cstdint>
#include <span>

namespace universal_protocol_runtime {

using ByteSpan = std::span<const std::byte>;
using MutableByteSpan = std::span<std::byte>;

inline ByteSpan as_byte_span(std::span<const uint8_t> bytes) noexcept { return std::as_bytes(bytes); }

inline MutableByteSpan as_writable_byte_span(std::span<uint8_t> bytes) noexcept {
  return std::as_writable_bytes(bytes);
}

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__BYTE_VIEW_HPP_
