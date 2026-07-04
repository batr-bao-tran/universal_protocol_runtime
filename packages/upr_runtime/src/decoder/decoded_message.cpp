#include "universal_protocol_runtime/decoder/decoded_message.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

#include "universal_protocol_runtime/core/unreachable.hpp"

namespace universal_protocol_runtime {
namespace {

constexpr uint8_t kMaxScalarWidthBytes = sizeof(uint64_t);
constexpr uint8_t kMaxBitWidth = 64U;
constexpr uint8_t kAsciiHighBitMask = 0x80U;
constexpr uint8_t kUtf8TwoByteLeadMask = 0xE0U;
constexpr uint8_t kUtf8ThreeByteLeadMask = 0xF0U;
constexpr uint8_t kUtf8FourByteLeadMask = 0xF8U;
constexpr uint8_t kUtf8ContinuationMask = 0xC0U;
constexpr uint8_t kUtf8ContinuationTag = 0x80U;
constexpr uint8_t kUtf8TwoByteLeadTag = 0xC0U;
constexpr uint8_t kUtf8ThreeByteLeadTag = 0xE0U;
constexpr uint8_t kUtf8FourByteLeadTag = 0xF0U;
constexpr uint8_t kUtf8SingleByteMask = 0x80U;
constexpr uint8_t kUtf8TwoBytePayloadMask = 0x1FU;
constexpr uint8_t kUtf8ThreeBytePayloadMask = 0x0FU;
constexpr uint8_t kUtf8FourBytePayloadMask = 0x07U;
constexpr uint8_t kUtf8ContinuationPayloadMask = 0x3FU;
constexpr uint32_t kUtf8MaxCodePoint = 0x10FFFFU;
constexpr uint32_t kUtf8SurrogateStart = 0xD800U;
constexpr uint32_t kUtf8SurrogateEnd = 0xDFFFU;
constexpr uint32_t kUtf8MinCodePointTwoByte = 0x80U;
constexpr uint32_t kUtf8MinCodePointThreeByte = 0x800U;
constexpr uint32_t kUtf8MinCodePointFourByte = 0x10000U;

std::optional<uint64_t> read_unsigned_scalar(ByteSpan bytes, ByteOrder byte_order) {
  if (bytes.empty() || bytes.size() > kMaxScalarWidthBytes) {
    return std::nullopt;
  }
  const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      switch (bytes.size()) {
        case 1:
          return static_cast<uint64_t>(data[0]);
        case 2:
          return static_cast<uint64_t>(data[0]) | (static_cast<uint64_t>(data[1]) << 8U);
        case 3:
          return static_cast<uint64_t>(data[0]) | (static_cast<uint64_t>(data[1]) << 8U) |
                 (static_cast<uint64_t>(data[2]) << 16U);
        case 4:
          return static_cast<uint64_t>(data[0]) | (static_cast<uint64_t>(data[1]) << 8U) |
                 (static_cast<uint64_t>(data[2]) << 16U) | (static_cast<uint64_t>(data[3]) << 24U);
        case 5:
          return static_cast<uint64_t>(data[0]) | (static_cast<uint64_t>(data[1]) << 8U) |
                 (static_cast<uint64_t>(data[2]) << 16U) | (static_cast<uint64_t>(data[3]) << 24U) |
                 (static_cast<uint64_t>(data[4]) << 32U);
        case 6:
          return static_cast<uint64_t>(data[0]) | (static_cast<uint64_t>(data[1]) << 8U) |
                 (static_cast<uint64_t>(data[2]) << 16U) | (static_cast<uint64_t>(data[3]) << 24U) |
                 (static_cast<uint64_t>(data[4]) << 32U) | (static_cast<uint64_t>(data[5]) << 40U);
        case 7:
          return static_cast<uint64_t>(data[0]) | (static_cast<uint64_t>(data[1]) << 8U) |
                 (static_cast<uint64_t>(data[2]) << 16U) | (static_cast<uint64_t>(data[3]) << 24U) |
                 (static_cast<uint64_t>(data[4]) << 32U) | (static_cast<uint64_t>(data[5]) << 40U) |
                 (static_cast<uint64_t>(data[6]) << 48U);
        case 8:
          return static_cast<uint64_t>(data[0]) | (static_cast<uint64_t>(data[1]) << 8U) |
                 (static_cast<uint64_t>(data[2]) << 16U) | (static_cast<uint64_t>(data[3]) << 24U) |
                 (static_cast<uint64_t>(data[4]) << 32U) | (static_cast<uint64_t>(data[5]) << 40U) |
                 (static_cast<uint64_t>(data[6]) << 48U) | (static_cast<uint64_t>(data[7]) << 56U);
        default:
          unreachable();
      }
    case ByteOrder::kBigEndian:
      switch (bytes.size()) {
        case 1:
          return static_cast<uint64_t>(data[0]);
        case 2:
          return (static_cast<uint64_t>(data[0]) << 8U) | static_cast<uint64_t>(data[1]);
        case 3:
          return (static_cast<uint64_t>(data[0]) << 16U) | (static_cast<uint64_t>(data[1]) << 8U) |
                 static_cast<uint64_t>(data[2]);
        case 4:
          return (static_cast<uint64_t>(data[0]) << 24U) | (static_cast<uint64_t>(data[1]) << 16U) |
                 (static_cast<uint64_t>(data[2]) << 8U) | static_cast<uint64_t>(data[3]);
        case 5:
          return (static_cast<uint64_t>(data[0]) << 32U) | (static_cast<uint64_t>(data[1]) << 24U) |
                 (static_cast<uint64_t>(data[2]) << 16U) | (static_cast<uint64_t>(data[3]) << 8U) |
                 static_cast<uint64_t>(data[4]);
        case 6:
          return (static_cast<uint64_t>(data[0]) << 40U) | (static_cast<uint64_t>(data[1]) << 32U) |
                 (static_cast<uint64_t>(data[2]) << 24U) | (static_cast<uint64_t>(data[3]) << 16U) |
                 (static_cast<uint64_t>(data[4]) << 8U) | static_cast<uint64_t>(data[5]);
        case 7:
          return (static_cast<uint64_t>(data[0]) << 48U) | (static_cast<uint64_t>(data[1]) << 40U) |
                 (static_cast<uint64_t>(data[2]) << 32U) | (static_cast<uint64_t>(data[3]) << 24U) |
                 (static_cast<uint64_t>(data[4]) << 16U) | (static_cast<uint64_t>(data[5]) << 8U) |
                 static_cast<uint64_t>(data[6]);
        case 8:
          return (static_cast<uint64_t>(data[0]) << 56U) | (static_cast<uint64_t>(data[1]) << 48U) |
                 (static_cast<uint64_t>(data[2]) << 40U) | (static_cast<uint64_t>(data[3]) << 32U) |
                 (static_cast<uint64_t>(data[4]) << 24U) | (static_cast<uint64_t>(data[5]) << 16U) |
                 (static_cast<uint64_t>(data[6]) << 8U) | static_cast<uint64_t>(data[7]);
        default:
          unreachable();
      }
  }
  unreachable();
}

std::optional<int64_t> sign_extend(uint64_t value, uint8_t width_bits) {
  if (width_bits == 0 || width_bits > kMaxBitWidth) {
    return std::nullopt;
  }
  if (width_bits == kMaxBitWidth) {
    return static_cast<int64_t>(value);
  }
  const uint64_t sign_mask = 1ULL << (width_bits - 1U);
  const uint64_t full_mask = (1ULL << width_bits) - 1ULL;
  value &= full_mask;
  if ((value & sign_mask) == 0U) {
    return static_cast<int64_t>(value);
  }
  return static_cast<int64_t>(value | ~full_mask);
}

template <typename FloatType, typename UIntType>
std::optional<FloatType> read_float(ByteSpan bytes, ByteOrder byte_order) {
  const auto raw = read_unsigned_scalar(bytes, byte_order);
  if (!raw.has_value()) {
    return std::nullopt;
  }
  const auto normalized = static_cast<UIntType>(*raw);
  return std::bit_cast<FloatType>(normalized);
}

bool is_valid_ascii(ByteSpan bytes) {
  return std::all_of(bytes.begin(), bytes.end(), [](const std::byte byte) {
    return (std::to_integer<uint8_t>(byte) & kAsciiHighBitMask) == 0U;
  });
}

bool is_valid_utf8(ByteSpan bytes) {
  size_t index = 0;
  while (index < bytes.size()) {
    const auto lead = std::to_integer<uint8_t>(bytes[index]);
    size_t continuation_count = 0;
    uint32_t code_point = 0;

    if ((lead & kUtf8SingleByteMask) == 0U) {
      continuation_count = 0;
      code_point = lead;
    } else if ((lead & kUtf8TwoByteLeadMask) == kUtf8TwoByteLeadTag) {
      continuation_count = 1;
      code_point = lead & kUtf8TwoBytePayloadMask;
      if (code_point == 0U) {
        return false;
      }
    } else if ((lead & kUtf8ThreeByteLeadMask) == kUtf8ThreeByteLeadTag) {
      continuation_count = 2;
      code_point = lead & kUtf8ThreeBytePayloadMask;
    } else if ((lead & kUtf8FourByteLeadMask) == kUtf8FourByteLeadTag) {
      continuation_count = 3;
      code_point = lead & kUtf8FourBytePayloadMask;
    } else {
      return false;
    }

    if (index + continuation_count >= bytes.size()) {
      return false;
    }
    for (size_t continuation = 0; continuation < continuation_count; ++continuation) {
      const auto next = std::to_integer<uint8_t>(bytes[index + continuation + 1U]);
      if ((next & kUtf8ContinuationMask) != kUtf8ContinuationTag) {
        return false;
      }
      code_point = (code_point << 6U) | static_cast<uint32_t>(next & kUtf8ContinuationPayloadMask);
    }

    if ((continuation_count == 1U && code_point < kUtf8MinCodePointTwoByte) ||
        (continuation_count == 2U && code_point < kUtf8MinCodePointThreeByte) ||
        (continuation_count == 3U && code_point < kUtf8MinCodePointFourByte) || code_point > kUtf8MaxCodePoint ||
        (code_point >= kUtf8SurrogateStart && code_point <= kUtf8SurrogateEnd)) {
      return false;
    }

    index += continuation_count + 1U;
  }
  return true;
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
  return std::nullopt;
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

std::optional<uint64_t> DecodedMessage::cached_container_unsigned(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || !field->is_scalar() || field->kind == FieldKind::kFloat32 ||
      field->kind == FieldKind::kFloat64) {
    return std::nullopt;
  }
  if (field_id >= field_count_) {
    return std::nullopt;
  }
  if (scalar_cache_valid_[field_id]) {
    return scalar_cache_values_[field_id];
  }
  const auto resolved = resolved_field(field_id);
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  const auto value = read_unsigned_scalar(frame_.subspan(resolved->offset, resolved->size), field->byte_order);
  if (!value.has_value()) {
    return std::nullopt;
  }
  scalar_cache_values_[field_id] = *value;
  scalar_cache_valid_[field_id] = true;
  return value;
}

std::optional<uint64_t> DecodedMessage::get_unsigned(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || (field->kind != FieldKind::kUnsigned && field->kind != FieldKind::kEnum)) {
    return std::nullopt;
  }
  return cached_container_unsigned(field_id);
}

