#include "universal_protocol_runtime/adapters/frame_channel.hpp"

#include <algorithm>
#include <array>

#include "universal_protocol_runtime/adapters/transport_capabilities.hpp"

namespace universal_protocol_runtime {
namespace {

void encode_little_endian(size_t value, uint8_t width, std::vector<std::byte>* output) {
  output->resize(width);
  for (uint8_t index = 0; index < width; ++index) {
    (*output)[index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
  }
}

size_t decode_little_endian(ByteSpan bytes) {
  size_t value = 0;
  for (size_t index = 0; index < bytes.size(); ++index) {
    value |= static_cast<size_t>(std::to_integer<uint8_t>(bytes[index])) << (index * 8U);
  }
  return value;
}

}  // namespace

FrameChannel::FrameChannel(IByteStreamTransport& transport, FrameChannelOptions options)
    : transport_(&transport),
      options_(options),
      scratch_prefix_(options.prefix_width_bytes),
      scratch_read_buffer_(4096) {
  stream_buffer_.reserve(options_.max_frame_size + options_.prefix_width_bytes);
}

Status FrameChannel::send_frame(ByteSpan frame) {
  if (frame.size() > options_.max_frame_size) {
    return exhausted("Frame exceeds configured max frame size.");
  }
  if (has_capability(transport_->capabilities(), TransportCapability::kPreservesFrameBoundaries)) {
    return write_fully(frame);
  }
  const Status prefix_status = write_frame_prefix(frame.size());
  if (!prefix_status.ok()) {
    return prefix_status;
  }
  const std::array<ByteSpan, 2> spans = {
      ByteSpan(scratch_prefix_.data(), scratch_prefix_.size()),
      frame,
  };
  return write_spans_fully(spans);
}

Status FrameChannel::send_frames(std::span<const ByteSpan> frames) {
  for (const ByteSpan frame : frames) {
    const Status status = send_frame(frame);
    if (!status.ok()) {
      return status;
    }
  }
  return Status::ok_status();
}

FrameChannelPollResult FrameChannel::receive_frame(std::vector<std::byte>* frame) {
  const StatusOr<TransportBufferLease> leased = try_acquire_frame();
  if (!leased.ok()) {
    const Status& status = leased.status();
    if (status.code() == StatusCode::kNotFound) {
      return {.status = FrameChannelPollStatus::kNeedMoreData};
    }
    return {
        .status = FrameChannelPollStatus::kTransportError,
        .transport_status = status,
    };
  }
  if (!leased.value().valid) {
    return {.status = FrameChannelPollStatus::kNeedMoreData};
  }
  frame->assign(leased.value().bytes.begin(), leased.value().bytes.end());
  (void)release_frame(leased.value());
  return {
      .status = FrameChannelPollStatus::kFrameReady,
      .bytes_transferred = frame->size(),
  };
}

StatusOr<TransportBufferLease> FrameChannel::try_acquire_frame() {
  if (has_capability(transport_->capabilities(), TransportCapability::kPreservesFrameBoundaries)) {
    return transport_->try_acquire_receive_buffer();
  }

  while (true) {
    const StatusOr<size_t> frame_length = try_parse_frame_length();
    if (frame_length.ok()) {
      const size_t required_bytes = options_.prefix_width_bytes + frame_length.value();
      if (stream_buffer_.size() - stream_buffer_offset_ < required_bytes) {
        const FrameChannelPollResult fill_result = fill_stream_buffer();
        if (fill_result.status != FrameChannelPollStatus::kNeedMoreData) {
          return not_found("Frame is not ready.");
        }
        continue;
      }
      const size_t frame_offset = stream_buffer_offset_ + options_.prefix_width_bytes;
      return TransportBufferLease{
          .bytes = ByteSpan(stream_buffer_.data() + frame_offset, frame_length.value()),
          .token = frame_offset,
          .valid = true,
      };
    }

    if (frame_length.status().code() == StatusCode::kExhausted) {
      return frame_length.status();
    }
    const FrameChannelPollResult fill_result = fill_stream_buffer();
    if (fill_result.status == FrameChannelPollStatus::kTransportError) {
      return fill_result.transport_status;
    }
    if (fill_result.status == FrameChannelPollStatus::kEndOfStream) {
      return not_found("End of stream.");
    }
    if (fill_result.status == FrameChannelPollStatus::kWouldBlock) {
      return not_found("Frame is not ready.");
    }
  }
}

Status FrameChannel::release_frame(const TransportBufferLease& lease) {
  if (has_capability(transport_->capabilities(), TransportCapability::kPreservesFrameBoundaries)) {
    return transport_->release_receive_buffer(lease);
  }
  if (!lease.valid) {
    return invalid_argument("Cannot release an invalid frame lease.");
  }
  stream_buffer_offset_ += options_.prefix_width_bytes + lease.bytes.size();
  compact_stream_buffer();
  return Status::ok_status();
}

Status FrameChannel::write_fully(ByteSpan bytes) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    const WriteResult result = transport_->write(bytes.subspan(offset));
    if (!result.status.ok()) {
      return result.status;
    }
    offset += result.bytes_written;
    if (offset == bytes.size()) {
      break;
    }
    if (!result.would_block && result.bytes_written == 0) {
      return io_error("Transport write made no progress.");
    }
    const StatusOr<bool> writable = transport_->wait_until_writable(1000);
    if (!writable.ok()) {
      return writable.status();
    }
    if (!writable.value()) {
      return exhausted("Timed out waiting for transport writability.");
    }
  }
  return Status::ok_status();
}

