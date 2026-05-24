#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_RUNTIME__STREAM_RUNTIME_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_RUNTIME__STREAM_RUNTIME_HPP_
#include <array>
#include <cstddef>
#include <string_view>

#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/core/compiler_hints.hpp"
#include "universal_protocol_runtime/decoder/decode_status.hpp"
#include "universal_protocol_runtime/decoder/protocol_decoder.hpp"
#include "universal_protocol_runtime/framing/framer.hpp"
#include "universal_protocol_runtime/runtime/byte_ring_buffer.hpp"
#include "universal_protocol_runtime/transport/transport.hpp"

namespace universal_protocol_runtime {

/**
 * @brief High-level outcomes returned by one stream runtime poll cycle.
 */
enum class PollStatus {
  kMessageReady,
  kNeedMoreData,
  kEndOfStream,
  kFrameInvalid,
  kDecodeError,
  kBufferExhausted,
  kTransportError,
};

/**
 * @brief Policy applied when a frame cannot be decoded successfully.
 */
enum class DecodeFailurePolicy {
  kStop,
  kDropAndContinue,
  kQuarantine,
};

/**
 * @brief Converts a poll status to a stable string name.
 * @param status Status value to stringify.
 * @return String representation of the poll status.
 */
constexpr std::string_view to_string(PollStatus status) noexcept {
  switch (status) {
    case PollStatus::kMessageReady:
      return "message_ready";
    case PollStatus::kNeedMoreData:
      return "need_more_data";
    case PollStatus::kEndOfStream:
      return "end_of_stream";
    case PollStatus::kFrameInvalid:
      return "frame_invalid";
    case PollStatus::kDecodeError:
      return "decode_error";
    case PollStatus::kBufferExhausted:
      return "buffer_exhausted";
    case PollStatus::kTransportError:
      return "transport_error";
  }
  return "unknown";
}

/**
 * @brief Converts a decode failure policy to a stable string name.
 * @param policy Policy value to stringify.
 * @return String representation of the policy.
 */
constexpr std::string_view to_string(DecodeFailurePolicy policy) noexcept {
  switch (policy) {
    case DecodeFailurePolicy::kStop:
      return "stop";
    case DecodeFailurePolicy::kDropAndContinue:
      return "drop_and_continue";
    case DecodeFailurePolicy::kQuarantine:
      return "quarantine";
  }
  return "unknown";
}

/**
 * @brief Configures failure handling for streamed frame decode.
 */
struct StreamRuntimeOptions {
  DecodeFailurePolicy message_not_found_policy = DecodeFailurePolicy::kDropAndContinue;
  DecodeFailurePolicy schema_mismatch_policy = DecodeFailurePolicy::kQuarantine;
  DecodeFailurePolicy invalid_data_policy = DecodeFailurePolicy::kQuarantine;
  DecodeFailurePolicy checksum_mismatch_policy = DecodeFailurePolicy::kQuarantine;
  DecodeFailurePolicy field_limit_policy = DecodeFailurePolicy::kStop;
};

/**
 * @brief Result returned from one call to `StreamRuntime::poll`.
 */
struct PollResult {
  PollStatus status = PollStatus::kNeedMoreData;
  DecodeStatus decode_status = DecodeStatus::kOk;
  DecodeFailurePolicy policy = DecodeFailurePolicy::kStop;
  size_t bytes_consumed = 0;

  /**
   * @brief Reports whether a decoded message is available.
   * @return `true` when `poll` produced a message.
   */
  constexpr bool message_ready() const noexcept { return status == PollStatus::kMessageReady; }
};

/**
 * @brief Counters describing stream runtime activity.
 */
struct RuntimeStats {
  size_t bytes_read = 0;
  size_t transport_reads = 0;
  size_t frames_seen = 0;
  size_t frames_decoded = 0;
  size_t frame_errors = 0;
  size_t decode_errors = 0;
};

template <size_t Capacity>
/**
 * @brief Couples transport, framing, and decode into a single polling loop.
 * @tparam Capacity Internal byte ring buffer capacity in bytes.
 */
class StreamRuntime {
 public:
  /**
   * @brief Constructs a stream runtime with default failure policies.
   * @param transport Transport that supplies framed bytes.
   * @param framer Framer used to extract message boundaries.
   * @param decoder Decoder used to parse framed messages.
   */
  StreamRuntime(ITransport& transport, const IFramer& framer, const ProtocolDecoder& decoder)
      : StreamRuntime(transport, framer, decoder, {}) {}

  /**
   * @brief Constructs a stream runtime with explicit failure policies.
   * @param transport Transport that supplies framed bytes.
   * @param framer Framer used to extract message boundaries.
   * @param decoder Decoder used to parse framed messages.
   * @param options Failure-handling options.
   */
  StreamRuntime(ITransport& transport,
                const IFramer& framer,
                const ProtocolDecoder& decoder,
                StreamRuntimeOptions options)
      : transport_(&transport), framer_(&framer), decoder_(&decoder), options_(options) {}

  /**
   * @brief Destroys the stream runtime.
   * @return No return value.
   */
  ~StreamRuntime() noexcept = default;

  /**
   * @brief Returns cumulative runtime statistics.
   * @return Reference to the statistics snapshot.
   */
  const RuntimeStats& stats() const { return stats_; }

