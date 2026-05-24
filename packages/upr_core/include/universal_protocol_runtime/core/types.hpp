#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__TYPES_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CORE_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CORE__TYPES_HPP_
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace universal_protocol_runtime {

/**
 * @brief Byte order for fixed-width scalar fields.
 */
enum class ByteOrder {
  kLittleEndian,
  kBigEndian,
};

/**
 * @brief Text encoding for schema string fields.
 */
enum class StringEncoding {
  kAscii,
  kUtf8,
};

/**
 * @brief Logical field categories supported by the schema model.
 */
enum class FieldKind {
  kUnsigned,
  kSigned,
  kFloat32,
  kFloat64,
  kBytes,
  kString,
  kStruct,
  kEnum,
  kCollection,
  kVariant,
};

/**
 * @brief Stable numeric identifier for a compiled field.
 */
using FieldId = uint16_t;
/**
 * @brief Stable numeric identifier for a compiled bit field.
 */
using BitFieldId = uint16_t;

/**
 * @brief Named numeric value for schema enums and enum-backed bit fields.
 */
struct EnumValueDefinition {
  uint64_t value = 0;
  std::string name;
};

inline constexpr size_t kBitsPerByte = 8U;
inline constexpr size_t kMaxFieldsPerMessage = 64;
inline constexpr size_t kMaxBitFieldsPerMessage = 64;

/**
 * @brief Converts a byte-order enum to a stable string name.
 * @param byte_order Byte order to stringify.
 * @return String representation of the byte order.
 */
constexpr std::string_view to_string(ByteOrder byte_order) noexcept {
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      return "little_endian";
    case ByteOrder::kBigEndian:
      return "big_endian";
  }
  return "unknown";
}

/**
 * @brief Converts a field kind to a stable string name.
 * @param kind Field kind to stringify.
 * @return String representation of the field kind.
 */
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
    case FieldKind::kCollection:
      return "collection";
    case FieldKind::kVariant:
      return "variant";
  }
  return "unknown";
}

/**
 * @brief Converts a string encoding to a stable string name.
 * @param encoding Encoding to stringify.
 * @return String representation of the encoding.
 */
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
