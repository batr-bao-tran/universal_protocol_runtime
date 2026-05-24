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
class DecodedMessage;

/**
 * @brief Borrowed view over a decoded collection field.
 */
class DecodedCollectionView {
 public:
  DecodedCollectionView() = default;
  /**
   * @brief Destroys the collection view.
   * @return No return value.
   */
  ~DecodedCollectionView() noexcept = default;

  /**
   * @brief Checks whether the view references a valid decoded collection.
   * @return `true` when the view is usable.
   */
  bool valid() const noexcept { return protocol_ != nullptr && element_layout_ != nullptr; }
  /**
   * @brief Returns the number of collection elements.
   * @return Element count.
   */
  size_t count() const noexcept { return count_; }
  /**
   * @brief Returns the raw bytes covering the collection payload.
   * @return Borrowed byte span for the collection payload.
   */
  ByteSpan raw() const noexcept { return frame_; }
  /**
   * @brief Decodes one collection element by index.
   * @param index Zero-based element index.
   * @return Decoded element view when the index is valid.
   */
  std::optional<DecodedMessage> at(size_t index) const;

 private:
  friend class DecodedMessage;

  const CompiledProtocol* protocol_ = nullptr;
  const CompiledMessage* element_layout_ = nullptr;
  ByteSpan frame_;
  size_t count_ = 0;
};

/**
 * @brief Borrowed decoded view over one compiled message instance.
 */
class DecodedMessage {
 public:
  /**
   * @brief Resolved byte range for one field inside the decoded frame.
   */
  struct ResolvedField {
    size_t offset = 0;
    size_t size = 0;
  };

  /**
   * @brief Destroys the decoded message view.
   * @return No return value.
   */
  ~DecodedMessage() noexcept = default;

  /**
   * @brief Checks whether the decoded view references a valid message.
   * @return `true` when the view is usable.
   */
  constexpr bool valid() const noexcept { return message_ != nullptr; }

  /**
   * @brief Returns the decoded message name.
   * @return Message name from the compiled schema.
   */
  std::string_view message_name() const;

  /**
   * @brief Returns the raw bytes for the decoded frame.
   * @return Borrowed byte span over the original frame.
   */
  constexpr ByteSpan raw() const noexcept { return frame_; }

  /**
   * @brief Returns the compiled protocol that produced this view.
   * @return Pointer to compiled protocol metadata.
   */
  constexpr const CompiledProtocol* protocol() const noexcept { return protocol_; }

  /**
   * @brief Returns the compiled message schema for this view.
   * @return Pointer to compiled message metadata.
   */
  constexpr const CompiledMessage* schema() const noexcept { return message_; }

  /**
   * @brief Resolves a field name to its field identifier.
   * @param name Field name to resolve.
   * @return Field identifier when the field exists.
   */
  std::optional<FieldId> field_id(std::string_view name) const;

  /**
   * @brief Resolves a bit-field name to its bit-field identifier.
   * @param name Bit-field name to resolve.
   * @return Bit-field identifier when the bit field exists.
   */
  std::optional<BitFieldId> bit_field_id(std::string_view name) const;

  /**
   * @brief Reports whether a field is present in the decoded frame.
   * @param field_id Field identifier to query.
   * @return `true` when the field is present and selected.
   */
  bool is_present(FieldId field_id) const;
  /**
   * @brief Reports whether a named field is present in the decoded frame.
   * @param name Field name to query.
   * @return `true` when the field is present and selected.
   */
  bool is_present(std::string_view name) const;

  /**
   * @brief Returns an unsigned scalar field by identifier.
   * @param field_id Field identifier to read.
   * @return Unsigned value when the field is present and compatible.
   */
  std::optional<uint64_t> get_unsigned(FieldId field_id) const;
  /**
   * @brief Returns a signed scalar field by identifier.
   * @param field_id Field identifier to read.
   * @return Signed value when the field is present and compatible.
   */
  std::optional<int64_t> get_signed(FieldId field_id) const;
  /**
   * @brief Returns a `float32` field by identifier.
   * @param field_id Field identifier to read.
   * @return Floating-point value when the field is present and compatible.
   */
  std::optional<float> get_float32(FieldId field_id) const;
  /**
   * @brief Returns a `float64` field by identifier.
   * @param field_id Field identifier to read.
   * @return Floating-point value when the field is present and compatible.
   */
  std::optional<double> get_float64(FieldId field_id) const;
  /**
   * @brief Returns a bytes field by identifier.
   * @param field_id Field identifier to read.
   * @return Borrowed byte span when the field is present and compatible.
   */
  std::optional<ByteSpan> get_bytes(FieldId field_id) const;
  /**
   * @brief Returns a string field by identifier.
   * @param field_id Field identifier to read.
   * @return Borrowed string view when the field is present and compatible.
   */
  std::optional<std::string_view> get_string_view(FieldId field_id) const;
  /**
   * @brief Returns the symbolic enum name for an enum field by identifier.
   * @param field_id Field identifier to read.
   * @return Enum symbol when the field is present and compatible.
   */
  std::optional<std::string_view> get_enum_name(FieldId field_id) const;
  /**
   * @brief Returns a nested struct field by identifier.
   * @param field_id Field identifier to read.
   * @return Decoded struct view when the field is present and compatible.
   */
  std::optional<DecodedMessage> get_struct(FieldId field_id) const;
  /**
   * @brief Returns a collection field by identifier.
   * @param field_id Field identifier to read.
   * @return Collection view when the field is present and compatible.
   */
  std::optional<DecodedCollectionView> get_collection(FieldId field_id) const;
  /**
   * @brief Returns a resolved variant field by identifier.
   * @param field_id Field identifier to read.
   * @return Decoded variant case view when the field is present and compatible.
   */
  std::optional<DecodedMessage> get_variant(FieldId field_id) const;

