#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__TYPES_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__TYPES_HPP_
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace universal_protocol_runtime {

enum class ByteOrder {
  kLittleEndian,
  kBigEndian,
};

enum class StringEncoding {
  kAscii,
  kUtf8,
};

enum class FieldKind {
  kUnsigned,
  kSigned,
  kFloat32,
  kFloat64,
  kBytes,
  kString,
  kStruct,
  kEnum,
};

using FieldId = uint16_t;
using BitFieldId = uint16_t;

struct EnumValueDefinition {
  uint64_t value = 0;
  std::string name;
};

inline constexpr size_t kBitsPerByte = 8U;
inline constexpr size_t kMaxFieldsPerMessage = 64;
inline constexpr size_t kMaxBitFieldsPerMessage = 64;

constexpr std::string_view to_string(ByteOrder byte_order) noexcept {
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      return "little_endian";
    case ByteOrder::kBigEndian:
      return "big_endian";
  }
  return "unknown";
}

constexpr std::string_view to_string(FieldKind kind) noexcept {
  switch (kind) {
    case FieldKind::kUnsigned:
      return "unsigned";
    case FieldKind::kSigned:
      return "signed";
    case FieldKind::kFloat32:
      return "float32";
    case FieldKind::kFloat64:
      return "float64";
    case FieldKind::kBytes:
      return "bytes";
    case FieldKind::kString:
      return "string";
    case FieldKind::kStruct:
      return "struct";
    case FieldKind::kEnum:
      return "enum";
  }
  return "unknown";
}

constexpr std::string_view to_string(StringEncoding encoding) noexcept {
  switch (encoding) {
    case StringEncoding::kAscii:
      return "ascii";
    case StringEncoding::kUtf8:
      return "utf8";
  }
  return "unknown";
}

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__TYPES_HPP_
