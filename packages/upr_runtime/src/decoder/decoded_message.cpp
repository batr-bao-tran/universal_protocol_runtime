#include "universal_protocol_runtime/decoder/decoded_message.hpp"

#include <algorithm>
#include <bit>

#include "universal_protocol_runtime/core/compiler_hints.hpp"
#include "universal_protocol_runtime/decoder/direct_decode_support.hpp"

namespace universal_protocol_runtime {
namespace {

bool supports_eager_scalar_cache(const CompiledField& field) {
  if (!field.is_scalar()) {
    return false;
  }
  switch (field.kind) {
    case FieldKind::kUnsigned:
    case FieldKind::kSigned:
    case FieldKind::kEnum:
      return field.width_bytes > 0 && field.width_bytes <= direct_decode_support::kMaxScalarWidthBytes;
    case FieldKind::kFloat32:
      return field.width_bytes == sizeof(uint32_t);
    case FieldKind::kFloat64:
      return field.width_bytes == sizeof(uint64_t);
  }
  return false;  // LCOV_EXCL_LINE
}

std::optional<size_t> checksum_anchor_offset(
    const CompiledChecksumAnchor& anchor,
    ByteSpan frame,
    const std::array<DecodedMessage::ResolvedField, kMaxFieldsPerMessage>& resolved_fields,
    size_t field_count) {
  switch (anchor.kind) {
    case ChecksumAnchorKind::kFrameStart:
      return 0U;
    case ChecksumAnchorKind::kFrameEnd:
      return frame.size();
    case ChecksumAnchorKind::kFieldStart:
    case ChecksumAnchorKind::kBeforeSelf:
      if (anchor.field_id >= field_count) {
        return std::nullopt;
      }
      return resolved_fields[anchor.field_id].offset;
    case ChecksumAnchorKind::kFieldEnd:
    case ChecksumAnchorKind::kAfterSelf:
      if (anchor.field_id >= field_count) {
        return std::nullopt;
      }
      return resolved_fields[anchor.field_id].offset + resolved_fields[anchor.field_id].size;
  }
  return std::nullopt;  // LCOV_EXCL_LINE
}

}  // namespace

std::string_view DecodedMessage::message_name() const {
  if (message_ == nullptr) {
    return {};
  }
  return message_->name();
}

std::optional<FieldId> DecodedMessage::field_id(std::string_view name) const {
  if (message_ == nullptr) {
    return std::nullopt;
  }
  return message_->find_field(name);
}

std::optional<BitFieldId> DecodedMessage::bit_field_id(std::string_view name) const {
  if (message_ == nullptr) {
    return std::nullopt;
  }
  return message_->find_bit_field(name);
}

const CompiledField* DecodedMessage::field_definition(FieldId field_id) const {
  if (message_ == nullptr || field_id >= message_->fields().size()) {
    return nullptr;
  }
  return &message_->fields()[field_id];
}

const CompiledBitField* DecodedMessage::bit_field_definition(BitFieldId bit_field_id) const {
  if (message_ == nullptr || bit_field_id >= message_->bit_fields().size()) {
    return nullptr;
  }
  return &message_->bit_fields()[bit_field_id];
}

std::optional<DecodedMessage::ResolvedField> DecodedMessage::resolved_field(FieldId field_id) const {
  if (field_id >= field_count_) {
    return std::nullopt;
  }
  return resolved_fields_[field_id];
}

std::optional<uint64_t> DecodedMessage::cached_scalar_raw(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (UPR_UNLIKELY(field == nullptr || !field->is_scalar())) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }
  if (scalar_cache_valid_[field_id]) {
    return scalar_cache_values_[field_id];
  }
  return std::nullopt;
}

void DecodedMessage::eager_cache_scalar(FieldId field_id, ByteSpan field_bytes) {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || !supports_eager_scalar_cache(*field) || field_bytes.size() != field->width_bytes) {
    return;
  }
  const auto value = direct_decode_support::read_unsigned_scalar(field_bytes, field->byte_order);
  if (UPR_UNLIKELY(!value.has_value())) {
    return;  // LCOV_EXCL_LINE
  }
  scalar_cache_values_[field_id] = *value;
  scalar_cache_valid_[field_id] = true;
}