  /**
   * @brief Returns an unsigned bit-field value by identifier.
   * @param bit_field_id Bit-field identifier to read.
   * @return Unsigned value when the bit field is present and compatible.
   */
  std::optional<uint64_t> get_bit_unsigned(BitFieldId bit_field_id) const;
  /**
   * @brief Returns a signed bit-field value by identifier.
   * @param bit_field_id Bit-field identifier to read.
   * @return Signed value when the bit field is present and compatible.
   */
  std::optional<int64_t> get_bit_signed(BitFieldId bit_field_id) const;
  /**
   * @brief Returns the symbolic enum name for a bit-field by identifier.
   * @param bit_field_id Bit-field identifier to read.
   * @return Enum symbol when the bit field is present and compatible.
   */
  std::optional<std::string_view> get_bit_enum_name(BitFieldId bit_field_id) const;

  /**
   * @brief Returns an unsigned scalar field by name.
   * @param name Field name to read.
   * @return Unsigned value when the field is present and compatible.
   */
  std::optional<uint64_t> get_unsigned(std::string_view name) const;
  /**
   * @brief Returns a signed scalar field by name.
   * @param name Field name to read.
   * @return Signed value when the field is present and compatible.
   */
  std::optional<int64_t> get_signed(std::string_view name) const;
  /**
   * @brief Returns a `float32` field by name.
   * @param name Field name to read.
   * @return Floating-point value when the field is present and compatible.
   */
  std::optional<float> get_float32(std::string_view name) const;
  /**
   * @brief Returns a `float64` field by name.
   * @param name Field name to read.
   * @return Floating-point value when the field is present and compatible.
   */
  std::optional<double> get_float64(std::string_view name) const;
  /**
   * @brief Returns a bytes field by name.
   * @param name Field name to read.
   * @return Borrowed byte span when the field is present and compatible.
   */
  std::optional<ByteSpan> get_bytes(std::string_view name) const;
  /**
   * @brief Returns a string field by name.
   * @param name Field name to read.
   * @return Borrowed string view when the field is present and compatible.
   */
  std::optional<std::string_view> get_string_view(std::string_view name) const;
  /**
   * @brief Returns the symbolic enum name for an enum field by name.
   * @param name Field name to read.
   * @return Enum symbol when the field is present and compatible.
   */
  std::optional<std::string_view> get_enum_name(std::string_view name) const;
  /**
   * @brief Returns a nested struct field by name.
   * @param name Field name to read.
   * @return Decoded struct view when the field is present and compatible.
   */
  std::optional<DecodedMessage> get_struct(std::string_view name) const;
  /**
   * @brief Returns a collection field by name.
   * @param name Field name to read.
   * @return Collection view when the field is present and compatible.
   */
  std::optional<DecodedCollectionView> get_collection(std::string_view name) const;
  /**
   * @brief Returns a resolved variant field by name.
   * @param name Field name to read.
   * @return Decoded variant case view when the field is present and compatible.
   */
  std::optional<DecodedMessage> get_variant(std::string_view name) const;

  /**
   * @brief Returns an unsigned bit-field value by name.
   * @param name Bit-field name to read.
   * @return Unsigned value when the bit field is present and compatible.
   */
  std::optional<uint64_t> get_bit_unsigned(std::string_view name) const;
  /**
   * @brief Returns a signed bit-field value by name.
   * @param name Bit-field name to read.
   * @return Signed value when the bit field is present and compatible.
   */
  std::optional<int64_t> get_bit_signed(std::string_view name) const;
  /**
   * @brief Returns the symbolic enum name for a bit-field by name.
   * @param name Bit-field name to read.
   * @return Enum symbol when the bit field is present and compatible.
   */
  std::optional<std::string_view> get_bit_enum_name(std::string_view name) const;

