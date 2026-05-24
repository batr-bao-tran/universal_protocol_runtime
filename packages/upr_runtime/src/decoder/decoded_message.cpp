#include "universal_protocol_runtime/decoder/decoded_message.hpp"

#include <algorithm>
#include <bit>
#include <limits>

#include "universal_protocol_runtime/core/compiler_hints.hpp"
#include "universal_protocol_runtime/decoder/direct_decode_support.hpp"

namespace universal_protocol_runtime {
namespace {

constexpr bool supports_eager_scalar_cache(const CompiledField& field) noexcept {
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
    case FieldKind::kBytes:
    case FieldKind::kString:
    case FieldKind::kStruct:
    case FieldKind::kCollection:
    case FieldKind::kVariant:
      return false;
  }
  return false;  // LCOV_EXCL_LINE
}

constexpr size_t align_up(size_t value, size_t alignment) noexcept {
  if (alignment <= 1U) {
    return value;
  }
  const size_t remainder = value % alignment;
  return remainder == 0U ? value : (value + (alignment - remainder));
}

constexpr bool validation_operator_matches(CompiledValidationOperator op, uint64_t lhs, uint64_t rhs) noexcept {
  switch (op) {
    case CompiledValidationOperator::kEq:
      return lhs == rhs;
    case CompiledValidationOperator::kNe:
      return lhs != rhs;
    case CompiledValidationOperator::kLt:
      return lhs < rhs;
    case CompiledValidationOperator::kLe:
      return lhs <= rhs;
    case CompiledValidationOperator::kGt:
      return lhs > rhs;
    case CompiledValidationOperator::kGe:
      return lhs >= rhs;
  }
  return false;  // LCOV_EXCL_LINE
}

constexpr bool checked_add_size(size_t lhs, size_t rhs, size_t* result) noexcept {
  if (lhs > (std::numeric_limits<size_t>::max() - rhs)) {
    return false;
  }
  *result = lhs + rhs;
  return true;
}

constexpr bool checked_mul_size(size_t lhs, size_t rhs, size_t* result) noexcept {
  if (lhs == 0U || rhs == 0U) {
    *result = 0U;
    return true;
  }
  if (lhs > (std::numeric_limits<size_t>::max() / rhs)) {
    return false;
  }
  *result = lhs * rhs;
  return true;
}

std::optional<size_t> checksum_anchor_offset(
    const CompiledChecksumAnchor& anchor,
    ByteSpan frame,
    const std::array<DecodedMessage::ResolvedField, kMaxFieldsPerMessage>& resolved_fields,
    const std::array<bool, kMaxFieldsPerMessage>& field_present,
    size_t field_count) {
  switch (anchor.kind) {
    case ChecksumAnchorKind::kFrameStart:
      return 0U;
    case ChecksumAnchorKind::kFrameEnd:
      return frame.size();
    case ChecksumAnchorKind::kFieldStart:
    case ChecksumAnchorKind::kBeforeSelf:
      if (anchor.field_id >= field_count || !field_present[anchor.field_id]) {
        return std::nullopt;
      }
      return resolved_fields[anchor.field_id].offset;
    case ChecksumAnchorKind::kFieldEnd:
    case ChecksumAnchorKind::kAfterSelf:
      if (anchor.field_id >= field_count || !field_present[anchor.field_id]) {
        return std::nullopt;
      }
      return resolved_fields[anchor.field_id].offset + resolved_fields[anchor.field_id].size;
  }
  return std::nullopt;  // LCOV_EXCL_LINE
}

const CompiledVariantCase* find_variant_case(const CompiledField& field, uint64_t tag_value) {
  if (!field.variant_lookup_indices.empty()) {
    if (tag_value < field.variant_lookup_base) {
      return nullptr;
    }
    const uint64_t dense_index = tag_value - field.variant_lookup_base;
    if (dense_index < field.variant_lookup_indices.size()) {
      const uint32_t case_index = field.variant_lookup_indices[static_cast<size_t>(dense_index)];
      if (case_index != CompiledField::kVariantLookupMissing) {
        return &field.variant_cases[case_index];
      }
      return nullptr;
    }
  }

  const auto it =
      std::lower_bound(field.variant_cases.begin(),
                       field.variant_cases.end(),
                       tag_value,
                       [](const CompiledVariantCase& candidate, uint64_t tag) { return candidate.tag_value < tag; });
  if (it == field.variant_cases.end() || it->tag_value != tag_value) {
    return nullptr;
  }
  return &*it;
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

bool DecodedMessage::is_present(FieldId field_id) const {
  return field_id < field_count_ && field_present_[field_id] && field_selected(field_id);
}

bool DecodedMessage::is_present(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() && is_present(*resolved);
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
  if (field_id >= field_count_ || !field_present_[field_id] || !field_selected(field_id)) {
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

bool DecodedMessage::field_selected(FieldId field_id) const { return !selection_active_ || selected_fields_[field_id]; }

std::optional<uint64_t> DecodedMessage::raw_unsigned(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || (field->kind != FieldKind::kUnsigned && field->kind != FieldKind::kEnum)) {
    return std::nullopt;
  }
  return cached_scalar_raw(field_id);
}

bool DecodedMessage::condition_matches(const CompiledField& field) const {
  if (!field.has_condition) {
    return true;
  }
  const auto value = raw_unsigned(field.condition_field);
  return value.has_value() && *value == field.condition_equals;
}

bool DecodedMessage::presence_matches(const CompiledField& field) const {
  if (!field.has_presence) {
    return true;
  }
  const auto value = raw_unsigned(field.presence_field);
  return value.has_value() && (((*value >> field.presence_bit) & 1ULL) != 0U);
}

std::optional<uint64_t> DecodedMessage::get_unsigned(FieldId field_id) const {
  if (!field_selected(field_id)) {
    return std::nullopt;
  }
  return raw_unsigned(field_id);
}

std::optional<int64_t> DecodedMessage::get_signed(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kSigned || !field_selected(field_id)) {
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
  if (field == nullptr || field->kind != FieldKind::kFloat32 || field->width_bytes != sizeof(uint32_t) ||
      !field_selected(field_id)) {
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
  if (field == nullptr || field->kind != FieldKind::kFloat64 || field->width_bytes != sizeof(uint64_t) ||
      !field_selected(field_id)) {
    return std::nullopt;
  }
  const auto value = cached_scalar_raw(field_id);
  if (UPR_UNLIKELY(!value.has_value())) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }
  return std::bit_cast<double>(*value);
}

std::optional<ByteSpan> DecodedMessage::get_bytes(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind == FieldKind::kCollection || field->kind == FieldKind::kVariant) {
    return std::nullopt;
  }
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

std::optional<DecodedCollectionView> DecodedMessage::get_collection(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kCollection || protocol_ == nullptr) {
    return std::nullopt;
  }
  const auto resolved = resolved_field(field_id);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  const CompiledMessage* element_layout = protocol_->struct_by_id(field->struct_id);
  if (element_layout == nullptr) {
    return std::nullopt;
  }
  size_t count = field->fixed_count;
  if (field->dynamic_count) {
    const auto dynamic_count = raw_unsigned(field->count_from_field);
    if (!dynamic_count.has_value()) {
      return std::nullopt;
    }
    count = static_cast<size_t>(*dynamic_count);
  }
  DecodedCollectionView view;
  view.protocol_ = protocol_;
  view.element_layout_ = element_layout;
  view.frame_ = frame_.subspan(resolved->offset, resolved->size);
  view.count_ = count;
  return view;
}

std::optional<DecodedMessage> DecodedMessage::get_variant(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kVariant || protocol_ == nullptr) {
    return std::nullopt;
  }
  const auto resolved = resolved_field(field_id);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  const auto tag_value = raw_unsigned(field->tag_from_field);
  if (!tag_value.has_value()) {
    return std::nullopt;
  }
  const CompiledVariantCase* variant_case = find_variant_case(*field, *tag_value);
  if (variant_case == nullptr) {
    return std::nullopt;
  }
  const CompiledMessage* nested_layout = protocol_->struct_by_id(variant_case->struct_id);
  if (nested_layout == nullptr) {
    return std::nullopt;
  }
  DecodedMessage nested;
  size_t bytes_consumed = 0;
  if (nested.assign_from_layout(
          *protocol_, *nested_layout, frame_.subspan(resolved->offset, resolved->size), &bytes_consumed) !=
      DecodeStatus::kOk) {
    return std::nullopt;
  }
  return nested;
}

std::optional<uint64_t> DecodedMessage::get_bit_unsigned(BitFieldId bit_field_id) const {
  const CompiledBitField* bit_field = bit_field_definition(bit_field_id);
  if (bit_field == nullptr || !field_selected(bit_field->container_field_id)) {
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

std::optional<DecodedCollectionView> DecodedMessage::get_collection(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() ? get_collection(*resolved) : std::nullopt;
}

std::optional<DecodedMessage> DecodedMessage::get_variant(std::string_view name) const {
  const auto resolved = field_id(name);
  return resolved.has_value() ? get_variant(*resolved) : std::nullopt;
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

std::optional<DecodedMessage> DecodedCollectionView::at(size_t index) const {
  if (!valid() || index >= count_) {
    return std::nullopt;
  }
  if (element_layout_->has_fixed_size()) {
    const size_t element_size = element_layout_->minimum_size();
    if (UPR_UNLIKELY(element_size == 0U)) {
      return std::nullopt;
    }
    size_t offset = 0;
    if (UPR_UNLIKELY(!checked_mul_size(index, element_size, &offset) || offset > frame_.size())) {
      return std::nullopt;
    }
    DecodedMessage element;
    size_t bytes_consumed = 0;
    if (element.assign_from_layout(
            *protocol_, *element_layout_, frame_.subspan(offset), &bytes_consumed, nullptr, true) !=
        DecodeStatus::kOk) {
      return std::nullopt;
    }
    return element;
  }
  size_t offset = 0;
  for (size_t current = 0; current <= index; ++current) {
    DecodedMessage element;
    size_t bytes_consumed = 0;
    if (element.assign_from_layout(
            *protocol_, *element_layout_, frame_.subspan(offset), &bytes_consumed, nullptr, true) !=
        DecodeStatus::kOk) {
      return std::nullopt;
    }
    if (current == index) {
      return element;
    }
    if (UPR_UNLIKELY(bytes_consumed == 0U || !checked_add_size(offset, bytes_consumed, &offset) ||
                     offset > frame_.size())) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

// Recursive descent is required for nested struct layouts.
// NOLINTNEXTLINE(misc-no-recursion)
DecodeStatus DecodedMessage::assign_from_layout(const CompiledProtocol& protocol,
                                                const CompiledMessage& layout,
                                                ByteSpan frame,
                                                size_t* bytes_consumed,
                                                const std::array<bool, kMaxFieldsPerMessage>* selected_fields,
                                                bool allow_prefix_trailing) {
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
  candidate.selection_active_ = selected_fields != nullptr;
  if (selected_fields != nullptr) {
    candidate.selected_fields_ = *selected_fields;
  } else {
    candidate.selected_fields_.fill(true);
  }

  size_t offset = 0;
  for (const CompiledField& field : layout.fields()) {
    if (UPR_UNLIKELY(!candidate.condition_matches(field) || !candidate.presence_matches(field))) {
      candidate.field_present_[field.id] = false;
      candidate.resolved_fields_[field.id] = {.offset = offset, .size = 0};
      continue;
    }

    offset = align_up(offset, field.alignment);
    if (UPR_UNLIKELY(offset > frame.size())) {
      return DecodeStatus::kSchemaMismatch;
    }

    size_t field_size = field.is_scalar() ? field.width_bytes : field.fixed_size;
    if (field.kind == FieldKind::kBytes || field.kind == FieldKind::kString) {
      if (field.dynamic_size) {
        const auto dependency_value = candidate.raw_unsigned(field.size_from_field);
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
          nested.assign_from_layout(protocol, *nested_layout, frame.subspan(offset), &nested_size, nullptr, true);
      if (nested_status != DecodeStatus::kOk) {
        return nested_status;
      }
      field_size = nested_size;
    } else if (field.kind == FieldKind::kCollection) {
      size_t count = field.fixed_count;
      if (field.dynamic_count) {
        const auto dependency_value = candidate.raw_unsigned(field.count_from_field);
        if (UPR_UNLIKELY(!dependency_value.has_value())) {
          return DecodeStatus::kInvalidData;
        }
        count = static_cast<size_t>(*dependency_value);
      }
      const CompiledMessage* element_layout = protocol.struct_by_id(field.struct_id);
      if (element_layout == nullptr) {
        return DecodeStatus::kInvalidData;
      }
      if (count == 0) {
        field_size = 0;
      } else if (element_layout->has_fixed_size()) {
        const size_t element_size = element_layout->minimum_size();
        if (UPR_UNLIKELY(element_size == 0U)) {
          return DecodeStatus::kSchemaMismatch;
        }
        const size_t remaining_bytes = frame.size() - offset;
        if (UPR_UNLIKELY(count > (remaining_bytes / element_size))) {
          return DecodeStatus::kSchemaMismatch;
        }
        field_size = count * element_size;
      } else {
        size_t total_size = 0;
        for (size_t index = 0; index < count; ++index) {
          DecodedMessage element;
          size_t bytes_consumed = 0;
          const DecodeStatus status = element.assign_from_layout(
              protocol, *element_layout, frame.subspan(offset + total_size), &bytes_consumed, nullptr, true);
          if (status != DecodeStatus::kOk) {
            return status;
          }
          if (UPR_UNLIKELY(bytes_consumed == 0U || !checked_add_size(total_size, bytes_consumed, &total_size) ||
                           total_size > (frame.size() - offset))) {
            return DecodeStatus::kSchemaMismatch;
          }
        }
        field_size = total_size;
      }
    } else if (field.kind == FieldKind::kVariant) {
      const auto tag_value = candidate.raw_unsigned(field.tag_from_field);
      if (!tag_value.has_value()) {
        return DecodeStatus::kInvalidData;
      }
      const CompiledVariantCase* variant_case = find_variant_case(field, *tag_value);
      if (variant_case == nullptr) {
        return DecodeStatus::kInvalidData;
      }
      const CompiledMessage* case_layout = protocol.struct_by_id(variant_case->struct_id);
      if (case_layout == nullptr) {
        return DecodeStatus::kInvalidData;
      }
      DecodedMessage nested;
      size_t bytes_consumed = 0;
      const DecodeStatus status =
          nested.assign_from_layout(protocol, *case_layout, frame.subspan(offset), &bytes_consumed, nullptr, true);
      if (status != DecodeStatus::kOk) {
        return status;
      }
      field_size = bytes_consumed;
    }

    size_t field_end = 0;
    if (UPR_UNLIKELY(!checked_add_size(offset, field_size, &field_end) || field_end > frame.size())) {
      return DecodeStatus::kSchemaMismatch;
    }
    candidate.field_present_[field.id] = true;
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
    if (field.is_reserved) {
      for (const std::byte byte : field_bytes) {
        if (UPR_UNLIKELY(byte != std::byte{field.reserved_fill_byte})) {
          return DecodeStatus::kSchemaMismatch;
        }
      }
    }

    offset = field_end;
  }

  if (UPR_UNLIKELY(!allow_prefix_trailing && !layout.allow_trailing_bytes() && offset != frame.size())) {
    return DecodeStatus::kSchemaMismatch;
  }

  for (const CompiledChecksum& checksum : layout.checksums()) {
    if (checksum.function == nullptr) {
      return DecodeStatus::kInvalidData;
    }
    const auto from_offset = checksum_anchor_offset(
        checksum.from, frame, candidate.resolved_fields_, candidate.field_present_, candidate.field_count_);
    const auto to_offset = checksum_anchor_offset(
        checksum.to, frame, candidate.resolved_fields_, candidate.field_present_, candidate.field_count_);
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
    const auto expected = candidate.raw_unsigned(checksum.field_id);
    if (UPR_UNLIKELY(!expected.has_value() || *expected != actual)) {
      return DecodeStatus::kChecksumMismatch;
    }
  }

  for (const CompiledValidationRule& validation : layout.validations()) {
    if (validation.has_when) {
      const auto when_value = candidate.raw_unsigned(validation.when_field_id);
      if (!when_value.has_value() || *when_value != validation.when_equals) {
        continue;
      }
    }
    const auto lhs = candidate.raw_unsigned(validation.field_id);
    if (!lhs.has_value()) {
      return DecodeStatus::kInvalidData;
    }
    uint64_t rhs = validation.value;
    if (validation.compare_to_field) {
      const auto rhs_value = candidate.raw_unsigned(validation.other_field_id);
      if (!rhs_value.has_value()) {
        return DecodeStatus::kInvalidData;
      }
      rhs = *rhs_value;
    }
    if (validation.multiplier != 1U) {
      rhs *= validation.multiplier;
    }
    if (UPR_UNLIKELY(!validation_operator_matches(validation.op, *lhs, rhs))) {
      return DecodeStatus::kSchemaMismatch;
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