std::optional<uint64_t> DecodedMessage::get_unsigned(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || (field->kind != FieldKind::kUnsigned && field->kind != FieldKind::kEnum)) {
    return std::nullopt;
  }
  return cached_scalar_raw(field_id);
}

std::optional<int64_t> DecodedMessage::get_signed(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kSigned) {
    return std::nullopt;
  }
  const auto raw = cached_scalar_raw(field_id);
  if (!raw.has_value()) {
    return std::nullopt;
  }
  return direct_decode_support::sign_extend(*raw, static_cast<uint8_t>(field->width_bytes * kBitsPerByte));
}

std::optional<float> DecodedMessage::get_float32(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kFloat32 || field->width_bytes != sizeof(uint32_t)) {
    return std::nullopt;
  }
  const auto value = cached_scalar_raw(field_id);
  if (UPR_UNLIKELY(!value.has_value())) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }
  return std::bit_cast<float>(static_cast<uint32_t>(*value));
}

std::optional<double> DecodedMessage::get_float64(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kFloat64 || field->width_bytes != sizeof(uint64_t)) {
    return std::nullopt;
  }
  const auto value = cached_scalar_raw(field_id);
  if (UPR_UNLIKELY(!value.has_value())) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }
  return std::bit_cast<double>(*value);
}

std::optional<ByteSpan> DecodedMessage::get_bytes(FieldId field_id) const {
  const auto resolved = resolved_field(field_id);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  return frame_.subspan(resolved->offset, resolved->size);
}

std::optional<std::string_view> DecodedMessage::get_string_view(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (UPR_UNLIKELY(field == nullptr || field->kind != FieldKind::kString)) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }
  const auto resolved = resolved_field(field_id);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  return std::string_view(reinterpret_cast<const char*>(frame_.data() + resolved->offset), resolved->size);
}

std::optional<std::string_view> DecodedMessage::get_enum_name(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kEnum) {
    return std::nullopt;
  }
  const auto value = get_unsigned(field_id);
  if (!value.has_value()) {
    return std::nullopt;
  }
  const auto it = std::find_if(field->enum_values.begin(),
                               field->enum_values.end(),
                               [value](const EnumValueDefinition& enum_value) { return enum_value.value == *value; });
  return it == field->enum_values.end() ? std::nullopt : std::optional<std::string_view>(it->name);
}

std::optional<DecodedMessage> DecodedMessage::get_struct(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kStruct || protocol_ == nullptr) {
    return std::nullopt;
  }
  const auto resolved = resolved_field(field_id);
  if (UPR_UNLIKELY(!resolved.has_value())) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }
  const CompiledMessage* nested_layout = protocol_->struct_by_id(field->struct_id);
  if (UPR_UNLIKELY(nested_layout == nullptr)) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }

  DecodedMessage nested;
  size_t bytes_consumed = 0;
  if (UPR_UNLIKELY(nested.assign_from_layout(
                       *protocol_, *nested_layout, frame_.subspan(resolved->offset, resolved->size), &bytes_consumed) !=
                   DecodeStatus::kOk)) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }
  return nested;
}

std::optional<uint64_t> DecodedMessage::get_bit_unsigned(BitFieldId bit_field_id) const {
  const CompiledBitField* bit_field = bit_field_definition(bit_field_id);
  if (bit_field == nullptr) {
    return std::nullopt;
  }
  const CompiledField* container_field = field_definition(bit_field->container_field_id);
  if (UPR_UNLIKELY(container_field == nullptr ||
                   (container_field->kind != FieldKind::kUnsigned && container_field->kind != FieldKind::kSigned &&
                    container_field->kind != FieldKind::kEnum))) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }
  const auto container_value = cached_scalar_raw(bit_field->container_field_id);
  if (!container_value.has_value()) {
    return std::nullopt;
  }
  return (*container_value >> bit_field->shift_bits) & bit_field->mask;
}

