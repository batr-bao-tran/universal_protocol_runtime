#include "universal_protocol_runtime/adapters/io_uring_reactor.hpp"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>

#include "universal_protocol_runtime/adapters/pluggable_stream_engine.hpp"

namespace upr = universal_protocol_runtime;

namespace {

TEST(IoUringReactorTest, ReportsAvailabilityConsistently) {
  if (upr::IoUringReactor::is_supported()) {
    EXPECT_EQ(upr::IoUringReactor::availability(), upr::IoUringAvailability::kAvailable);
    EXPECT_EQ(upr::IoUringReactor::reason(), "io_uring is available.");
  } else {
    EXPECT_EQ(upr::IoUringReactor::availability(), upr::IoUringAvailability::kUnavailable);
    EXPECT_EQ(upr::IoUringReactor::reason(), "io_uring is unavailable on this kernel or process.");
  }
}

TEST(IoUringReactorTest, RejectsInvalidDescriptorsAndClosedOperations) {
  auto invalid = upr::IoUringStreamEngine::create(-1, "uring://local", "uring://peer");
  EXPECT_FALSE(invalid.ok());
  EXPECT_EQ(invalid.status().code(), upr::StatusCode::kInvalidArgument);

  if (!upr::IoUringReactor::is_supported()) {
    GTEST_SKIP() << upr::IoUringReactor::reason();
  }

  std::array<int, 2> sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0);
  auto engine = upr::IoUringStreamEngine::create(sockets[0], "uring://local", "uring://peer");
  ASSERT_TRUE(engine.ok()) << engine.status().message();
  upr::PluggableStreamTransport transport(std::move(engine.value()));
  EXPECT_TRUE(transport.close().ok());
  EXPECT_FALSE(transport.is_open());
  EXPECT_FALSE(transport.wait_until_readable(0).ok());
  EXPECT_FALSE(transport.wait_until_writable(0).ok());
  EXPECT_TRUE(transport.shutdown_read().ok());
  EXPECT_TRUE(transport.shutdown_write().ok());
  upr::WriteResult closed_write = transport.write(upr::ByteSpan());
  EXPECT_FALSE(closed_write.status.ok());
  upr::WriteResult closed_writev = transport.writev(std::span<const upr::ByteSpan>());
  EXPECT_FALSE(closed_writev.status.ok());
  EXPECT_EQ(::close(sockets[1]), 0);
}

TEST(IoUringReactorTest, TransfersBytesOverSocketPairWhenSupported) {
  if (!upr::IoUringReactor::is_supported()) {
    GTEST_SKIP() << upr::IoUringReactor::reason();
  }

  std::array<int, 2> sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0);

  auto left_engine = upr::IoUringStreamEngine::create(sockets[0], "uring://left", "uring://right");
  ASSERT_TRUE(left_engine.ok()) << left_engine.status().message();
  auto right_engine = upr::IoUringStreamEngine::create(sockets[1], "uring://right", "uring://left");
  ASSERT_TRUE(right_engine.ok()) << right_engine.status().message();

  upr::PluggableStreamTransport left(std::move(left_engine.value()));
  upr::PluggableStreamTransport right(std::move(right_engine.value()));

  const std::array<std::byte, 4> payload = {std::byte{'p'}, std::byte{'i'}, std::byte{'n'}, std::byte{'g'}};
  const upr::WriteResult write_result = left.write(upr::ByteSpan(payload.data(), payload.size()));
  ASSERT_TRUE(write_result.status.ok()) << write_result.status.message();
  ASSERT_EQ(write_result.bytes_written, payload.size());

  const std::array<upr::ByteSpan, 2> vectored_payload = {
      upr::ByteSpan(payload.data(), 2),
      upr::ByteSpan(payload.data() + 2, 2),
  };
  const upr::WriteResult writev_result = left.writev(vectored_payload);
  ASSERT_TRUE(writev_result.status.ok()) << writev_result.status.message();
  ASSERT_EQ(writev_result.bytes_written, payload.size());

  std::array<std::byte, 4> received{};
  const upr::ReadResult read_result = right.read(received);
  ASSERT_TRUE(read_result.status.ok()) << read_result.status.message();
  ASSERT_EQ(read_result.bytes_read, payload.size());
  EXPECT_EQ(received[3], std::byte{'g'});
  EXPECT_TRUE(upr::has_capability(left.capabilities(), upr::TransportCapability::kKernelBatching));

  std::array<std::byte, 4> vectored_received{};
  const upr::ReadResult vectored_read = right.read(vectored_received);
  ASSERT_TRUE(vectored_read.status.ok()) << vectored_read.status.message();
  ASSERT_EQ(vectored_read.bytes_read, payload.size());
  EXPECT_EQ(vectored_received[0], std::byte{'p'});
}

