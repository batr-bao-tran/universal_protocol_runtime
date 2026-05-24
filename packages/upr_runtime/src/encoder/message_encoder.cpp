#include "universal_protocol_runtime/encoder/message_encoder.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <vector>

#include "universal_protocol_runtime/core/compiler_hints.hpp"
#include "universal_protocol_runtime/decoder/direct_decode_support.hpp"
#include "universal_protocol_runtime/encoder/direct_encode_support.hpp"

namespace universal_protocol_runtime {
namespace {

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
  return false;
}

uint64_t compute_builtin_checksum(const CompiledChecksum& chk, ByteSpan data) noexcept {
  switch (chk.builtin_kind) {
    case CompiledChecksum::BuiltinKind::kXor8:
      return direct_decode_support::runtime_checksum_xor8(data);
    case CompiledChecksum::BuiltinKind::kSum16:
      return direct_decode_support::runtime_checksum_sum16(data);
    case CompiledChecksum::BuiltinKind::kCrc16Ccitt:
      return direct_decode_support::runtime_checksum_crc16_ccitt(data);
    case CompiledChecksum::BuiltinKind::kCrc32:
      return direct_decode_support::runtime_checksum_crc32(data);
    case CompiledChecksum::BuiltinKind::kCrc32c:
      return direct_decode_support::runtime_checksum_crc32c(data);
    case CompiledChecksum::BuiltinKind::kCustom:
      if (chk.function != nullptr) {
        return chk.function(data);
      }
      return 0;
  }
  return 0;
}

template <typename RangeFn>
uint64_t compute_segmented_builtin_checksum(const CompiledChecksum& chk,
                                            size_t from,
                                            size_t to,
                                            RangeFn&& visit_range) {
  switch (chk.builtin_kind) {
    case CompiledChecksum::BuiltinKind::kXor8: {
      uint8_t value = 0;
      visit_range(from, to, [&value](ByteSpan chunk) {
        for (const std::byte byte : chunk) {
          value ^= std::to_integer<uint8_t>(byte);
        }
      });
      return value;
    }
    case CompiledChecksum::BuiltinKind::kSum16: {
      uint64_t sum = 0;
      visit_range(from, to, [&sum](ByteSpan chunk) {
        for (const std::byte byte : chunk) {
          sum += std::to_integer<uint8_t>(byte);
        }
      });
      return sum & 0xFFFFU;
    }
    case CompiledChecksum::BuiltinKind::kCrc16Ccitt:
    case CompiledChecksum::BuiltinKind::kCrc32:
    case CompiledChecksum::BuiltinKind::kCrc32c:
    case CompiledChecksum::BuiltinKind::kCustom:
      break;
  }

  ByteSpan single_chunk;
  bool saw_chunk = false;
  bool contiguous_single_chunk = true;
  visit_range(from, to, [&single_chunk, &saw_chunk, &contiguous_single_chunk](ByteSpan chunk) {
    if (!saw_chunk) {
      single_chunk = chunk;
      saw_chunk = true;
      return;
    }
    contiguous_single_chunk = false;
  });
  if (saw_chunk && contiguous_single_chunk) {
    if (chk.builtin_kind == CompiledChecksum::BuiltinKind::kCustom) {
      return chk.function == nullptr ? 0 : chk.function(single_chunk);
    }
    return compute_builtin_checksum(chk, single_chunk);
  }

  // Fall back to a contiguous scratch span for algorithms without an incremental path.
  std::vector<std::byte> scratch;
  scratch.reserve(to - from);
  visit_range(from, to, [&scratch](ByteSpan chunk) { scratch.insert(scratch.end(), chunk.begin(), chunk.end()); });
  if (chk.builtin_kind == CompiledChecksum::BuiltinKind::kCustom) {
    return chk.function == nullptr ? 0 : chk.function(ByteSpan(scratch));
  }
  return compute_builtin_checksum(chk, ByteSpan(scratch));
}

template <typename ScalarReader>
bool validations_pass(const CompiledMessage& layout, ScalarReader&& read_scalar) {
  for (const CompiledValidationRule& validation : layout.validations()) {
    if (validation.has_when) {
      const auto when_value = read_scalar(validation.when_field_id);
      if (!when_value.has_value() || *when_value != validation.when_equals) {
        continue;
      }
    }
    const auto lhs = read_scalar(validation.field_id);
    if (!lhs.has_value()) {
      return false;
    }
    uint64_t rhs = validation.value;
    if (validation.compare_to_field) {
      const auto rhs_value = read_scalar(validation.other_field_id);
      if (!rhs_value.has_value()) {
        return false;
      }
      rhs = *rhs_value;
    }
    if (validation.multiplier != 1U) {
      rhs *= validation.multiplier;
    }
    if (!validation_operator_matches(validation.op, *lhs, rhs)) {
      return false;
    }
  }
  return true;
}

}  // namespace

