#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODED_MESSAGE_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODED_MESSAGE_HPP_
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include "universal_protocol_runtime/compiler/compiled_protocol.hpp"
#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/decoder/decode_status.hpp"

namespace universal_protocol_runtime {

class ProtocolDecoder;

class DecodedMessage {
 public:
  struct ResolvedField {
    size_t offset = 0;
    size_t size = 0;
  };

  ~DecodedMessage() noexcept = default;

  bool valid() const { return message_ != nullptr; }

  std::string_view message_name() const;

  ByteSpan raw() const { return frame_; }

  const CompiledProtocol* protocol() const { return protocol_; }

  const CompiledMessage* schema() const { return message_; }

  std::optional<FieldId> field_id(std::string_view name) const;

  std::optional<BitFieldId> bit_field_id(std::string_view name) const;

  std::optional<uint64_t> get_unsigned(FieldId field_id) const;
  std::optional<int64_t> get_signed(FieldId field_id) const;
  std::optional<float> get_float32(FieldId field_id) const;
  std::optional<double> get_float64(FieldId field_id) const;
  std::optional<ByteSpan> get_bytes(FieldId field_id) const;
  std::optional<std::string_view> get_string_view(FieldId field_id) const;
  std::optional<std::string_view> get_enum_name(FieldId field_id) const;
  std::optional<DecodedMessage> get_struct(FieldId field_id) const;

  std::optional<uint64_t> get_bit_unsigned(BitFieldId bit_field_id) const;
  std::optional<int64_t> get_bit_signed(BitFieldId bit_field_id) const;
  std::optional<std::string_view> get_bit_enum_name(BitFieldId bit_field_id) const;

  std::optional<uint64_t> get_unsigned(std::string_view name) const;
  std::optional<int64_t> get_signed(std::string_view name) const;
  std::optional<float> get_float32(std::string_view name) const;
  std::optional<double> get_float64(std::string_view name) const;
  std::optional<ByteSpan> get_bytes(std::string_view name) const;
  std::optional<std::string_view> get_string_view(std::string_view name) const;
  std::optional<std::string_view> get_enum_name(std::string_view name) const;
  std::optional<DecodedMessage> get_struct(std::string_view name) const;

  std::optional<uint64_t> get_bit_unsigned(std::string_view name) const;
  std::optional<int64_t> get_bit_signed(std::string_view name) const;
  std::optional<std::string_view> get_bit_enum_name(std::string_view name) const;

  template <size_t Extent>
  std::optional<std::span<const std::byte, Extent>> get_fixed_bytes(FieldId field_id) const {
    const auto bytes = get_bytes(field_id);
    if (!bytes.has_value() || bytes->size() != Extent) {
      return std::nullopt;
    }
    return std::span<const std::byte, Extent>(bytes->data(), Extent);
  }

  template <size_t Extent>
  std::optional<std::span<const std::byte, Extent>> get_fixed_bytes(std::string_view name) const {
    const auto resolved_field_id = field_id(name);
    if (!resolved_field_id.has_value()) {
      return std::nullopt;
    }
    return get_fixed_bytes<Extent>(*resolved_field_id);
  }

  template <size_t Extent>
  std::optional<std::span<const char, Extent>> get_fixed_string(FieldId field_id) const {
    const CompiledField* field = field_definition(field_id);
    if (field == nullptr || field->kind != FieldKind::kString) {
      return std::nullopt;
    }
    const auto bytes = get_fixed_bytes<Extent>(field_id);
    if (!bytes.has_value()) {
      return std::nullopt;
    }
    return std::span<const char, Extent>(reinterpret_cast<const char*>(bytes->data()), Extent);
  }

  template <size_t Extent>
  std::optional<std::span<const char, Extent>> get_fixed_string(std::string_view name) const {
    const auto resolved_field_id = field_id(name);
    if (!resolved_field_id.has_value()) {
      return std::nullopt;
    }
    return get_fixed_string<Extent>(*resolved_field_id);
  }

  template <typename T>
  std::optional<T> get(FieldId field_id) const {
    if constexpr (std::is_unsigned_v<T> && std::is_integral_v<T>) {
      const auto value = get_unsigned(field_id);
      if (!value.has_value() || *value > std::numeric_limits<T>::max()) {
        return std::nullopt;
      }
      return static_cast<T>(*value);
    } else if constexpr (std::is_signed_v<T> && std::is_integral_v<T>) {
      const auto value = get_signed(field_id);
      if (!value.has_value() || *value < std::numeric_limits<T>::min() || *value > std::numeric_limits<T>::max()) {
        return std::nullopt;
      }
      return static_cast<T>(*value);
    } else if constexpr (std::is_same_v<T, float>) {
      return get_float32(field_id);
    } else if constexpr (std::is_same_v<T, double>) {
      return get_float64(field_id);
    } else {
      return std::nullopt;
    }
  }

  template <typename T>
  std::optional<T> get(std::string_view name) const {
    const auto resolved_field_id = field_id(name);
    if (!resolved_field_id.has_value()) {
      return std::nullopt;
    }
    return get<T>(*resolved_field_id);
  }

  template <typename T>
  std::optional<T> get_bit(BitFieldId bit_field_id) const {
    if constexpr (std::is_unsigned_v<T> && std::is_integral_v<T>) {
      const auto value = get_bit_unsigned(bit_field_id);
      if (!value.has_value() || *value > std::numeric_limits<T>::max()) {
        return std::nullopt;
      }
      return static_cast<T>(*value);
    } else if constexpr (std::is_signed_v<T> && std::is_integral_v<T>) {
      const auto value = get_bit_signed(bit_field_id);
      if (!value.has_value() || *value < std::numeric_limits<T>::min() || *value > std::numeric_limits<T>::max()) {
        return std::nullopt;
      }
      return static_cast<T>(*value);
    } else {
      return std::nullopt;
    }
  }

  template <typename T>
  std::optional<T> get_bit(std::string_view name) const {
    const auto resolved_bit_field_id = bit_field_id(name);
    if (!resolved_bit_field_id.has_value()) {
      return std::nullopt;
    }
    return get_bit<T>(*resolved_bit_field_id);
  }

 private:
  friend class ProtocolDecoder;

  const CompiledField* field_definition(FieldId field_id) const;
  const CompiledBitField* bit_field_definition(BitFieldId bit_field_id) const;
  std::optional<ResolvedField> resolved_field(FieldId field_id) const;
  std::optional<uint64_t> cached_container_unsigned(FieldId field_id) const;
  DecodeStatus assign_from_layout(const CompiledProtocol& protocol,
                                  const CompiledMessage& layout,
                                  ByteSpan frame,
                                  size_t* bytes_consumed);

  const CompiledProtocol* protocol_ = nullptr;
  const CompiledMessage* message_ = nullptr;
  ByteSpan frame_;
  std::array<ResolvedField, kMaxFieldsPerMessage> resolved_fields_{};
  mutable std::array<uint64_t, kMaxFieldsPerMessage> scalar_cache_values_{};
  mutable std::array<bool, kMaxFieldsPerMessage> scalar_cache_valid_{};
  size_t field_count_ = 0;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODED_MESSAGE_HPP_
