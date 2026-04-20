#include "universal_protocol_runtime/adapters/byte_stream_transport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace upr = universal_protocol_runtime;

namespace {

class ScriptedByteTransport final : public upr::IByteStreamTransport {
 public:
  struct WriteStep {
    size_t bytes_written = 0;
    bool would_block = false;
    upr::Status status = upr::Status::ok_status();
  };

  upr::ReadResult read(upr::MutableByteSpan) override { return {.end_of_stream = true}; }

  upr::WriteResult write(upr::ByteSpan source) override {
    writes_.push_back(source.size());
    if (steps_.empty()) {
      return {.bytes_written = source.size()};
    }
    const WriteStep step = steps_.front();
    steps_.pop_front();
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
  int native_handle() const override { return 42; }
  upr::StatusOr<bool> wait_until_readable(int) const override { return true; }
  upr::StatusOr<bool> wait_until_writable(int) const override { return true; }
  upr::TransportCapabilityMask capabilities() const override {
    return upr::capability_mask(upr::TransportCapability::kStream);
  }
  std::string local_endpoint() const override { return "scripted://local"; }
  std::string peer_endpoint() const override { return "scripted://peer"; }

  void push_step(const WriteStep& step) { steps_.push_back(step); }
  const std::vector<size_t>& writes() const { return writes_; }

 private:
  std::deque<WriteStep> steps_;
  std::vector<size_t> writes_;
  bool open_ = true;
};

class ScriptedListener final : public upr::IListenerTransport {
 public:
  upr::StatusOr<bool> wait_for_connection(int timeout_ms) const override { return timeout_ms >= 0; }
  upr::StatusOr<std::unique_ptr<upr::IByteStreamTransport>> accept() override {
    return std::unique_ptr<upr::IByteStreamTransport>(new ScriptedByteTransport());
  }
  upr::Status close() override {
    open_ = false;
    return upr::Status::ok_status();
  }
  int native_handle() const override { return 7; }
  bool is_open() const override { return open_; }
  std::string local_endpoint() const override { return "listener://local"; }

 private:
  bool open_ = true;
};

TEST(ByteStreamTransportTest, WritevHandlesSuccessPartialWouldBlockAndErrors) {
  ScriptedByteTransport transport;
  const std::array<std::byte, 2> first = {std::byte{'a'}, std::byte{'b'}};
  const std::array<std::byte, 3> second = {std::byte{'c'}, std::byte{'d'}, std::byte{'e'}};
  const std::array<upr::ByteSpan, 2> sources = {
      upr::ByteSpan(first.data(), first.size()),
      upr::ByteSpan(second.data(), second.size()),
  };

  upr::WriteResult full = transport.writev(sources);
  EXPECT_TRUE(full.status.ok());
  EXPECT_EQ(full.bytes_written, 5U);
  EXPECT_FALSE(full.would_block);

  transport.push_step({.bytes_written = 2});
  transport.push_step({.bytes_written = 1});
  upr::WriteResult partial = transport.writev(sources);
  EXPECT_TRUE(partial.status.ok());
  EXPECT_EQ(partial.bytes_written, 3U);
  EXPECT_TRUE(partial.would_block);

  transport.push_step({.bytes_written = 2});
  transport.push_step({.bytes_written = 1, .would_block = true});
  upr::WriteResult blocked = transport.writev(sources);
  EXPECT_TRUE(blocked.status.ok());
  EXPECT_EQ(blocked.bytes_written, 3U);
  EXPECT_TRUE(blocked.would_block);

  transport.push_step({.bytes_written = 0, .status = upr::io_error("write failed")});
  upr::WriteResult errored = transport.writev(sources);
  EXPECT_FALSE(errored.status.ok());
  EXPECT_EQ(errored.status.code(), upr::StatusCode::kIoError);
  EXPECT_EQ(errored.bytes_written, 0U);

  const std::array<upr::ByteSpan, 1> empty_sources = {
      upr::ByteSpan(),
  };
  upr::WriteResult empty = transport.writev(empty_sources);
  EXPECT_TRUE(empty.status.ok());
  EXPECT_EQ(empty.bytes_written, 0U);
}

TEST(ByteStreamTransportTest, DefaultHelpersExposeBaseBehavior) {
  ScriptedByteTransport transport;
  EXPECT_TRUE(transport.flush().ok());
  EXPECT_TRUE(transport.shutdown_read().ok());
  EXPECT_TRUE(transport.shutdown_write().ok());
  EXPECT_TRUE(transport.is_open());
  EXPECT_EQ(transport.native_handle(), 42);
  EXPECT_EQ(transport.local_endpoint(), "scripted://local");
  EXPECT_EQ(transport.peer_endpoint(), "scripted://peer");
  EXPECT_TRUE(transport.wait_until_readable(0).value());
  EXPECT_TRUE(transport.wait_until_writable(0).value());

  upr::StatusOr<upr::TransportBufferLease> lease = transport.try_acquire_receive_buffer();
  EXPECT_FALSE(lease.ok());
  EXPECT_EQ(lease.status().code(), upr::StatusCode::kNotFound);

  const upr::Status release = transport.release_receive_buffer({});
  EXPECT_FALSE(release.ok());
  EXPECT_EQ(release.code(), upr::StatusCode::kInvalidArgument);

  EXPECT_TRUE(transport.close().ok());
  EXPECT_FALSE(transport.is_open());
}

TEST(ByteStreamTransportTest, ListenerInterfaceCanBeExercisedViaConcreteImplementation) {
  ScriptedListener listener;
  ASSERT_TRUE(listener.wait_for_connection(0).ok());
  EXPECT_TRUE(listener.wait_for_connection(0).value());
  EXPECT_TRUE(listener.is_open());
  EXPECT_EQ(listener.native_handle(), 7);
  EXPECT_EQ(listener.local_endpoint(), "listener://local");

  upr::StatusOr<std::unique_ptr<upr::IByteStreamTransport>> accepted = listener.accept();
  ASSERT_TRUE(accepted.ok()) << accepted.status().message();
  ASSERT_NE(accepted.value(), nullptr);

  EXPECT_TRUE(listener.close().ok());
  EXPECT_FALSE(listener.is_open());
}

}  // namespace
