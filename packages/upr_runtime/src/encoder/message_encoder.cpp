#include "universal_protocol_runtime/encoder/message_encoder.hpp"

#include <cstring>

#include "universal_protocol_runtime/core/compiler_hints.hpp"
#include "universal_protocol_runtime/decoder/direct_decode_support.hpp"
#include "universal_protocol_runtime/encoder/direct_encode_support.hpp"

namespace universal_protocol_runtime {
namespace {

// Compute a checksum value over [data] using the builtin algorithm stored in
// [chk].  Returns 0 for unknown/custom algorithms with no registered function
// (the caller is responsible for using the registry in that case).
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

    if (is_checksum_field(fid)) {
      // Write zero placeholder; finalize() will overwrite with the real value.
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

    // A required field was skipped — the user must call set_* for it first.
    failed_ = true;
    return EncodeStatus::kInvalidData;
  }
  next_field_id_ = target_id;
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::set_unsigned(FieldId id, uint64_t value) {
  if (UPR_UNLIKELY(failed_)) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id >= layout_->fields().size())) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id < next_field_id_)) {
    return EncodeStatus::kInvalidData;  // out-of-order write
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
  if (UPR_UNLIKELY(failed_)) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id >= layout_->fields().size())) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id < next_field_id_)) {
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
  // Truncate to field width for wire representation.
  const auto raw_value = static_cast<uint64_t>(value);
  const EncodeStatus status = write_scalar_field(id, raw_value);
  if (UPR_UNLIKELY(status != EncodeStatus::kOk)) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::set_float32(FieldId id, float value) {
  if (UPR_UNLIKELY(failed_)) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id >= layout_->fields().size())) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id < next_field_id_)) {
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
  const auto raw_value = static_cast<uint64_t>(std::bit_cast<uint32_t>(value));
  const EncodeStatus status = write_scalar_field(id, raw_value);
  if (UPR_UNLIKELY(status != EncodeStatus::kOk)) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::set_float64(FieldId id, double value) {
  if (UPR_UNLIKELY(failed_)) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id >= layout_->fields().size())) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id < next_field_id_)) {
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
  const auto raw_value = std::bit_cast<uint64_t>(value);
  const EncodeStatus status = write_scalar_field(id, raw_value);
  if (UPR_UNLIKELY(status != EncodeStatus::kOk)) {
    return status;
  }
  next_field_id_ = static_cast<FieldId>(id + 1U);
  return EncodeStatus::kOk;
}

EncodeStatus MessageBuilder::set_bytes(FieldId id, ByteSpan bytes) {
  if (UPR_UNLIKELY(failed_)) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id >= layout_->fields().size())) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id < next_field_id_)) {
    return EncodeStatus::kInvalidData;
  }
  const CompiledField& field = layout_->fields()[id];
  if (UPR_UNLIKELY(field.kind != FieldKind::kBytes && field.kind != FieldKind::kStruct)) {
    return EncodeStatus::kInvalidData;
  }
  const EncodeStatus adv = advance_to(id);
  if (UPR_UNLIKELY(adv != EncodeStatus::kOk)) {
    return adv;
  }

  const std::size_t size = [&]() -> std::size_t {
    if (field.dynamic_size) {
      return bytes.size();
    }
    return field.fixed_size;
  }();

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
  if (UPR_UNLIKELY(failed_)) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id >= layout_->fields().size())) {
    return EncodeStatus::kInvalidData;
  }
  if (UPR_UNLIKELY(id < next_field_id_)) {
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

  // Auto-advance past any remaining fields (fixed-value and checksum placeholders).
  const auto adv = advance_to(static_cast<FieldId>(layout_->fields().size()));
  if (UPR_UNLIKELY(adv != EncodeStatus::kOk)) {
    return adv;
  }

  // Compute and write each checksum.
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

    const ByteSpan data(buffer_.data() + from, to - from);
    const uint64_t checksum_value = compute_builtin_checksum(chk, data);

    const CompiledField& chk_field = layout_->fields()[chk.field_id];
    direct_encode_support::write_unsigned_scalar_unchecked(
        MutableByteSpan(buffer_.data() + field_starts_[chk.field_id], chk.result_width_bytes),
        checksum_value,
        chk_field.byte_order);
  }

  finalized_ = true;
  if (bytes_written != nullptr) {
    *bytes_written = offset_;
  }
  return EncodeStatus::kOk;
}

const CompiledMessage* ProtocolEncoder::find_message(std::string_view message_name) const {
  return protocol_->find_message(message_name);
}

std::optional<MessageBuilder> ProtocolEncoder::build(std::string_view message_name, MutableByteSpan buffer) const {
  const CompiledMessage* layout = protocol_->find_message(message_name);
  if (layout == nullptr) {
    return std::nullopt;
  }
  return MessageBuilder(*protocol_, *layout, buffer);
}

}  // namespace universal_protocol_runtime