std::optional<int64_t> DecodedMessage::get_bit_signed(BitFieldId bit_field_id) const {
  const CompiledBitField* bit_field = bit_field_definition(bit_field_id);
  if (bit_field == nullptr || !bit_field->is_signed) {
    return std::nullopt;
  }
  const auto value = get_bit_unsigned(bit_field_id);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return direct_decode_support::sign_extend(*value, bit_field->width_bits);
}

std::optional<std::string_view> DecodedMessage::get_bit_enum_name(BitFieldId bit_field_id) const {
  const CompiledBitField* bit_field = bit_field_definition(bit_field_id);
  if (bit_field == nullptr) {
    return std::nullopt;
  }
  const auto value = get_bit_unsigned(bit_field_id);
  if (!value.has_value()) {
    return std::nullopt;
  }
  const auto it = std::find_if(bit_field->enum_values.begin(),
                               bit_field->enum_values.end(),
                               [value](const EnumValueDefinition& enum_value) { return enum_value.value == *value; });
  return it == bit_field->enum_values.end() ? std::nullopt : std::optional<std::string_view>(it->name);
}

std::optional<uint64_t> DecodedMessage::get_unsigned(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() ? get_unsigned(*resolved) : std::nullopt;
}

std::optional<int64_t> DecodedMessage::get_signed(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() ? get_signed(*resolved) : std::nullopt;
}

std::optional<float> DecodedMessage::get_float32(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() ? get_float32(*resolved) : std::nullopt;
}

std::optional<double> DecodedMessage::get_float64(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() ? get_float64(*resolved) : std::nullopt;
}

std::optional<ByteSpan> DecodedMessage::get_bytes(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() ? get_bytes(*resolved) : std::nullopt;
}

std::optional<std::string_view> DecodedMessage::get_string_view(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() ? get_string_view(*resolved) : std::nullopt;
}

std::optional<std::string_view> DecodedMessage::get_enum_name(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() ? get_enum_name(*resolved) : std::nullopt;
}

std::optional<DecodedMessage> DecodedMessage::get_struct(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() ? get_struct(*resolved) : std::nullopt;
}

std::optional<uint64_t> DecodedMessage::get_bit_unsigned(std::string_view name) const {
  const auto resolved = bit_field_id(name);
  return resolved.has_value() ? get_bit_unsigned(*resolved) : std::nullopt;
}

std::optional<int64_t> DecodedMessage::get_bit_signed(std::string_view name) const {
  const auto resolved = bit_field_id(name);
  return resolved.has_value() ? get_bit_signed(*resolved) : std::nullopt;
}

std::optional<std::string_view> DecodedMessage::get_bit_enum_name(std::string_view name) const {
  const auto resolved = bit_field_id(name);
  return resolved.has_value() ? get_bit_enum_name(*resolved) : std::nullopt;
}

