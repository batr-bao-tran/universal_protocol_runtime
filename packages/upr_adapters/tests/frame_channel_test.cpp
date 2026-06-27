#include "universal_protocol_runtime/adapters/frame_channel.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <deque>
#include <string_view>
#include <vector>

#include "universal_protocol_runtime/adapters/local_shm_ring_transport.hpp"
#include "universal_protocol_runtime/adapters/unix_socket_transport.hpp"

namespace upr = universal_protocol_runtime;

namespace {

class ScriptedTransport final : public upr::IByteStreamTransport {
 public:
  struct ReadStep {
    std::vector<std::byte> bytes;
    bool would_block = false;
    bool end_of_stream = false;
    upr::Status status = upr::Status::ok_status();
  };

  struct WriteStep {
    size_t bytes_written = 0;
    bool would_block = false;
    upr::Status status = upr::Status::ok_status();
  };

  explicit ScriptedTransport(
      upr::TransportCapabilityMask caps = upr::capability_mask(upr::TransportCapability::kStream))
      : capabilities_(caps) {}

  upr::ReadResult read(upr::MutableByteSpan destination) override {
    if (read_steps_.empty()) {
      return {.would_block = true};
    }
    const ReadStep step = read_steps_.front();
    read_steps_.pop_front();
    if (!step.status.ok()) {
      return {.status = step.status};
    }
    const size_t bytes_to_copy = std::min(destination.size(), step.bytes.size());
    std::copy_n(step.bytes.begin(), bytes_to_copy, destination.begin());
    return {
        .bytes_read = bytes_to_copy,
        .end_of_stream = step.end_of_stream,
        .would_block = step.would_block,
    };
  }

  upr::WriteResult write(upr::ByteSpan source) override {
    writes_.emplace_back(source.begin(), source.end());
    if (write_steps_.empty()) {
      return {.bytes_written = source.size()};
    }
    const WriteStep step = write_steps_.front();
    write_steps_.pop_front();
    return {
        .bytes_written = step.bytes_written,
        .would_block = step.would_block,
        .status = step.status,
    };
  }

  upr::WriteResult writev(std::span<const upr::ByteSpan> sources) override {
    if (writev_steps_.empty()) {
      return upr::IByteStreamTransport::writev(sources);
    }
    const WriteStep step = writev_steps_.front();
    writev_steps_.pop_front();
    return {
        .bytes_written = step.bytes_written,
        .would_block = step.would_block,
        .status = step.status,
    };
  }

  upr::Status close() override {
    open_ = false;
    return upr::Status::ok_status();
  }

  bool is_open() const override { return open_; }
  int native_handle() const override { return -1; }
  upr::StatusOr<bool> wait_until_readable(int) const override { return true; }

  upr::StatusOr<bool> wait_until_writable(int) const override {
    if (!wait_status_.ok()) {
      return wait_status_;
    }
    return wait_writable_value_;
  }

  upr::TransportCapabilityMask capabilities() const override { return capabilities_; }
  std::string local_endpoint() const override { return "scripted://local"; }
  std::string peer_endpoint() const override { return "scripted://peer"; }

  upr::StatusOr<upr::TransportBufferLease> try_acquire_receive_buffer() override {
    if (!lease_result_.ok()) {
      return lease_result_.status();
    }
    return lease_result_.value();
  }

  upr::Status release_receive_buffer(const upr::TransportBufferLease& lease) override {
    released_tokens_.push_back(lease.token);
    return release_status_;
  }

  void push_read_step(ReadStep step) { read_steps_.push_back(std::move(step)); }
  void push_write_step(const WriteStep& step) { write_steps_.push_back(step); }
  void push_writev_step(const WriteStep& step) { writev_steps_.push_back(step); }
  void set_wait_result(const upr::Status& status, bool value) {
    wait_status_ = std::move(status);
    wait_writable_value_ = value;
  }
  void set_lease_result(const upr::StatusOr<upr::TransportBufferLease>& result) { lease_result_ = std::move(result); }

  const std::vector<std::vector<std::byte>>& writes() const { return writes_; }
  const std::vector<uint64_t>& released_tokens() const { return released_tokens_; }

