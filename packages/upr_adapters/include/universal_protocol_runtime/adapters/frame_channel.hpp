#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__FRAME_CHANNEL_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__FRAME_CHANNEL_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "universal_protocol_runtime/adapters/byte_stream_transport.hpp"

namespace universal_protocol_runtime {

struct FrameChannelOptions {
  /**
   * @brief Byte width used for stream frame length prefixes.
   */
  uint8_t prefix_width_bytes = 4;
  /**
   * @brief Maximum accepted frame payload size in bytes.
   */
  size_t max_frame_size = 1U << 20U;
};

enum class FrameChannelPollStatus {
  kFrameReady,
  kNeedMoreData,
  kWouldBlock,
  kEndOfStream,
  kFrameTooLarge,
  kTransportError,
};

struct FrameChannelPollResult {
  /**
   * @brief Current polling status for frame reception.
   */
  FrameChannelPollStatus status = FrameChannelPollStatus::kNeedMoreData;
  /**
   * @brief Number of bytes transferred during this poll step.
   */
  size_t bytes_transferred = 0;
  /**
   * @brief Transport status when a transport error occurs.
   */
  Status transport_status = Status::ok_status();
};

/**
 * @brief Framed channel wrapper over a byte-stream transport.
 */
class FrameChannel {
 public:
  /**
   * @brief Constructs a frame channel around a transport.
   */
  explicit FrameChannel(IByteStreamTransport& transport, FrameChannelOptions options = {});

  /**
   * @brief Sends one frame over the channel.
   */
  Status send_frame(ByteSpan frame);
  /**
   * @brief Sends multiple frames in order.
   */
  Status send_frames(std::span<const ByteSpan> frames);

  /**
   * @brief Attempts to receive one frame into an owned buffer.
   */
  FrameChannelPollResult receive_frame(std::vector<std::byte>* frame);
  /**
   * @brief Attempts to acquire a frame as a lease.
   */
  StatusOr<TransportBufferLease> try_acquire_frame();
  /**
   * @brief Releases a previously acquired frame lease.
   */
  Status release_frame(const TransportBufferLease& lease);

 private:
  Status write_fully(ByteSpan bytes);
  Status write_spans_fully(std::span<const ByteSpan> spans);
  Status write_frame_prefix(size_t frame_size);
  StatusOr<size_t> try_parse_frame_length() const;
  FrameChannelPollResult fill_stream_buffer();
  void compact_stream_buffer();

  IByteStreamTransport* transport_ = nullptr;
  FrameChannelOptions options_;
  std::vector<std::byte> stream_buffer_;
  size_t stream_buffer_offset_ = 0;
  std::vector<std::byte> scratch_prefix_;
  std::vector<std::byte> scratch_read_buffer_;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__FRAME_CHANNEL_HPP_