Status FrameChannel::write_spans_fully(std::span<const ByteSpan> spans) {
  size_t span_index = 0;
  size_t span_offset = 0;
  while (span_index < spans.size()) {
    while (span_index < spans.size() && spans[span_index].size() <= span_offset) {
      ++span_index;
      span_offset = 0;
    }
    if (span_index >= spans.size()) {
      break;
    }

    std::array<ByteSpan, 2> pending{};
    size_t pending_count = 0;
    pending[pending_count++] = spans[span_index].subspan(span_offset);
    if ((span_index + 1U) < spans.size() && !spans[span_index + 1U].empty()) {
      pending[pending_count++] = spans[span_index + 1U];
    }

    const WriteResult result = transport_->writev(std::span<const ByteSpan>(pending.data(), pending_count));
    if (!result.status.ok()) {
      return result.status;
    }

    size_t consumed = result.bytes_written;
    while (consumed > 0 && span_index < spans.size()) {
      const size_t remaining = spans[span_index].size() - span_offset;
      if (consumed < remaining) {
        span_offset += consumed;
        consumed = 0;
        break;
      }
      consumed -= remaining;
      ++span_index;
      span_offset = 0;
    }

    if (span_index >= spans.size()) {
      break;
    }
    if (!result.would_block && result.bytes_written == 0) {
      return io_error("Transport vectored write made no progress.");
    }
    const StatusOr<bool> writable = transport_->wait_until_writable(1000);
    if (!writable.ok()) {
      return writable.status();
    }
    if (!writable.value()) {
      return exhausted("Timed out waiting for transport writability.");
    }
  }
  return Status::ok_status();
}

Status FrameChannel::write_frame_prefix(size_t frame_size) {
  encode_little_endian(frame_size, options_.prefix_width_bytes, &scratch_prefix_);
  return Status::ok_status();
}

StatusOr<size_t> FrameChannel::try_parse_frame_length() const {
  const size_t readable_bytes = stream_buffer_.size() - stream_buffer_offset_;
  if (readable_bytes < options_.prefix_width_bytes) {
    return not_found("Frame prefix is incomplete.");
  }
  const ByteSpan prefix(stream_buffer_.data() + stream_buffer_offset_, options_.prefix_width_bytes);
  const size_t frame_length = decode_little_endian(prefix);
  if (frame_length > options_.max_frame_size) {
    return exhausted("Frame exceeds configured max frame size.");
  }
  return frame_length;
}

FrameChannelPollResult FrameChannel::fill_stream_buffer() {
  const size_t unread_bytes = stream_buffer_.size() - stream_buffer_offset_;
  if (stream_buffer_offset_ > 0 && (unread_bytes == 0 || stream_buffer_offset_ >= (stream_buffer_.size() / 2U))) {
    compact_stream_buffer();
  }

  size_t target_read_size = 4096;
  if (options_.max_frame_size > target_read_size) {
    target_read_size = std::min<size_t>(options_.max_frame_size, 65536);
  }
  if (scratch_read_buffer_.size() != target_read_size) {
    scratch_read_buffer_.resize(target_read_size);
  }

  const ReadResult result = transport_->read(MutableByteSpan(scratch_read_buffer_.data(), scratch_read_buffer_.size()));
  if (!result.status.ok()) {
    return {
        .status = FrameChannelPollStatus::kTransportError,
        .transport_status = result.status,
    };
  }
  if (result.bytes_read > 0) {
    stream_buffer_.insert(
        stream_buffer_.end(), scratch_read_buffer_.begin(), scratch_read_buffer_.begin() + result.bytes_read);
    return {
        .status = FrameChannelPollStatus::kNeedMoreData,
        .bytes_transferred = result.bytes_read,
    };
  }
  if (result.would_block) {
    return {.status = FrameChannelPollStatus::kWouldBlock};
  }
  if (result.end_of_stream) {
    return {.status = FrameChannelPollStatus::kEndOfStream};
  }
  return {.status = FrameChannelPollStatus::kNeedMoreData};
}

void FrameChannel::compact_stream_buffer() {
  if (stream_buffer_offset_ == 0) {
    return;
  }
  if (stream_buffer_offset_ >= stream_buffer_.size()) {
    stream_buffer_.clear();
    stream_buffer_offset_ = 0;
    return;
  }
  std::move(stream_buffer_.begin() + static_cast<ptrdiff_t>(stream_buffer_offset_),
            stream_buffer_.end(),
            stream_buffer_.begin());
  stream_buffer_.resize(stream_buffer_.size() - stream_buffer_offset_);
  stream_buffer_offset_ = 0;
}

}  // namespace universal_protocol_runtime