MessageBuilder::MessageBuilder(const CompiledProtocol& protocol, const CompiledMessage& layout, MutableByteSpan buffer)
    : protocol_(&protocol), layout_(&layout), buffer_(buffer) {
  if (layout.fields().size() > kMaxFieldsPerMessage) {
    failed_ = true;
    return;
  }
  for (const CompiledChecksum& chk : layout.checksums()) {
    if (chk.field_id < kMaxFieldsPerMessage) {
      checksum_field_mask_ |= (1ULL << chk.field_id);
    }
  }
}

ByteSpan MessageBuilder::view() const noexcept { return buffer_.subspan(0, offset_); }

EncodeStatus MessageBuilder::write_scalar_field(FieldId fid, uint64_t raw_value) {
  const CompiledField& field = layout_->fields()[fid];
  const std::size_t width = field.width_bytes;
  if (UPR_UNLIKELY(width > buffer_.size() - offset_)) {
    failed_ = true;
    return EncodeStatus::kBufferTooSmall;
  }
  field_starts_[fid] = offset_;
  direct_encode_support::write_unsigned_scalar_unchecked(
      MutableByteSpan(buffer_.data() + offset_, width), raw_value, field.byte_order);
  offset_ += width;
  field_ends_[fid] = offset_;
  written_scalars_[fid] = raw_value;
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::advance_to(FieldId target_id) {
  if (UPR_UNLIKELY(failed_)) {
    return EncodeStatus::kInvalidData;
  }
  const auto field_count = static_cast<FieldId>(layout_->fields().size());
  if (UPR_UNLIKELY(target_id > field_count)) {
    failed_ = true;
    return EncodeStatus::kInvalidData;
  }
  for (FieldId fid = next_field_id_; fid < target_id; ++fid) {
    const CompiledField& field = layout_->fields()[fid];
    const size_t aligned_offset = align_up(offset_, field.alignment);
    if (aligned_offset > buffer_.size()) {
      failed_ = true;
      return EncodeStatus::kBufferTooSmall;
    }
    if (aligned_offset != offset_) {
      direct_encode_support::fill_zeros(buffer_.subspan(offset_, aligned_offset - offset_));
      offset_ = aligned_offset;
    }

    if (is_checksum_field(fid)) {
      const std::size_t width = field.width_bytes;
      if (UPR_UNLIKELY(width > buffer_.size() - offset_)) {
        failed_ = true;
        return EncodeStatus::kBufferTooSmall;
      }
      field_starts_[fid] = offset_;
      direct_encode_support::fill_zeros(buffer_.subspan(offset_, width));
      offset_ += width;
      field_ends_[fid] = offset_;
      written_scalars_[fid] = 0;
      continue;
    }

    if (field.has_expected_unsigned) {
      const EncodeStatus status = write_scalar_field(fid, field.expected_unsigned);
      if (UPR_UNLIKELY(status != EncodeStatus::kOk)) {
        return status;
      }
      continue;
    }

    if (field.is_reserved) {
      if (field.fixed_size > buffer_.size() - offset_) {
        failed_ = true;
        return EncodeStatus::kBufferTooSmall;
      }
      field_starts_[fid] = offset_;
      std::memset(buffer_.data() + offset_, field.reserved_fill_byte, field.fixed_size);
      offset_ += field.fixed_size;
      field_ends_[fid] = offset_;
      continue;
    }

    failed_ = true;
    return EncodeStatus::kInvalidData;
  }
  next_field_id_ = target_id;
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::set_unsigned(FieldId id, uint64_t value) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (UPR_UNLIKELY(field.kind != FieldKind::kUnsigned && field.kind != FieldKind::kEnum)) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (UPR_UNLIKELY(adv != EncodeStatus::kOk)) {
    return adv;
  }
  const EncodeStatus status = write_scalar_field(id, value);
  if (UPR_UNLIKELY(status != EncodeStatus::kOk)) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::set_signed(FieldId id, int64_t value) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (UPR_UNLIKELY(field.kind != FieldKind::kSigned)) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (UPR_UNLIKELY(adv != EncodeStatus::kOk)) {
    return adv;
  }
  const EncodeStatus status = write_scalar_field(id, static_cast<uint64_t>(value));
  if (UPR_UNLIKELY(status != EncodeStatus::kOk)) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::set_float32(FieldId id, float value) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (UPR_UNLIKELY(field.kind != FieldKind::kFloat32)) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (UPR_UNLIKELY(adv != EncodeStatus::kOk)) {
    return adv;
  }
  const EncodeStatus status = write_scalar_field(id, static_cast<uint64_t>(std::bit_cast<uint32_t>(value)));
  if (UPR_UNLIKELY(status != EncodeStatus::kOk)) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::set_float64(FieldId id, double value) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (UPR_UNLIKELY(field.kind != FieldKind::kFloat64)) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (UPR_UNLIKELY(adv != EncodeStatus::kOk)) {
    return adv;
  }
  const EncodeStatus status = write_scalar_field(id, std::bit_cast<uint64_t>(value));
  if (UPR_UNLIKELY(status != EncodeStatus::kOk)) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::set_bytes(FieldId id, ByteSpan bytes) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (UPR_UNLIKELY(field.kind != FieldKind::kBytes && field.kind != FieldKind::kStruct)) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(field.is_reserved)) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (UPR_UNLIKELY(adv != EncodeStatus::kOk)) {
    return adv;
  }
  const std::size_t size = field.dynamic_size ? bytes.size() : field.fixed_size;
  if (UPR_UNLIKELY(size > buffer_.size() - offset_)) {
    failed_ = true;
    return EncodeStatus::kBufferTooSmall;
  }
  field_starts_[id] = offset_;
  if (size > 0U) {
    std::memcpy(buffer_.data() + offset_, bytes.data(), size);
  }
  offset_ += size;
  field_ends_[id] = offset_;
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::set_string(FieldId id, std::string_view str) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (UPR_UNLIKELY(field.kind != FieldKind::kString)) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (UPR_UNLIKELY(adv != EncodeStatus::kOk)) {
    return adv;
  }
  const std::size_t size = field.dynamic_size ? str.size() : field.fixed_size;
  if (UPR_UNLIKELY(size > buffer_.size() - offset_)) {
    failed_ = true;
    return EncodeStatus::kBufferTooSmall;
  }
  field_starts_[id] = offset_;
  if (size > 0U) {
    std::memcpy(buffer_.data() + offset_, str.data(), size);
  }
  offset_ += size;
  field_ends_[id] = offset_;
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::finalize(std::size_t* bytes_written) {
  if (UPR_UNLIKELY(failed_)) {
    return EncodeStatus::kInvalidData;
  }
  if (finalized_) {
    if (bytes_written != nullptr) {
      *bytes_written = offset_;
    }
    return EncodeStatus::kOk;
  }

  const auto adv = advance_to(static_cast<FieldId>(layout_->fields().size()));
  if (UPR_UNLIKELY(adv != EncodeStatus::kOk)) {
    return adv;
  }

  for (const CompiledChecksum& chk : layout_->checksums()) {
    const std::size_t total = offset_;
    std::size_t from = 0;
    std::size_t to = 0;
    const auto resolve_anchor = [&](const CompiledChecksumAnchor& anchor, std::size_t* out) -> bool {
      switch (anchor.kind) {
        case ChecksumAnchorKind::kFrameStart:
          *out = 0U;
          return true;
        case ChecksumAnchorKind::kFrameEnd:
          *out = total;
          return true;
        case ChecksumAnchorKind::kFieldStart:
        case ChecksumAnchorKind::kBeforeSelf:
          if (anchor.field_id >= layout_->fields().size()) {
            return false;
          }
          *out = field_starts_[anchor.field_id];
          return true;
        case ChecksumAnchorKind::kFieldEnd:
        case ChecksumAnchorKind::kAfterSelf:
          if (anchor.field_id >= layout_->fields().size()) {
            return false;
          }
          *out = field_ends_[anchor.field_id];
          return true;
      }
      return false;
    };

    if (chk.from.kind == ChecksumAnchorKind::kFrameStart && chk.to.kind == ChecksumAnchorKind::kBeforeSelf &&
        chk.to.field_id == chk.field_id) {
      from = 0U;
      to = field_starts_[chk.field_id];
    } else if (!resolve_anchor(chk.from, &from) || !resolve_anchor(chk.to, &to)) {
      failed_ = true;
      return EncodeStatus::kInvalidData;
    }
    if (UPR_UNLIKELY(from > to || to > total)) {
      failed_ = true;
      return EncodeStatus::kInvalidData;
    }

    const uint64_t checksum_value = compute_builtin_checksum(chk, ByteSpan(buffer_.data() + from, to - from));
    const CompiledField& chk_field = layout_->fields()[chk.field_id];
    direct_encode_support::write_unsigned_scalar_unchecked(
        MutableByteSpan(buffer_.data() + field_starts_[chk.field_id], chk.result_width_bytes),
        checksum_value,
        chk_field.byte_order);
    written_scalars_[chk.field_id] = checksum_value;
  }

  if (!validations_pass(*layout_, [this](FieldId field_id) -> std::optional<uint64_t> {
        const CompiledField& field = layout_->fields()[field_id];
        if (field.kind != FieldKind::kUnsigned && field.kind != FieldKind::kEnum) {
          return std::nullopt;
        }
        return written_scalars_[field_id];
      })) {
    failed_ = true;
    return EncodeStatus::kInvalidData;
  }

  finalized_ = true;
  if (bytes_written != nullptr) {
    *bytes_written = offset_;
  }
  return EncodeStatus::kOk;
}

SegmentedMessageBuilder::SegmentedMessageBuilder(const CompiledProtocol& protocol,
                                                 const CompiledMessage& layout,
                                                 MutableByteSpan scratch_buffer)
    : protocol_(&protocol), layout_(&layout), scratch_buffer_(scratch_buffer) {
  field_segment_indices_.fill(kMaxSegments);
  if (layout.fields().size() > kMaxFieldsPerMessage) {
    failed_ = true;
    return;
  }
  for (const CompiledChecksum& chk : layout.checksums()) {
    if (chk.field_id < kMaxFieldsPerMessage) {
      checksum_field_mask_ |= (1ULL << chk.field_id);
    }
  }
}

EncodeStatus SegmentedMessageBuilder::emit_alignment_gap(FieldId next_id) {
  const size_t aligned = align_up(total_size_, layout_->fields()[next_id].alignment);
  if (aligned == total_size_) {
    return EncodeStatus::kOk;
  }
  const size_t gap = aligned - total_size_;
  if (gap > scratch_buffer_.size() - scratch_offset_ || segment_count_ >= kMaxSegments) {
    failed_ = true;
    return EncodeStatus::kBufferTooSmall;
  }
  direct_encode_support::fill_zeros(scratch_buffer_.subspan(scratch_offset_, gap));
  segments_[segment_count_] = EncodedSegment{.bytes = scratch_buffer_.subspan(scratch_offset_, gap)};
  segment_offsets_[segment_count_] = total_size_;
  segment_lengths_[segment_count_] = gap;
  ++segment_count_;
  scratch_offset_ += gap;
  total_size_ = aligned;
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::append_owned_bytes(FieldId id, ByteSpan bytes) {
  if (bytes.size() > scratch_buffer_.size() - scratch_offset_ || segment_count_ >= kMaxSegments) {
    failed_ = true;
    return EncodeStatus::kBufferTooSmall;
  }
  field_starts_[id] = total_size_;
  if (!bytes.empty()) {
    std::memcpy(scratch_buffer_.data() + scratch_offset_, bytes.data(), bytes.size());
  }
  field_segment_indices_[id] = segment_count_;
  segments_[segment_count_] = EncodedSegment{.bytes = scratch_buffer_.subspan(scratch_offset_, bytes.size())};
  segment_offsets_[segment_count_] = total_size_;
  segment_lengths_[segment_count_] = bytes.size();
  ++segment_count_;
  scratch_offset_ += bytes.size();
  total_size_ += bytes.size();
  field_ends_[id] = total_size_;
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::append_filled_bytes(FieldId id, size_t size, std::byte fill_byte) {
  if (size > scratch_buffer_.size() - scratch_offset_ || segment_count_ >= kMaxSegments) {
    failed_ = true;
    return EncodeStatus::kBufferTooSmall;
  }
  field_starts_[id] = total_size_;
  if (size != 0U) {
    std::memset(scratch_buffer_.data() + scratch_offset_, std::to_integer<int>(fill_byte), size);
  }
  field_segment_indices_[id] = segment_count_;
  segments_[segment_count_] = EncodedSegment{.bytes = scratch_buffer_.subspan(scratch_offset_, size)};
  segment_offsets_[segment_count_] = total_size_;
  segment_lengths_[segment_count_] = size;
  ++segment_count_;
  scratch_offset_ += size;
  total_size_ += size;
  field_ends_[id] = total_size_;
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::append_borrowed_bytes(FieldId id, ByteSpan bytes) {
  if (segment_count_ >= kMaxSegments) {
    failed_ = true;
    return EncodeStatus::kBufferTooSmall;
  }
  field_starts_[id] = total_size_;
  field_segment_indices_[id] = segment_count_;
  segments_[segment_count_] = EncodedSegment{.bytes = bytes};
  segment_offsets_[segment_count_] = total_size_;
  segment_lengths_[segment_count_] = bytes.size();
  ++segment_count_;
  total_size_ += bytes.size();
  field_ends_[id] = total_size_;
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::append_scalar(FieldId id, uint64_t raw_value) {
  const CompiledField& field = layout_->fields()[id];
  if (field.width_bytes > scratch_buffer_.size() - scratch_offset_) {
    failed_ = true;
    return EncodeStatus::kBufferTooSmall;
  }
  field_starts_[id] = total_size_;
  field_segment_indices_[id] = segment_count_;
  direct_encode_support::write_unsigned_scalar_unchecked(
      scratch_buffer_.subspan(scratch_offset_, field.width_bytes), raw_value, field.byte_order);
  segments_[segment_count_] = EncodedSegment{.bytes = scratch_buffer_.subspan(scratch_offset_, field.width_bytes)};
  segment_offsets_[segment_count_] = total_size_;
  segment_lengths_[segment_count_] = field.width_bytes;
  ++segment_count_;
  scratch_offset_ += field.width_bytes;
  total_size_ += field.width_bytes;
  field_ends_[id] = total_size_;
  written_scalars_[id] = raw_value;
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::advance_to(FieldId target_id) {
  if (UPR_UNLIKELY(failed_)) {
    return EncodeStatus::kInvalidData;
  }
  const auto field_count = static_cast<FieldId>(layout_->fields().size());
  if (UPR_UNLIKELY(target_id > field_count)) {
    failed_ = true;
    return EncodeStatus::kInvalidData;
  }
  for (FieldId fid = next_field_id_; fid < target_id; ++fid) {
    if (const EncodeStatus gap_status = emit_alignment_gap(fid); gap_status != EncodeStatus::kOk) {
      return gap_status;
    }
    const CompiledField& field = layout_->fields()[fid];
    if (is_checksum_field(fid)) {
      const EncodeStatus status = append_filled_bytes(fid, field.width_bytes, std::byte{0});
      if (status != EncodeStatus::kOk) {
        return status;
      }
      written_scalars_[fid] = 0;
      continue;
    }
    if (field.has_expected_unsigned) {
      const EncodeStatus status = append_scalar(fid, field.expected_unsigned);
      if (status != EncodeStatus::kOk) {
        return status;
      }
      continue;
    }
    if (field.is_reserved) {
      const EncodeStatus status = append_filled_bytes(fid, field.fixed_size, std::byte{field.reserved_fill_byte});
      if (status != EncodeStatus::kOk) {
        return status;
      }
      continue;
    }
    failed_ = true;
    return EncodeStatus::kInvalidData;
  }
  next_field_id_ = target_id;
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::set_unsigned(FieldId id, uint64_t value) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (field.kind != FieldKind::kUnsigned && field.kind != FieldKind::kEnum) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (adv != EncodeStatus::kOk) {
    return adv;
  }
  const EncodeStatus status = append_scalar(id, value);
  if (status != EncodeStatus::kOk) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::set_signed(FieldId id, int64_t value) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  if (layout_->fields()[id].kind != FieldKind::kSigned) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (adv != EncodeStatus::kOk) {
    return adv;
  }
  const EncodeStatus status = append_scalar(id, static_cast<uint64_t>(value));
  if (status != EncodeStatus::kOk) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::set_float32(FieldId id, float value) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  if (layout_->fields()[id].kind != FieldKind::kFloat32) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (adv != EncodeStatus::kOk) {
    return adv;
  }
  const EncodeStatus status = append_scalar(id, static_cast<uint64_t>(std::bit_cast<uint32_t>(value)));
  if (status != EncodeStatus::kOk) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::set_float64(FieldId id, double value) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  if (layout_->fields()[id].kind != FieldKind::kFloat64) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (adv != EncodeStatus::kOk) {
    return adv;
  }
  const EncodeStatus status = append_scalar(id, std::bit_cast<uint64_t>(value));
  if (status != EncodeStatus::kOk) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::set_bytes(FieldId id, ByteSpan bytes) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if ((field.kind != FieldKind::kBytes && field.kind != FieldKind::kStruct) || field.is_reserved) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (adv != EncodeStatus::kOk) {
    return adv;
  }
  const std::size_t size = field.dynamic_size ? bytes.size() : field.fixed_size;
  const EncodeStatus status = append_owned_bytes(id, ByteSpan(bytes.data(), size));
  if (status != EncodeStatus::kOk) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::attach_bytes(FieldId id, ByteSpan bytes) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (field.kind != FieldKind::kBytes || !field.dynamic_size || field.is_reserved) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (adv != EncodeStatus::kOk) {
    return adv;
  }
  const EncodeStatus status = append_borrowed_bytes(id, bytes);
  if (status != EncodeStatus::kOk) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::set_string(FieldId id, std::string_view str) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (field.kind != FieldKind::kString) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (adv != EncodeStatus::kOk) {
    return adv;
  }
  const std::size_t size = field.dynamic_size ? str.size() : field.fixed_size;
  const EncodeStatus status = append_owned_bytes(id, ByteSpan(reinterpret_cast<const std::byte*>(str.data()), size));
  if (status != EncodeStatus::kOk) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::attach_string(FieldId id, std::string_view str) {
  if (UPR_UNLIKELY(failed_ || id >= layout_->fields().size() || id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (field.kind != FieldKind::kString || !field.dynamic_size) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (adv != EncodeStatus::kOk) {
    return adv;
  }
  const auto* data = reinterpret_cast<const std::byte*>(str.data());
  const EncodeStatus status = append_borrowed_bytes(id, ByteSpan(data, str.size()));
  if (status != EncodeStatus::kOk) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::finalize(std::size_t* bytes_written) {
  if (UPR_UNLIKELY(failed_)) {
    return EncodeStatus::kInvalidData;
  }
  if (finalized_) {
    if (bytes_written != nullptr) {
      *bytes_written = total_size_;
    }
    return EncodeStatus::kOk;
  }

  const EncodeStatus adv = advance_to(static_cast<FieldId>(layout_->fields().size()));
  if (adv != EncodeStatus::kOk) {
    return adv;
  }

  const auto visit_range = [this](size_t from, size_t to, auto&& visitor) {
    for (size_t index = 0; index < segment_count_; ++index) {
      const size_t segment_start = segment_offsets_[index];
      const size_t segment_end = segment_start + segment_lengths_[index];
      if (segment_end <= from || segment_start >= to) {
        continue;
      }
      const size_t chunk_start = std::max(from, segment_start);
      const size_t chunk_end = std::min(to, segment_end);
      const size_t local_offset = chunk_start - segment_start;
      visitor(segments_[index].bytes.subspan(local_offset, chunk_end - chunk_start));
    }
  };

  for (const CompiledChecksum& chk : layout_->checksums()) {
    const std::size_t total = total_size_;
    std::size_t from = 0;
    std::size_t to = 0;
    const auto resolve_anchor = [&](const CompiledChecksumAnchor& anchor, std::size_t* out) -> bool {
      switch (anchor.kind) {
        case ChecksumAnchorKind::kFrameStart:
          *out = 0U;
          return true;
        case ChecksumAnchorKind::kFrameEnd:
          *out = total;
          return true;
        case ChecksumAnchorKind::kFieldStart:
        case ChecksumAnchorKind::kBeforeSelf:
          if (anchor.field_id >= layout_->fields().size()) {
            return false;
          }
          *out = field_starts_[anchor.field_id];
          return true;
        case ChecksumAnchorKind::kFieldEnd:
        case ChecksumAnchorKind::kAfterSelf:
          if (anchor.field_id >= layout_->fields().size()) {
            return false;
          }
          *out = field_ends_[anchor.field_id];
          return true;
      }
      return false;
    };
    if (chk.from.kind == ChecksumAnchorKind::kFrameStart && chk.to.kind == ChecksumAnchorKind::kBeforeSelf &&
        chk.to.field_id == chk.field_id) {
      from = 0U;
      to = field_starts_[chk.field_id];
    } else if (!resolve_anchor(chk.from, &from) || !resolve_anchor(chk.to, &to)) {
      failed_ = true;
      return EncodeStatus::kInvalidData;
    }
    if (from > to || to > total) {
      failed_ = true;
      return EncodeStatus::kInvalidData;
    }
    const uint64_t checksum_value = compute_segmented_builtin_checksum(chk, from, to, visit_range);
    written_scalars_[chk.field_id] = checksum_value;
    const size_t segment_index = field_segment_indices_[chk.field_id];
    if (segment_index < segment_count_) {
      direct_encode_support::write_unsigned_scalar_unchecked(
          MutableByteSpan(const_cast<std::byte*>(segments_[segment_index].bytes.data()), chk.result_width_bytes),
          checksum_value,
          layout_->fields()[chk.field_id].byte_order);
    }
  }

  if (!validations_pass(*layout_, [this](FieldId field_id) -> std::optional<uint64_t> {
        const CompiledField& field = layout_->fields()[field_id];
        if (field.kind != FieldKind::kUnsigned && field.kind != FieldKind::kEnum) {
          return std::nullopt;
        }
        return written_scalars_[field_id];
      })) {
    failed_ = true;
    return EncodeStatus::kInvalidData;
  }

  finalized_ = true;
  if (bytes_written != nullptr) {
    *bytes_written = total_size_;
  }
  return EncodeStatus::kOk;
}

EncodeStatus SegmentedMessageBuilder::copy_to(MutableByteSpan output, std::size_t* bytes_written) const {
  if (!finalized_) {
    return EncodeStatus::kInvalidData;
  }
  if (output.size() < total_size_) {
    return EncodeStatus::kBufferTooSmall;
  }
  size_t cursor = 0;
  for (size_t index = 0; index < segment_count_; ++index) {
    const ByteSpan bytes = segments_[index].bytes;
    if (!bytes.empty()) {
      std::memcpy(output.data() + cursor, bytes.data(), bytes.size());
      cursor += bytes.size();
    }
  }
  if (bytes_written != nullptr) {
    *bytes_written = cursor;
  }
  return EncodeStatus::kOk;
}

const CompiledMessage* ProtocolEncoder::find_message(std::string_view message_name) const {
  return protocol_->find_message(message_name);
}

std::optional<EncodePlan> ProtocolEncoder::make_plan(std::string_view message_name) const {
  const CompiledMessage* layout = protocol_->find_message(message_name);
  if (layout == nullptr) {
    return std::nullopt;
  }
  return EncodePlan{.protocol = protocol_, .layout = layout};
}

std::optional<MessageBuilder> ProtocolEncoder::build(std::string_view message_name, MutableByteSpan buffer) const {
  const CompiledMessage* layout = protocol_->find_message(message_name);
  if (layout == nullptr) {
    return std::nullopt;
  }
  return MessageBuilder(*protocol_, *layout, buffer);
}

std::optional<MessageBuilder> ProtocolEncoder::build(const EncodePlan& plan, MutableByteSpan buffer) const {
  if (!plan.valid() || plan.protocol != protocol_) {
    return std::nullopt;
  }
  return MessageBuilder(*protocol_, *plan.layout, buffer);
}

std::optional<SegmentedMessageBuilder> ProtocolEncoder::build_segmented(std::string_view message_name,
                                                                        MutableByteSpan scratch_buffer) const {
  const CompiledMessage* layout = protocol_->find_message(message_name);
  if (layout == nullptr) {
    return std::nullopt;
  }
  return SegmentedMessageBuilder(*protocol_, *layout, scratch_buffer);
}

std::optional<SegmentedMessageBuilder> ProtocolEncoder::build_segmented(const EncodePlan& plan,
                                                                        MutableByteSpan scratch_buffer) const {
  if (!plan.valid() || plan.protocol != protocol_) {
    return std::nullopt;
  }
  return SegmentedMessageBuilder(*protocol_, *plan.layout, scratch_buffer);
}

}  // namespace universal_protocol_runtime