 private:
  std::deque<ReadStep> read_steps_;
  std::deque<WriteStep> write_steps_;
  std::deque<WriteStep> writev_steps_;
  upr::TransportCapabilityMask capabilities_;
  bool open_ = true;
  upr::Status wait_status_ = upr::Status::ok_status();
  bool wait_writable_value_ = true;
  upr::StatusOr<upr::TransportBufferLease> lease_result_ = upr::not_found("no lease");
  upr::Status release_status_ = upr::Status::ok_status();
  std::vector<std::vector<std::byte>> writes_;
  std::vector<uint64_t> released_tokens_;
};

TEST(FrameChannelTest, SendsAndReceivesLengthPrefixedFramesOverStreamTransport) {
  auto pair = upr::UnixSocketTransport::create_socket_pair();
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  upr::FrameChannel sender(pair.value().first, {.prefix_width_bytes = 2, .max_frame_size = 1024});
  upr::FrameChannel receiver(pair.value().second, {.prefix_width_bytes = 2, .max_frame_size = 1024});

  constexpr std::string_view kPayload = "frame-data";
  ASSERT_TRUE(
      sender.send_frame(upr::ByteSpan(reinterpret_cast<const std::byte*>(kPayload.data()), kPayload.size())).ok());

  std::vector<std::byte> frame;
  const upr::FrameChannelPollResult result = receiver.receive_frame(&frame);
  ASSERT_EQ(result.status, upr::FrameChannelPollStatus::kFrameReady);
  ASSERT_EQ(frame.size(), kPayload.size());
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(frame.data()), frame.size()), kPayload);
}

TEST(FrameChannelTest, UsesZeroCopyLeasesForBoundaryPreservingTransport) {
  auto pair = upr::LocalShmRingTransport::create_pair({.slot_count = 8, .slot_size = 256});
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  upr::FrameChannel sender(pair.value().first);
  upr::FrameChannel receiver(pair.value().second);

  constexpr std::array<std::byte, 4> kPayload = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44}};
  ASSERT_TRUE(sender.send_frame(upr::ByteSpan(kPayload.data(), kPayload.size())).ok());

  upr::StatusOr<upr::TransportBufferLease> lease = receiver.try_acquire_frame();
  ASSERT_TRUE(lease.ok()) << lease.status().message();
  ASSERT_TRUE(lease.value().valid);
  ASSERT_EQ(lease.value().bytes.size(), kPayload.size());
  EXPECT_EQ(lease.value().bytes[2], std::byte{0x33});
  EXPECT_TRUE(receiver.release_frame(lease.value()).ok());
}

TEST(FrameChannelTest, RejectsFramesLargerThanConfiguredMaximum) {
  auto pair = upr::UnixSocketTransport::create_socket_pair();
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  upr::FrameChannel sender(pair.value().first, {.prefix_width_bytes = 4, .max_frame_size = 4});
  const std::array<std::byte, 8> payload = {
      std::byte{0},
      std::byte{1},
      std::byte{2},
      std::byte{3},
      std::byte{4},
      std::byte{5},
      std::byte{6},
      std::byte{7},
  };
  const upr::Status status = sender.send_frame(upr::ByteSpan(payload.data(), payload.size()));
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), upr::StatusCode::kExhausted);
}

TEST(FrameChannelTest, SendFramesWritesPrefixAndPayloadForEachFrame) {
  ScriptedTransport transport;
  upr::FrameChannel channel(transport, {.prefix_width_bytes = 1, .max_frame_size = 16});
  const std::array<std::byte, 2> first = {std::byte{'a'}, std::byte{'b'}};
  const std::array<std::byte, 1> second = {std::byte{'z'}};
  const std::array<upr::ByteSpan, 2> frames = {
      upr::ByteSpan(first.data(), first.size()),
      upr::ByteSpan(second.data(), second.size()),
  };

  const upr::Status status = channel.send_frames(frames);
  ASSERT_TRUE(status.ok()) << status.message();
  ASSERT_EQ(transport.writes().size(), 4U);
  EXPECT_EQ(transport.writes()[0][0], std::byte{2});
  EXPECT_EQ(transport.writes()[1][1], std::byte{'b'});
  EXPECT_EQ(transport.writes()[2][0], std::byte{1});
}