TEST(IoUringReactorTest, ManagesPollLifecycleAcrossTimeoutsAndReadyTransitions) {
  if (!upr::IoUringReactor::is_supported()) {
    GTEST_SKIP() << upr::IoUringReactor::reason();
  }

  std::array<int, 2> sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0);

  auto left_engine = upr::IoUringStreamEngine::create(sockets[0], "uring://left", "uring://right");
  ASSERT_TRUE(left_engine.ok()) << left_engine.status().message();
  auto right_engine = upr::IoUringStreamEngine::create(sockets[1], "uring://right", "uring://left");
  ASSERT_TRUE(right_engine.ok()) << right_engine.status().message();

  upr::PluggableStreamTransport left(std::move(left_engine.value()));
  upr::PluggableStreamTransport right(std::move(right_engine.value()));

  auto first_timeout = right.wait_until_readable(0);
  ASSERT_TRUE(first_timeout.ok()) << first_timeout.status().message();
  EXPECT_FALSE(first_timeout.value());

  auto second_timeout = right.wait_until_readable(0);
  ASSERT_TRUE(second_timeout.ok()) << second_timeout.status().message();
  EXPECT_FALSE(second_timeout.value());

  auto writable = left.wait_until_writable(0);
  ASSERT_TRUE(writable.ok()) << writable.status().message();
  EXPECT_TRUE(writable.value());

  const std::array<std::byte, 3> payload = {std::byte{'o'}, std::byte{'k'}, std::byte{'!'}};
  const upr::WriteResult write_result = left.write(upr::ByteSpan(payload.data(), payload.size()));
  ASSERT_TRUE(write_result.status.ok()) << write_result.status.message();
  ASSERT_EQ(write_result.bytes_written, payload.size());

  auto readable = right.wait_until_readable(1000);
  ASSERT_TRUE(readable.ok()) << readable.status().message();
  EXPECT_TRUE(readable.value());

  std::array<std::byte, 3> received{};
  const upr::ReadResult read_result = right.read(received);
  ASSERT_TRUE(read_result.status.ok()) << read_result.status.message();
  EXPECT_EQ(read_result.bytes_read, payload.size());
  EXPECT_EQ(received[1], std::byte{'k'});

  auto post_read_timeout = right.wait_until_readable(0);
  ASSERT_TRUE(post_read_timeout.ok()) << post_read_timeout.status().message();
  EXPECT_FALSE(post_read_timeout.value());
}

