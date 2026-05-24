#include "universal_protocol_runtime/adapters/unix_socket_transport.hpp"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

namespace upr = universal_protocol_runtime;

namespace {

inline constexpr int kSocketTimeoutMs = 3000;

TEST(UnixSocketTransportTest, SocketPairSupportsReadWriteAndReadiness) {
  auto pair = upr::UnixSocketTransport::create_socket_pair();
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  const std::array<std::byte, 3> payload = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
  ASSERT_TRUE(pair.value().first.write(upr::ByteSpan(payload.data(), payload.size())).status.ok());

  auto readable = pair.value().second.wait_until_readable(1000);
  ASSERT_TRUE(readable.ok()) << readable.status().message();
  EXPECT_TRUE(readable.value());

  std::array<std::byte, 3> buffer{};
  const upr::ReadResult read_result = pair.value().second.read(buffer);
  ASSERT_TRUE(read_result.status.ok()) << read_result.status.message();
  EXPECT_EQ(read_result.bytes_read, 3U);
  EXPECT_EQ(buffer[2], std::byte{0xCC});
}

TEST(UnixSocketTransportTest, SocketPairSupportsVectoredWrite) {
  auto pair = upr::UnixSocketTransport::create_socket_pair();
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  const std::array<std::byte, 2> first = {std::byte{'O'}, std::byte{'K'}};
  const std::array<std::byte, 3> second = {std::byte{'!'}, std::byte{'!'}, std::byte{'\n'}};
  const std::array<upr::ByteSpan, 2> spans = {
      upr::ByteSpan(first.data(), first.size()),
      upr::ByteSpan(second.data(), second.size()),
  };

  const upr::WriteResult write_result = pair.value().first.writev(spans);
  ASSERT_TRUE(write_result.status.ok()) << write_result.status.message();
  ASSERT_EQ(write_result.bytes_written, 5U);
  EXPECT_FALSE(write_result.would_block);

  std::array<std::byte, 5> buffer{};
  const upr::ReadResult read_result = pair.value().second.read(buffer);
  ASSERT_TRUE(read_result.status.ok()) << read_result.status.message();
  ASSERT_EQ(read_result.bytes_read, 5U);
  EXPECT_EQ(buffer[0], std::byte{'O'});
  EXPECT_EQ(buffer[2], std::byte{'!'});
  EXPECT_EQ(buffer[4], std::byte{'\n'});
}

TEST(UnixSocketTransportTest, ListenerAcceptsConnectionsOnFilesystemPath) {
  const std::filesystem::path socket_path =
      std::filesystem::temp_directory_path() / "upr_unix_socket_transport_test.sock";
  auto listener = upr::UnixSocketListener::bind_path(socket_path.string());
  ASSERT_TRUE(listener.ok()) << listener.status().message();

  std::thread client_thread([&]() {
    auto client = upr::UnixSocketTransport::connect_to_path(socket_path.string());
    ASSERT_TRUE(client.ok()) << client.status().message();
    const char byte = 'x';
    ASSERT_TRUE(client.value().write(upr::ByteSpan(reinterpret_cast<const std::byte*>(&byte), 1)).status.ok());
  });

  auto ready = listener.value().wait_for_connection(1000);
  ASSERT_TRUE(ready.ok()) << ready.status().message();
  EXPECT_TRUE(ready.value());

  auto accepted = listener.value().accept();
  ASSERT_TRUE(accepted.ok()) << accepted.status().message();

  const auto readable = accepted.value()->wait_until_readable(kSocketTimeoutMs);
  ASSERT_TRUE(readable.ok()) << readable.status().message();
  ASSERT_TRUE(readable.value());
  std::array<std::byte, 1> buffer{};
  const upr::ReadResult read_result = accepted.value()->read(buffer);
  ASSERT_TRUE(read_result.status.ok()) << read_result.status.message();
  EXPECT_EQ(read_result.bytes_read, 1U);
  EXPECT_EQ(buffer[0], std::byte{'x'});

  client_thread.join();
  EXPECT_TRUE(listener.value().close().ok());
}

TEST(UnixSocketTransportTest, ReportsClosedAndMissingSocketFailuresDefensively) {
  upr::UnixSocketTransport closed;
  EXPECT_FALSE(closed.is_open());
  EXPECT_TRUE(closed.close().ok());
  const auto wait_read = closed.wait_until_readable(0);
  EXPECT_FALSE(wait_read.ok());
  EXPECT_EQ(wait_read.status().code(), upr::StatusCode::kInvalidArgument);
  const auto wait_write = closed.wait_until_writable(0);
  EXPECT_FALSE(wait_write.ok());
  EXPECT_EQ(wait_write.status().code(), upr::StatusCode::kInvalidArgument);
  const char byte = 'q';
  const upr::WriteResult write_result = closed.write(upr::ByteSpan(reinterpret_cast<const std::byte*>(&byte), 1));
  EXPECT_FALSE(write_result.status.ok());
  EXPECT_EQ(write_result.status.code(), upr::StatusCode::kIoError);

  const std::filesystem::path missing_path = std::filesystem::temp_directory_path() / "upr_missing_unix_socket.sock";
  auto connect_result = upr::UnixSocketTransport::connect_to_path(missing_path.string());
  EXPECT_FALSE(connect_result.ok());
}

TEST(UnixSocketTransportTest, ListenerSupportsWouldBlockAndMoveClosePaths) {
  const std::filesystem::path socket_path =
      std::filesystem::temp_directory_path() / "upr_unix_socket_transport_test_would_block.sock";
  auto listener = upr::UnixSocketListener::bind_path(socket_path.string());
  ASSERT_TRUE(listener.ok()) << listener.status().message();

  auto ready = listener.value().wait_for_connection(0);
  ASSERT_TRUE(ready.ok()) << ready.status().message();
  EXPECT_FALSE(ready.value());

  auto no_connection = listener.value().accept();
  EXPECT_FALSE(no_connection.ok());
  EXPECT_EQ(no_connection.status().code(), upr::StatusCode::kNotFound);

  upr::UnixSocketListener moved = std::move(listener.value());
  EXPECT_TRUE(moved.is_open());
  EXPECT_EQ(moved.local_endpoint(), socket_path.string());
  EXPECT_GE(moved.native_handle(), 0);
  EXPECT_TRUE(moved.close().ok());
  EXPECT_FALSE(moved.is_open());
}

TEST(UnixSocketTransportTest, SupportsOwnershipEndpointsAndMoveAssignment) {
  auto pair = upr::UnixSocketTransport::create_socket_pair({.non_blocking = false});
  ASSERT_TRUE(pair.ok()) << pair.status().message();
  EXPECT_TRUE(pair.value().first.is_open());
  EXPECT_EQ(pair.value().first.capabilities(), upr::capability_mask(upr::TransportCapability::kStream));
  EXPECT_GE(pair.value().first.native_handle(), 0);
  EXPECT_EQ(pair.value().first.local_endpoint(), "");
  EXPECT_EQ(pair.value().first.peer_endpoint(), "");

  upr::UnixSocketTransport moved;
  moved = std::move(pair.value().first);
  EXPECT_TRUE(moved.is_open());
  EXPECT_TRUE(moved.close().ok());
  EXPECT_FALSE(moved.is_open());
}

TEST(UnixSocketTransportTest, ConstructorHandlesInvalidDescriptorsAndNonOwnedHandles) {
  std::array<int, 2> sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0);

  upr::UnixSocketTransport borrowed(sockets[0], {.own_handle = false});
  ASSERT_TRUE(borrowed.is_open());
  EXPECT_TRUE(borrowed.close().ok());
  EXPECT_FALSE(borrowed.is_open());

  const char payload_byte = 'z';
  EXPECT_EQ(::send(sockets[0], &payload_byte, 1, MSG_NOSIGNAL), 1);
  std::array<char, 1> recv_buffer{};
  EXPECT_EQ(::recv(sockets[1], recv_buffer.data(), recv_buffer.size(), 0), 1);

  EXPECT_EQ(::close(sockets[0]), 0);
  EXPECT_EQ(::close(sockets[1]), 0);

  int stale_socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(stale_socket, 0);
  ASSERT_EQ(::close(stale_socket), 0);
  upr::UnixSocketTransport invalid(stale_socket);
  EXPECT_FALSE(invalid.is_open());
  EXPECT_TRUE(invalid.close().ok());

  std::array<std::byte, 1> read_buffer{};
  upr::ReadResult closed_read = invalid.read(read_buffer);
  EXPECT_TRUE(closed_read.end_of_stream);
}

}  // namespace