TEST(FrameChannelTest, ReceiveFrameReturnsNeedMoreDataForWouldBlockAndEndOfStream) {
  ScriptedTransport transport;
  transport.push_read_step({.bytes = {}, .would_block = true});
  upr::FrameChannel channel(transport, {.prefix_width_bytes = 2, .max_frame_size = 16});

  std::vector<std::byte> frame;
  upr::FrameChannelPollResult blocked = channel.receive_frame(&frame);
  EXPECT_EQ(blocked.status, upr::FrameChannelPollStatus::kNeedMoreData);

  transport.push_read_step({.bytes = {}, .end_of_stream = true});
  upr::FrameChannelPollResult eos = channel.receive_frame(&frame);
  EXPECT_EQ(eos.status, upr::FrameChannelPollStatus::kNeedMoreData);
}

TEST(FrameChannelTest, ReceiveFrameReturnsTransportErrorForOversizedOrFailedInput) {
  ScriptedTransport oversized;
  oversized.push_read_step({.bytes = {std::byte{0x08}, std::byte{0x00}}});
  upr::FrameChannel oversized_channel(oversized, {.prefix_width_bytes = 2, .max_frame_size = 4});

  std::vector<std::byte> frame;
  upr::FrameChannelPollResult oversized_result = oversized_channel.receive_frame(&frame);
  EXPECT_EQ(oversized_result.status, upr::FrameChannelPollStatus::kTransportError);
  EXPECT_EQ(oversized_result.transport_status.code(), upr::StatusCode::kExhausted);

  ScriptedTransport broken;
  broken.push_read_step({.bytes = {}, .status = upr::io_error("boom")});
  upr::FrameChannel broken_channel(broken);
  upr::FrameChannelPollResult broken_result = broken_channel.receive_frame(&frame);
  EXPECT_EQ(broken_result.status, upr::FrameChannelPollStatus::kTransportError);
  EXPECT_EQ(broken_result.transport_status.code(), upr::StatusCode::kIoError);
}

TEST(FrameChannelTest, SendFrameHandlesPartialWritesTimeoutsAndZeroProgressErrors) {
  {
    ScriptedTransport timeout_transport;
    timeout_transport.push_write_step({.bytes_written = 1, .would_block = false});
    timeout_transport.push_write_step({.bytes_written = 1, .would_block = true});
    timeout_transport.set_wait_result(upr::Status::ok_status(), false);
    upr::FrameChannel channel(timeout_transport, {.prefix_width_bytes = 1, .max_frame_size = 8});
    const std::array<std::byte, 2> payload = {std::byte{'o'}, std::byte{'k'}};
    const upr::Status status = channel.send_frame(upr::ByteSpan(payload.data(), payload.size()));
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), upr::StatusCode::kExhausted);
  }
  {
    ScriptedTransport zero_progress_transport;
    zero_progress_transport.push_writev_step({.bytes_written = 0, .would_block = false});
    upr::FrameChannel channel(zero_progress_transport, {.prefix_width_bytes = 1, .max_frame_size = 8});
    const std::array<std::byte, 2> payload = {std::byte{'o'}, std::byte{'k'}};
    const upr::Status status = channel.send_frame(upr::ByteSpan(payload.data(), payload.size()));
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), upr::StatusCode::kIoError);
  }
  {
    ScriptedTransport wait_error_transport;
    wait_error_transport.push_write_step({.bytes_written = 1, .would_block = true});
    wait_error_transport.set_wait_result(upr::io_error("wait failed"), false);
    upr::FrameChannel channel(wait_error_transport, {.prefix_width_bytes = 2, .max_frame_size = 8});
    const std::array<std::byte, 2> payload = {std::byte{'o'}, std::byte{'k'}};
    const upr::Status status = channel.send_frame(upr::ByteSpan(payload.data(), payload.size()));
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), upr::StatusCode::kIoError);
  }
}