TEST(IoUringReactorTest, SupportsMoveSemanticsShutdownAndOwnershipModes) {
  if (!upr::IoUringReactor::is_supported()) {
    GTEST_SKIP() << upr::IoUringReactor::reason();
  }

  std::array<int, 2> sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0);

  upr::IoUringOptions borrowed_options;
  borrowed_options.own_handle = false;
  auto borrowed =
      upr::IoUringStreamEngine::create(sockets[0], "uring://borrowed-local", "uring://borrowed-peer", borrowed_options);
  ASSERT_TRUE(borrowed.ok()) << borrowed.status().message();
  auto owned = upr::IoUringStreamEngine::create(sockets[1], "uring://owned-local", "uring://owned-peer");
  ASSERT_TRUE(owned.ok()) << owned.status().message();

  upr::PluggableStreamTransport left(std::move(borrowed.value()));
  upr::PluggableStreamTransport right(std::move(owned.value()));
  EXPECT_EQ(left.local_endpoint(), "uring://borrowed-local");
  EXPECT_EQ(left.peer_endpoint(), "uring://borrowed-peer");
  EXPECT_NE(left.native_handle(), -1);

  upr::PluggableStreamTransport moved(std::move(left));
  EXPECT_TRUE(moved.is_open());
  EXPECT_TRUE(upr::has_capability(moved.capabilities(), upr::TransportCapability::kKernelBatching));
  EXPECT_TRUE(moved.shutdown_read().ok());
  EXPECT_TRUE(right.shutdown_write().ok());
  EXPECT_TRUE(moved.close().ok());

  const char byte = 'k';
  EXPECT_EQ(::send(sockets[0], &byte, 1, MSG_NOSIGNAL), 1);
  std::array<char, 1> buffer{};
  EXPECT_EQ(::recv(sockets[1], buffer.data(), buffer.size(), 0), 1);

  EXPECT_EQ(::close(sockets[0]), 0);
  EXPECT_TRUE(right.close().ok());
}

TEST(IoUringReactorTest, CoversWouldBlockEndOfStreamAndZeroCopySendCapabilities) {
  if (!upr::IoUringReactor::is_supported()) {
    GTEST_SKIP() << upr::IoUringReactor::reason();
  }

  upr::IoUringOptions left_options;
  left_options.use_send_zerocopy = true;
  left_options.send_zerocopy_threshold_bytes = 1;
  std::array<int, 2> capability_sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, capability_sockets.data()), 0);
  auto left_engine =
      upr::IoUringStreamEngine::create(capability_sockets[0], "uring://left", "uring://right", left_options);
  ASSERT_TRUE(left_engine.ok()) << left_engine.status().message();
  auto right_engine = upr::IoUringStreamEngine::create(capability_sockets[1], "uring://right", "uring://left");
  ASSERT_TRUE(right_engine.ok()) << right_engine.status().message();

  upr::PluggableStreamTransport left(std::move(left_engine.value()));
  upr::PluggableStreamTransport right(std::move(right_engine.value()));
  EXPECT_TRUE(upr::has_capability(left.capabilities(), upr::TransportCapability::kZeroCopySend));
  EXPECT_TRUE(left.close().ok());
  EXPECT_TRUE(right.close().ok());

  std::array<int, 2> sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0);
  auto plain_left_engine = upr::IoUringStreamEngine::create(sockets[0], "uring://left", "uring://right");
  ASSERT_TRUE(plain_left_engine.ok()) << plain_left_engine.status().message();
  auto plain_right_engine = upr::IoUringStreamEngine::create(sockets[1], "uring://right", "uring://left");
  ASSERT_TRUE(plain_right_engine.ok()) << plain_right_engine.status().message();
  upr::PluggableStreamTransport plain_left(std::move(plain_left_engine.value()));
  upr::PluggableStreamTransport plain_right(std::move(plain_right_engine.value()));

  const upr::WriteResult empty_writev = plain_left.writev(std::span<const upr::ByteSpan>());
  EXPECT_TRUE(empty_writev.status.ok());
  EXPECT_EQ(empty_writev.bytes_written, 0U);

  const std::array<std::byte, 3> payload = {std::byte{'z'}, std::byte{'c'}, std::byte{'!'}};
  EXPECT_TRUE(plain_right.close().ok());
  const upr::WriteResult closed_peer_write = plain_left.write(upr::ByteSpan(payload.data(), payload.size()));
  EXPECT_FALSE(closed_peer_write.status.ok());
  EXPECT_TRUE(plain_left.close().ok());
}

}  // namespace