// Recursive descent is required for nested struct layouts.
// NOLINTNEXTLINE(misc-no-recursion)
DecodeStatus DecodedMessage::assign_from_layout(const CompiledProtocol& protocol,
                                                const CompiledMessage& layout,
                                                ByteSpan frame,
                                                size_t* bytes_consumed) {
  if (UPR_UNLIKELY(layout.fields().size() > kMaxFieldsPerMessage)) {
    return DecodeStatus::kFieldLimitExceeded;
  }
  if (UPR_UNLIKELY(frame.size() < layout.minimum_size())) {
    return DecodeStatus::kSchemaMismatch;
  }

  DecodedMessage candidate;
  candidate.protocol_ = &protocol;
  candidate.message_ = &layout;
  candidate.frame_ = frame;
  candidate.field_count_ = layout.fields().size();

  size_t offset = 0;
  for (const CompiledField& field : layout.fields()) {
    size_t field_size = field.minimum_size_contribution();
    if (field.kind == FieldKind::kBytes || field.kind == FieldKind::kString) {
      if (field.dynamic_size) {
        const auto dependency_value = candidate.get_unsigned(field.size_from_field);
        if (UPR_UNLIKELY(!dependency_value.has_value() || *dependency_value > frame.size())) {
          return DecodeStatus::kInvalidData;
        }
        field_size = static_cast<size_t>(*dependency_value);
      }
    } else if (field.kind == FieldKind::kStruct) {
      const CompiledMessage* nested_layout = protocol.struct_by_id(field.struct_id);
      if (nested_layout == nullptr) {
        return DecodeStatus::kInvalidData;
      }
      size_t nested_size = 0;
      DecodedMessage nested;
      const DecodeStatus nested_status =
          nested.assign_from_layout(protocol, *nested_layout, frame.subspan(offset), &nested_size);
      if (nested_status != DecodeStatus::kOk) {
        return nested_status;
      }
      field_size = nested_size;
    }

    if (UPR_UNLIKELY(offset + field_size > frame.size())) {
      return DecodeStatus::kSchemaMismatch;
    }
    candidate.resolved_fields_[field.id] = {.offset = offset, .size = field_size};
    const ByteSpan field_bytes = frame.subspan(offset, field_size);
    candidate.eager_cache_scalar(field.id, field_bytes);

    if (field.has_expected_unsigned) {
      const auto actual = candidate.cached_scalar_raw(field.id);
      if (UPR_UNLIKELY(!actual.has_value() || *actual != field.expected_unsigned)) {
        return DecodeStatus::kSchemaMismatch;
      }
    }

    if (field.kind == FieldKind::kString) {
      const bool valid_encoding =
          field.string_encoding == StringEncoding::kAscii
              ? direct_decode_support::runtime_validate_string<StringEncoding::kAscii>(field_bytes)
              : direct_decode_support::runtime_validate_string<StringEncoding::kUtf8>(field_bytes);
      if (UPR_UNLIKELY(!valid_encoding)) {
        return DecodeStatus::kInvalidData;
      }
    }

    offset += field_size;
  }

  if (UPR_UNLIKELY(!layout.allow_trailing_bytes() && offset != frame.size())) {
    return DecodeStatus::kSchemaMismatch;
  }

  for (const CompiledChecksum& checksum : layout.checksums()) {
    if (checksum.function == nullptr) {
      return DecodeStatus::kInvalidData;
    }
    const auto from_offset =
        checksum_anchor_offset(checksum.from, frame, candidate.resolved_fields_, candidate.field_count_);
    const auto to_offset =
        checksum_anchor_offset(checksum.to, frame, candidate.resolved_fields_, candidate.field_count_);
    if (UPR_UNLIKELY(!from_offset.has_value() || !to_offset.has_value() || *from_offset > *to_offset ||
                     *to_offset > frame.size())) {
      return DecodeStatus::kSchemaMismatch;
    }
    const ByteSpan checksum_bytes = frame.subspan(*from_offset, *to_offset - *from_offset);
    uint64_t actual = 0;
    switch (checksum.builtin_kind) {
      case CompiledChecksum::BuiltinKind::kXor8:
        actual = direct_decode_support::runtime_checksum_xor8(checksum_bytes);
        break;
      case CompiledChecksum::BuiltinKind::kSum16:
        actual = direct_decode_support::runtime_checksum_sum16(checksum_bytes);
        break;
      case CompiledChecksum::BuiltinKind::kCrc16Ccitt:
        actual = direct_decode_support::runtime_checksum_crc16_ccitt(checksum_bytes);
        break;
      case CompiledChecksum::BuiltinKind::kCrc32:
        actual = direct_decode_support::runtime_checksum_crc32(checksum_bytes);
        break;
      case CompiledChecksum::BuiltinKind::kCrc32c:
        actual = direct_decode_support::runtime_checksum_crc32c(checksum_bytes);
        break;
      case CompiledChecksum::BuiltinKind::kCustom:
        actual = checksum.function(checksum_bytes);
        break;
    }
    const auto expected = candidate.get_unsigned(checksum.field_id);
    if (UPR_UNLIKELY(!expected.has_value() || *expected != actual)) {
      return DecodeStatus::kChecksumMismatch;
    }
  }

  if (!layout.allow_trailing_bytes()) {
    candidate.frame_ = frame.first(offset);
  }

  if (bytes_consumed != nullptr) {
    *bytes_consumed = offset;
  }
  *this = candidate;
  return DecodeStatus::kOk;
}

}  // namespace universal_protocol_runtime