  // Greedy polling entry point for a single-threaded runtime.
  // One call may perform multiple transport reads before it returns with a
  // decoded message, a blocking boundary, end-of-stream, or an error.
  // When used with non-blocking transports, call this after a readiness signal
  // or backoff policy rather than spinning on repeated kNeedMoreData results.
  /**
   * @brief Polls transport, framing, and decode until a boundary condition is reached.
   * @param message Output decoded message when one is available.
   * @return Poll result describing the outcome of this cycle.
   */
  PollResult poll(DecodedMessage* message) {
    while (true) {
      ByteSpan frame;
      FrameSlice frame_slice;
      const FrameStatus frame_status = try_extract_frame(&frame, &frame_slice);
      if (frame_status == FrameStatus::kReady) {
        stats_.frames_seen += 1;
        const DecodeStatus decode_status = decoder_->decode_any(frame, message);
        buffer_.consume(frame_slice.bytes_consumed);
        if (decode_status == DecodeStatus::kOk) {
          stats_.frames_decoded += 1;
          return {
              .status = PollStatus::kMessageReady,
              .bytes_consumed = frame_slice.bytes_consumed,
          };
        }
        stats_.decode_errors += 1;
        return {
            .status = PollStatus::kDecodeError,
            .decode_status = decode_status,
            .policy = policy_for(decode_status),
            .bytes_consumed = frame_slice.bytes_consumed,
        };
      }
      if (frame_status == FrameStatus::kInvalidFrame) {
        stats_.frame_errors += 1;
        return {
            .status = PollStatus::kFrameInvalid,
        };
      }
      if (end_of_stream_ && buffer_.empty()) {
        return {
            .status = PollStatus::kEndOfStream,
        };
      }
      if (end_of_stream_ && !buffer_.empty()) {
        stats_.frame_errors += 1;
        return {
            .status = PollStatus::kFrameInvalid,
        };
      }

      const FillResult fill_result = fill_buffer();
      switch (fill_result) {
        case FillResult::kReadSome:
          continue;
        case FillResult::kWouldBlock:
          return {
              .status = PollStatus::kNeedMoreData,
          };
        case FillResult::kEndOfStream:
          continue;
        case FillResult::kNoSpace:
          return {
              .status = PollStatus::kBufferExhausted,
          };
        case FillResult::kTransportError:
          return {
              .status = PollStatus::kTransportError,
          };
      }
    }
  }

 private:
  enum class FillResult {
    kReadSome,
    kWouldBlock,
    kEndOfStream,
    kNoSpace,
    kTransportError,
  };

  FillResult fill_buffer() {
    MutableByteSpan writable = buffer_.writable_span();
    if (writable.empty()) {
      return FillResult::kNoSpace;
    }
    const ReadResult result = transport_->read(writable);
    stats_.transport_reads += 1;
    if (!result.status.ok()) {
      return FillResult::kTransportError;
    }
    buffer_.commit_write(result.bytes_read);
    stats_.bytes_read += result.bytes_read;
    end_of_stream_ = result.end_of_stream;
    if (result.bytes_read > 0) {
      return FillResult::kReadSome;
    }
    if (result.would_block) {
      return FillResult::kWouldBlock;
    }
    if (UPR_UNLIKELY(result.end_of_stream)) {
      return FillResult::kEndOfStream;  // LCOV_EXCL_LINE
    }
    return FillResult::kWouldBlock;
  }

  DecodeFailurePolicy policy_for(DecodeStatus status) const {
    switch (status) {
      case DecodeStatus::kOk:
        return DecodeFailurePolicy::kStop;  // LCOV_EXCL_LINE
      case DecodeStatus::kMessageNotFound:
        return options_.message_not_found_policy;
      case DecodeStatus::kSchemaMismatch:
        return options_.schema_mismatch_policy;
      case DecodeStatus::kInvalidData:
        return options_.invalid_data_policy;
      case DecodeStatus::kChecksumMismatch:
        return options_.checksum_mismatch_policy;
      case DecodeStatus::kFieldLimitExceeded:
        return options_.field_limit_policy;
    }
    return DecodeFailurePolicy::kStop;  // LCOV_EXCL_LINE
  }                                     // NOLINT(misc-no-recursion)

  FrameStatus try_extract_frame(ByteSpan* frame, FrameSlice* frame_slice) {
    const ByteSpan contiguous = buffer_.readable_span();
    FrameStatus status = framer_->try_frame(contiguous, frame_slice);
    if (status == FrameStatus::kReady) {
      *frame = contiguous.subspan(frame_slice->offset, frame_slice->size);
      return status;
    }
    if (buffer_.has_wrapped_readable_data()) {
      const size_t linearized_size = buffer_.linearize(linearized_frame_);
      const ByteSpan linearized = ByteSpan(linearized_frame_.data(), linearized_size);
      status = framer_->try_frame(linearized, frame_slice);
      if (status == FrameStatus::kReady) {
        *frame = linearized.subspan(frame_slice->offset, frame_slice->size);
      }
    }
    return status;
  }

  ITransport* transport_ = nullptr;
  const IFramer* framer_ = nullptr;
  const ProtocolDecoder* decoder_ = nullptr;
  ByteRingBuffer<Capacity> buffer_;
  std::array<std::byte, Capacity> linearized_frame_{};
  bool end_of_stream_ = false;
  StreamRuntimeOptions options_;
  RuntimeStats stats_;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_RUNTIME__STREAM_RUNTIME_HPP_