TEST(FrameChannelTest, BoundaryPreservingSendFramePropagatesWriteFailures) {
  constexpr auto kBoundaryCaps = upr::capability_mask(upr::TransportCapability::kPreservesFrameBoundaries);
  const std::array<std::byte, 2> payload = {std::byte{'o'}, std::byte{'k'}};

  {
    ScriptedTransport transport(kBoundaryCaps);
    transport.push_write_step({.bytes_written = 0, .status = upr::io_error("write failed")});
    upr::FrameChannel channel(transport);

    const upr::Status status = channel.send_frame(upr::ByteSpan(payload.data(), payload.size()));

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), upr::StatusCode::kIoError);
  }
  {
    ScriptedTransport transport(kBoundaryCaps);
    transport.push_write_step({.bytes_written = 0, .would_block = false});
    upr::FrameChannel channel(transport);

    const upr::Status status = channel.send_frame(upr::ByteSpan(payload.data(), payload.size()));

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), upr::StatusCode::kIoError);
  }
  {
    ScriptedTransport transport(kBoundaryCaps);
    transport.push_write_step({.bytes_written = 1, .would_block = true});
    transport.set_wait_result(upr::Status::ok_status(), false);
    upr::FrameChannel channel(transport);

    const upr::Status status = channel.send_frame(upr::ByteSpan(payload.data(), payload.size()));

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), upr::StatusCode::kExhausted);
  }
  {
    ScriptedTransport transport(kBoundaryCaps);
    transport.push_write_step({.bytes_written = 1, .would_block = true});
    transport.set_wait_result(upr::io_error("wait failed"), false);
    upr::FrameChannel channel(transport);

    const upr::Status status = channel.send_frame(upr::ByteSpan(payload.data(), payload.size()));

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), upr::StatusCode::kIoError);
  }
}

TEST(FrameChannelTest, StreamSendFrameHandlesEmptyPayloadSpan) {
  ScriptedTransport transport;
  transport.push_writev_step({.bytes_written = 1});
  upr::FrameChannel channel(transport, {.prefix_width_bytes = 1, .max_frame_size = 8});

  const upr::Status status = channel.send_frame(upr::ByteSpan{});

  EXPECT_TRUE(status.ok()) << status.message();
}

TEST(FrameChannelTest, BoundaryPreservingPathsPropagateLeaseSemantics) {
  ScriptedTransport transport(upr::capability_mask(upr::TransportCapability::kPreservesFrameBoundaries));
  std::array<std::byte, 3> payload = {std::byte{'x'}, std::byte{'y'}, std::byte{'z'}};
  transport.set_lease_result(upr::TransportBufferLease{
      .bytes = upr::ByteSpan(payload.data(), payload.size()),
      .token = 17,
      .valid = true,
  });
  upr::FrameChannel channel(transport);

  std::vector<std::byte> frame;
  upr::FrameChannelPollResult result = channel.receive_frame(&frame);
  ASSERT_EQ(result.status, upr::FrameChannelPollStatus::kFrameReady);
  EXPECT_EQ(frame.size(), 3U);
  ASSERT_EQ(transport.released_tokens().size(), 1U);
  EXPECT_EQ(transport.released_tokens()[0], 17U);

  const upr::Status release_status = channel.release_frame({.valid = false});
  EXPECT_TRUE(release_status.ok());
}

TEST(FrameChannelTest, ReleaseFrameRejectsInvalidLeaseForStreamTransport) {
  ScriptedTransport transport;
  upr::FrameChannel channel(transport);
  const upr::Status status = channel.release_frame({.valid = false});
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), upr::StatusCode::kInvalidArgument);
}

TEST(FrameChannelTest, SendFramesStopsAtFirstFailureAndReceiveHandlesInvalidLeases) {
  ScriptedTransport send_transport;
  send_transport.push_write_step({.bytes_written = 1});
  send_transport.push_write_step({.bytes_written = 2});
  send_transport.push_write_step({.bytes_written = 0, .status = upr::io_error("boom")});
  upr::FrameChannel send_channel(send_transport, {.prefix_width_bytes = 1, .max_frame_size = 8});
  const std::array<std::byte, 2> payload = {std::byte{'o'}, std::byte{'k'}};
  const std::array<upr::ByteSpan, 2> frames = {
      upr::ByteSpan(payload.data(), payload.size()),
      upr::ByteSpan(payload.data(), payload.size()),
  };
  const upr::Status send_status = send_channel.send_frames(frames);
  EXPECT_FALSE(send_status.ok());
  EXPECT_EQ(send_status.code(), upr::StatusCode::kIoError);

  ScriptedTransport invalid_lease_transport(upr::capability_mask(upr::TransportCapability::kPreservesFrameBoundaries));
  invalid_lease_transport.set_lease_result(upr::TransportBufferLease{.valid = false});
  upr::FrameChannel invalid_lease_channel(invalid_lease_transport);
  std::vector<std::byte> frame;
  upr::FrameChannelPollResult invalid_lease_result = invalid_lease_channel.receive_frame(&frame);
  EXPECT_EQ(invalid_lease_result.status, upr::FrameChannelPollStatus::kNeedMoreData);
}