std::optional<int64_t> DecodedMessage::get_signed(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kSigned) {
    return std::nullopt;
  }
  const auto raw = cached_container_unsigned(field_id);
  if (!raw.has_value()) {
    return std::nullopt;
  }
  return sign_extend(*raw, static_cast<uint8_t>(field->width_bytes * kBitsPerByte));
}

std::optional<float> DecodedMessage::get_float32(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kFloat32) {
    return std::nullopt;
  }
  const auto value = get_bytes(field_id);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return read_float<float, uint32_t>(*value, field->byte_order);
}

std::optional<double> DecodedMessage::get_float64(FieldId field_id) const {
  const CompiledField* field = field_definition(field_id);
  if (field == nullptr || field->kind != FieldKind::kFloat64) {
    return std::nullopt;
  }
  const auto value = get_bytes(field_id);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return read_float<double, uint64_t>(*value, field->byte_order);
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
  if (field == nullptr || field->kind != FieldKind::kString) {
    return std::nullopt;
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
  if (!resolved.has_value()) {
    return std::nullopt;
  }
  const CompiledMessage* nested_layout = protocol_->struct_by_id(field->struct_id);
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
  if (bit_field == nullptr) {
    return std::nullopt;
  }
  const auto container_value = cached_container_unsigned(bit_field->container_field_id);
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
  return sign_extend(*value, bit_field->width_bits);
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
  if (layout.fields().size() > kMaxFieldsPerMessage) {
    return DecodeStatus::kFieldLimitExceeded;
  }
  if (frame.size() < layout.minimum_size()) {
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
        if (!dependency_value.has_value() || *dependency_value > frame.size()) {
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

    if (offset + field_size > frame.size()) {
      return DecodeStatus::kSchemaMismatch;
    }
    candidate.resolved_fields_[field.id] = {.offset = offset, .size = field_size};

    if (field.has_expected_unsigned) {
      const auto actual = read_unsigned_scalar(frame.subspan(offset, field_size), field.byte_order);
      if (!actual.has_value() || *actual != field.expected_unsigned) {
        return DecodeStatus::kSchemaMismatch;
      }
    }

    if (field.kind == FieldKind::kString) {
      const ByteSpan string_bytes = frame.subspan(offset, field_size);
      const bool valid_encoding =
          field.string_encoding == StringEncoding::kAscii ? is_valid_ascii(string_bytes) : is_valid_utf8(string_bytes);
      if (!valid_encoding) {
        return DecodeStatus::kInvalidData;
      }
    }

    offset += field_size;
  }

  if (!layout.allow_trailing_bytes() && offset != frame.size()) {
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
    if (!from_offset.has_value() || !to_offset.has_value() || *from_offset > *to_offset || *to_offset > frame.size()) {
      return DecodeStatus::kSchemaMismatch;
    }
    const uint64_t actual = checksum.function(frame.subspan(*from_offset, *to_offset - *from_offset));
    const auto expected = candidate.get_unsigned(checksum.field_id);
    if (!expected.has_value() || *expected != actual) {
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