  /**
   * @brief Returns a fixed-size bytes field by identifier.
   * @tparam Extent Expected field width in bytes.
   * @param field_id Field identifier to read.
   * @return Borrowed fixed-extent byte span when the field matches.
   */
  template <size_t Extent>
  std::optional<std::span<const std::byte, Extent>> get_fixed_bytes(FieldId field_id) const {
    const auto bytes = get_bytes(field_id);
    if (!bytes.has_value() || bytes->size() != Extent) {
      return std::nullopt;
    }
    return std::span<const std::byte, Extent>(bytes->data(), Extent);
  }

  /**
   * @brief Returns a fixed-size bytes field by name.
   * @tparam Extent Expected field width in bytes.
   * @param name Field name to read.
   * @return Borrowed fixed-extent byte span when the field matches.
   */
  template <size_t Extent>
  std::optional<std::span<const std::byte, Extent>> get_fixed_bytes(std::string_view name) const {
    const auto resolved_field_id = field_id(name);
    if (!resolved_field_id.has_value()) {
      return std::nullopt;
    }
    return get_fixed_bytes<Extent>(*resolved_field_id);
  }

  /**
   * @brief Returns a fixed-size string field by identifier.
   * @tparam Extent Expected field width in bytes.
   * @param field_id Field identifier to read.
   * @return Borrowed fixed-extent character span when the field matches.
   */
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

  /**
   * @brief Returns a fixed-size string field by name.
   * @tparam Extent Expected field width in bytes.
   * @param name Field name to read.
   * @return Borrowed fixed-extent character span when the field matches.
   */
  template <size_t Extent>
  std::optional<std::span<const char, Extent>> get_fixed_string(std::string_view name) const {
    const auto resolved_field_id = field_id(name);
    if (!resolved_field_id.has_value()) {
      return std::nullopt;
    }
    return get_fixed_string<Extent>(*resolved_field_id);
  }

  /**
   * @brief Returns a scalar field converted to the requested native type.
   * @tparam T Requested native type.
   * @param field_id Field identifier to read.
   * @return Converted value when the field is present and in range.
   */
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

  /**
   * @brief Returns a scalar field by name converted to the requested native type.
   * @tparam T Requested native type.
   * @param name Field name to read.
   * @return Converted value when the field is present and in range.
   */
  template <typename T>
  std::optional<T> get(std::string_view name) const {
    const auto resolved_field_id = field_id(name);
    if (!resolved_field_id.has_value()) {
      return std::nullopt;
    }
    return get<T>(*resolved_field_id);
  }

  /**
   * @brief Returns a bit-field converted to the requested native type.
   * @tparam T Requested native type.
   * @param bit_field_id Bit-field identifier to read.
   * @return Converted value when the bit field is present and in range.
   */
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

  /**
   * @brief Returns a bit-field by name converted to the requested native type.
   * @tparam T Requested native type.
   * @param name Bit-field name to read.
   * @return Converted value when the bit field is present and in range.
   */
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
  friend class DecodedCollectionView;

  const CompiledField* field_definition(FieldId field_id) const;
  const CompiledBitField* bit_field_definition(BitFieldId bit_field_id) const;
  std::optional<ResolvedField> resolved_field(FieldId field_id) const;
  std::optional<uint64_t> cached_scalar_raw(FieldId field_id) const;
  std::optional<uint64_t> raw_unsigned(FieldId field_id) const;
  bool condition_matches(const CompiledField& field) const;
  bool presence_matches(const CompiledField& field) const;
  void eager_cache_scalar(FieldId field_id, ByteSpan field_bytes);
  bool field_selected(FieldId field_id) const;
  DecodeStatus assign_from_layout(const CompiledProtocol& protocol,
                                  const CompiledMessage& layout,
                                  ByteSpan frame,
                                  size_t* bytes_consumed,
                                  const std::array<bool, kMaxFieldsPerMessage>* selected_fields = nullptr,
                                  bool allow_prefix_trailing = false);

  const CompiledProtocol* protocol_ = nullptr;
  const CompiledMessage* message_ = nullptr;
  ByteSpan frame_;
  std::array<ResolvedField, kMaxFieldsPerMessage> resolved_fields_{};
  std::array<bool, kMaxFieldsPerMessage> field_present_{};
  mutable std::array<uint64_t, kMaxFieldsPerMessage> scalar_cache_values_{};
  mutable std::array<bool, kMaxFieldsPerMessage> scalar_cache_valid_{};
  std::array<bool, kMaxFieldsPerMessage> selected_fields_{};
  bool selection_active_ = false;
  size_t field_count_ = 0;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DECODER__DECODED_MESSAGE_HPP_
