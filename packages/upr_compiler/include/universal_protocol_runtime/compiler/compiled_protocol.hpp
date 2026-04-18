#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_COMPILER_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_COMPILER__COMPILED_PROTOCOL_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_COMPILER_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_COMPILER__COMPILED_PROTOCOL_HPP_
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "universal_protocol_runtime/compiler/checksum_registry.hpp"
#include "universal_protocol_runtime/core/types.hpp"

namespace universal_protocol_runtime {

struct TransparentStringHash {
  using is_transparent = void;

  size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }

  size_t operator()(const std::string& value) const noexcept { return operator()(std::string_view(value)); }

  size_t operator()(const char* value) const noexcept { return operator()(std::string_view(value)); }
};

enum class ChecksumAnchorKind {
  kFrameStart,
  kFrameEnd,
  kFieldStart,
  kFieldEnd,
  kBeforeSelf,
  kAfterSelf,
};

struct CompiledChecksumAnchor {
  ChecksumAnchorKind kind = ChecksumAnchorKind::kFrameStart;
  FieldId field_id = 0;
};

struct CompiledBitField {
  BitFieldId id = 0;
  std::string name;
  FieldId container_field_id = 0;
  uint8_t shift_bits = 0;
  uint8_t width_bits = 0;
  uint64_t mask = 0;
  bool is_signed = false;
  std::vector<EnumValueDefinition> enum_values;
};

struct CompiledChecksum {
  enum class BuiltinKind {
    kCustom,
    kXor8,
    kSum16,
    kCrc16Ccitt,
    kCrc32,
    kCrc32c,
  };

  FieldId field_id = 0;
  uint8_t result_width_bytes = 0;
  ChecksumFunction function = nullptr;
  std::string algorithm_name;
  BuiltinKind builtin_kind = BuiltinKind::kCustom;
  CompiledChecksumAnchor from;
  CompiledChecksumAnchor to;
};

struct CompiledField {
  FieldId id = 0;
  std::string name;
  FieldKind kind = FieldKind::kUnsigned;
  uint8_t width_bytes = 0;
  ByteOrder byte_order = ByteOrder::kLittleEndian;
  StringEncoding string_encoding = StringEncoding::kAscii;
  size_t fixed_size = 0;
  bool dynamic_size = false;
  FieldId size_from_field = 0;
  size_t struct_id = 0;
  bool has_expected_unsigned = false;
  uint64_t expected_unsigned = 0;
  std::vector<EnumValueDefinition> enum_values;

  constexpr bool is_scalar() const noexcept {
    return kind == FieldKind::kUnsigned || kind == FieldKind::kSigned || kind == FieldKind::kFloat32 ||
           kind == FieldKind::kFloat64 || kind == FieldKind::kEnum;
  }

  constexpr size_t minimum_size_contribution() const noexcept {
    if (dynamic_size) {
      return 0;
    }
    if (kind == FieldKind::kBytes || kind == FieldKind::kString || kind == FieldKind::kStruct) {
      return fixed_size;
    }
    return width_bytes;
  }
};

class CompiledMessage {
 public:
  CompiledMessage() = default;
  ~CompiledMessage() noexcept = default;

  CompiledMessage(std::string name,
                  std::vector<CompiledField> fields,
                  std::vector<CompiledBitField> bit_fields,
                  std::vector<CompiledChecksum> checksums,
                  size_t minimum_size,
                  bool allow_trailing_bytes,
                  std::vector<std::byte> dispatch_prefix = {});

  std::string_view name() const { return name_; }

  const std::vector<CompiledField>& fields() const { return fields_; }

  size_t minimum_size() const { return minimum_size_; }

  bool allow_trailing_bytes() const { return allow_trailing_bytes_; }

  std::span<const std::byte> dispatch_prefix() const { return dispatch_prefix_; }

  const std::vector<CompiledBitField>& bit_fields() const { return bit_fields_; }

  const std::vector<CompiledChecksum>& checksums() const { return checksums_; }

  std::optional<FieldId> find_field(std::string_view field_name) const;

  std::optional<BitFieldId> find_bit_field(std::string_view bit_field_name) const;

 private:
  std::string name_;
  std::vector<CompiledField> fields_;
  std::vector<CompiledBitField> bit_fields_;
  std::vector<CompiledChecksum> checksums_;
  size_t minimum_size_ = 0;
  bool allow_trailing_bytes_ = false;
  std::vector<std::byte> dispatch_prefix_;
  std::unordered_map<std::string, FieldId, TransparentStringHash, std::equal_to<>> field_ids_;
  std::unordered_map<std::string, BitFieldId, TransparentStringHash, std::equal_to<>> bit_field_ids_;
};

class CompiledProtocol {
 public:
  CompiledProtocol() = default;
  ~CompiledProtocol() noexcept = default;

  CompiledProtocol(std::string name,
                   uint64_t fingerprint,
                   std::vector<CompiledMessage> structs,
                   std::vector<CompiledMessage> messages);

  std::string_view name() const { return name_; }

  uint64_t fingerprint() const { return fingerprint_; }

  const std::vector<CompiledMessage>& structs() const { return structs_; }

  const std::vector<CompiledMessage>& messages() const { return messages_; }

  const CompiledMessage* find_message(std::string_view message_name) const;

  const CompiledMessage* find_struct(std::string_view struct_name) const;

  const CompiledMessage* struct_by_id(size_t struct_id) const;

  std::span<const size_t> dispatch_candidate_ids(ByteSpan frame) const noexcept;

  std::span<const size_t> fallback_candidate_ids() const noexcept { return fallback_message_ids_; }

 private:
  static constexpr size_t kDispatchTableSize = 256U;

  std::string name_;
  uint64_t fingerprint_ = 0;
  std::vector<CompiledMessage> structs_;
  std::vector<CompiledMessage> messages_;
  std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> struct_ids_;
  std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> message_ids_;
  std::array<std::vector<size_t>, kDispatchTableSize> dispatch_message_ids_;
  std::vector<size_t> fallback_message_ids_;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_COMPILER__COMPILED_PROTOCOL_HPP_