TEST(FrameChannelTest, StreamReceivePathHandlesWouldBlockEndOfStreamAndCompaction) {
  ScriptedTransport transport;
  transport.push_read_step({.bytes = {std::byte{2}}});
  transport.push_read_step({.bytes = {}, .would_block = true});
  upr::FrameChannel channel(transport, {.prefix_width_bytes = 1, .max_frame_size = 8});

  std::vector<std::byte> frame;
  upr::FrameChannelPollResult blocked = channel.receive_frame(&frame);
  EXPECT_EQ(blocked.status, upr::FrameChannelPollStatus::kNeedMoreData);

  transport.push_read_step({.bytes = {std::byte{'a'}, std::byte{'b'}}});
  upr::FrameChannelPollResult ready = channel.receive_frame(&frame);
  ASSERT_EQ(ready.status, upr::FrameChannelPollStatus::kFrameReady);
  EXPECT_EQ(frame.size(), 2U);

  transport.push_read_step({.bytes = {std::byte{1}, std::byte{'z'}}});
  upr::FrameChannelPollResult compacted = channel.receive_frame(&frame);
  ASSERT_EQ(compacted.status, upr::FrameChannelPollStatus::kFrameReady);
  EXPECT_EQ(frame.size(), 1U);
  EXPECT_EQ(frame[0], std::byte{'z'});

  transport.push_read_step({.bytes = {std::byte{1}}});
  transport.push_read_step({.bytes = {}, .end_of_stream = true});
  upr::StatusOr<upr::TransportBufferLease> lease = channel.try_acquire_frame();
  EXPECT_FALSE(lease.ok());
  EXPECT_EQ(lease.status().code(), upr::StatusCode::kNotFound);
}

TEST(FrameChannelTest, StreamReceiveTreatsEmptyReadWithoutFlagsAsNeedMoreData) {
  ScriptedTransport transport;
  transport.push_read_step({.bytes = {}});
  upr::FrameChannel channel(transport, {.prefix_width_bytes = 1, .max_frame_size = 8});

  std::vector<std::byte> frame;
  const upr::FrameChannelPollResult result = channel.receive_frame(&frame);

  EXPECT_EQ(result.status, upr::FrameChannelPollStatus::kNeedMoreData);
}

TEST(FrameChannelTest, StreamReceiveCompactsWhilePreservingBufferedTail) {
  ScriptedTransport transport;
  std::vector<std::byte> combined = {
      std::byte{1},
      std::byte{'a'},
      std::byte{6},
      std::byte{'b'},
      std::byte{'c'},
      std::byte{'d'},
      std::byte{'e'},
      std::byte{'f'},
      std::byte{'g'},
  };
  transport.push_read_step({.bytes = combined});
  upr::FrameChannel channel(transport, {.prefix_width_bytes = 1, .max_frame_size = 8});

  std::vector<std::byte> frame;
  upr::FrameChannelPollResult first = channel.receive_frame(&frame);
  ASSERT_EQ(first.status, upr::FrameChannelPollStatus::kFrameReady);
  ASSERT_EQ(frame.size(), 1U);
  EXPECT_EQ(frame[0], std::byte{'a'});

  upr::FrameChannelPollResult second = channel.receive_frame(&frame);
  ASSERT_EQ(second.status, upr::FrameChannelPollStatus::kFrameReady);
  ASSERT_EQ(frame.size(), 6U);
  EXPECT_EQ(frame[0], std::byte{'b'});
}

}  // namespace
