#include "packages/upr_adapters/src/adapters/stream_transport_support.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <filesystem>

namespace support = universal_protocol_runtime::stream_transport_support;
namespace upr = universal_protocol_runtime;

namespace {

TEST(StreamTransportSupportTest, ConfiguresWaitsAndClosesDescriptors) {
  std::array<int, 2> pipe_fds = {-1, -1};
  ASSERT_EQ(::pipe(pipe_fds.data()), 0);

  ASSERT_TRUE(support::set_non_blocking(pipe_fds[0]).ok());
  ASSERT_TRUE(support::set_non_blocking(pipe_fds[1]).ok());

  auto invalid_wait = support::wait_for_fd(-1, POLLIN, 0);
  EXPECT_FALSE(invalid_wait.ok());
  EXPECT_EQ(invalid_wait.status().code(), upr::StatusCode::kInvalidArgument);

  auto timeout_wait = support::wait_for_fd(pipe_fds[0], POLLIN, 0);
  ASSERT_TRUE(timeout_wait.ok()) << timeout_wait.status().message();
  EXPECT_FALSE(timeout_wait.value());

  constexpr char kByte = 'x';
  ASSERT_EQ(::write(pipe_fds[1], &kByte, 1), 1);
  auto ready_wait = support::wait_for_fd(pipe_fds[0], POLLIN, 0);
  ASSERT_TRUE(ready_wait.ok()) << ready_wait.status().message();
  EXPECT_TRUE(ready_wait.value());

  EXPECT_TRUE(support::close_fd(pipe_fds[0]).ok());
  pipe_fds[0] = -1;
  EXPECT_TRUE(support::close_fd(-1).ok());
  EXPECT_TRUE(support::close_fd(pipe_fds[1]).ok());
  pipe_fds[1] = -1;
}

TEST(StreamTransportSupportTest, ReadsWritesAndReportsDescriptorFailures) {
  std::array<int, 2> pipe_fds = {-1, -1};
  ASSERT_EQ(::pipe(pipe_fds.data()), 0);
  ASSERT_TRUE(support::set_non_blocking(pipe_fds[0]).ok());
  ASSERT_TRUE(support::set_non_blocking(pipe_fds[1]).ok());

  std::array<std::byte, 4> payload = {std::byte{'t'}, std::byte{'e'}, std::byte{'s'}, std::byte{'t'}};
  upr::WriteResult write_result =
      support::write_to_fd(pipe_fds[1], upr::ByteSpan(payload.data(), payload.size()), "write");
  ASSERT_TRUE(write_result.status.ok()) << write_result.status.message();
  EXPECT_EQ(write_result.bytes_written, payload.size());

  bool open = true;
  std::array<std::byte, 4> buffer{};
  upr::ReadResult read_result =
      support::read_from_fd(pipe_fds[0], upr::MutableByteSpan(buffer.data(), buffer.size()), &open, "read");
  ASSERT_TRUE(read_result.status.ok()) << read_result.status.message();
  EXPECT_EQ(read_result.bytes_read, payload.size());
  EXPECT_TRUE(open);

  std::array<std::byte, 1> empty{};
  upr::ReadResult blocked =
      support::read_from_fd(pipe_fds[0], upr::MutableByteSpan(empty.data(), empty.size()), &open, "read");
  EXPECT_TRUE(blocked.would_block);

  EXPECT_TRUE(support::close_fd(pipe_fds[1]).ok());
  pipe_fds[1] = -1;
  upr::ReadResult eos =
      support::read_from_fd(pipe_fds[0], upr::MutableByteSpan(empty.data(), empty.size()), &open, "read");
  EXPECT_TRUE(eos.end_of_stream);
  EXPECT_FALSE(open);

  upr::ReadResult failed_read =
      support::read_from_fd(-1, upr::MutableByteSpan(empty.data(), empty.size()), nullptr, "read");
  EXPECT_FALSE(failed_read.status.ok());
  EXPECT_EQ(failed_read.status.code(), upr::StatusCode::kIoError);

  upr::WriteResult failed_write = support::write_to_fd(-1, upr::ByteSpan(payload.data(), payload.size()), "write");
  EXPECT_FALSE(failed_write.status.ok());
  EXPECT_EQ(failed_write.status.code(), upr::StatusCode::kIoError);

  EXPECT_TRUE(support::close_fd(pipe_fds[0]).ok());
}

TEST(StreamTransportSupportTest, HandlesSocketHelpersAndEndpointFormatting) {
  std::array<int, 2> unix_sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, unix_sockets.data()), 0);
  ASSERT_TRUE(support::set_non_blocking(unix_sockets[0]).ok());
  ASSERT_TRUE(support::set_non_blocking(unix_sockets[1]).ok());

  std::array<std::byte, 3> payload = {std::byte{'o'}, std::byte{'n'}, std::byte{'e'}};
  upr::WriteResult send_result =
      support::send_to_socket(unix_sockets[0], upr::ByteSpan(payload.data(), payload.size()));
  ASSERT_TRUE(send_result.status.ok()) << send_result.status.message();
  EXPECT_EQ(send_result.bytes_written, payload.size());

  bool open = true;
  std::array<std::byte, 3> received{};
  upr::ReadResult recv_result =
      support::recv_from_socket(unix_sockets[1], upr::MutableByteSpan(received.data(), received.size()), &open);
  ASSERT_TRUE(recv_result.status.ok()) << recv_result.status.message();
  EXPECT_EQ(recv_result.bytes_read, payload.size());

  std::array<std::byte, 1> empty{};
  upr::ReadResult would_block =
      support::recv_from_socket(unix_sockets[1], upr::MutableByteSpan(empty.data(), empty.size()), &open);
  EXPECT_TRUE(would_block.would_block);

  EXPECT_TRUE(support::close_fd(unix_sockets[0]).ok());
  unix_sockets[0] = -1;
  upr::ReadResult recv_eos =
      support::recv_from_socket(unix_sockets[1], upr::MutableByteSpan(empty.data(), empty.size()), &open);
  EXPECT_TRUE(recv_eos.end_of_stream);
  EXPECT_FALSE(open);

  upr::ReadResult failed_recv =
      support::recv_from_socket(-1, upr::MutableByteSpan(empty.data(), empty.size()), nullptr);
  EXPECT_FALSE(failed_recv.status.ok());
  EXPECT_EQ(failed_recv.status.code(), upr::StatusCode::kIoError);

  upr::WriteResult failed_send = support::send_to_socket(-1, upr::ByteSpan(payload.data(), payload.size()));
  EXPECT_FALSE(failed_send.status.ok());
  EXPECT_EQ(failed_send.status.code(), upr::StatusCode::kIoError);

  EXPECT_EQ(support::socket_endpoint_string(-1, false), "");
  EXPECT_EQ(support::socket_endpoint_string(unix_sockets[1], false), "");
  EXPECT_EQ(support::socket_endpoint_string(unix_sockets[1], true), "");

  EXPECT_TRUE(support::close_fd(unix_sockets[1]).ok());
}

TEST(StreamTransportSupportTest, HandlesVectorWritesBufferConfigurationAndTcpEndpoints) {
  std::array<int, 2> pipe_fds = {-1, -1};
  ASSERT_EQ(::pipe(pipe_fds.data()), 0);

  const std::array<std::byte, 2> first = {std::byte{'a'}, std::byte{'b'}};
  const std::array<std::byte, 2> second = {std::byte{'c'}, std::byte{'d'}};
  const std::array<upr::ByteSpan, 3> sources = {
      upr::ByteSpan(first.data(), first.size()),
      upr::ByteSpan(),
      upr::ByteSpan(second.data(), second.size()),
  };

  upr::WriteResult writev_result = support::writev_to_fd(pipe_fds[1], sources, false);
  ASSERT_TRUE(writev_result.status.ok()) << writev_result.status.message();
  EXPECT_EQ(writev_result.bytes_written, 4U);

  std::array<std::byte, 4> pipe_buffer{};
  ASSERT_EQ(::read(pipe_fds[0], pipe_buffer.data(), pipe_buffer.size()), 4);

  upr::WriteResult empty_writev = support::writev_to_fd(pipe_fds[1], std::span<const upr::ByteSpan>(), false);
  EXPECT_TRUE(empty_writev.status.ok());
  EXPECT_EQ(empty_writev.bytes_written, 0U);

  upr::WriteResult failed_writev = support::writev_to_fd(-1, sources, false);
  EXPECT_FALSE(failed_writev.status.ok());
  EXPECT_EQ(failed_writev.status.code(), upr::StatusCode::kIoError);

  std::array<int, 2> unix_sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, unix_sockets.data()), 0);
  upr::WriteResult sendmsg_result = support::writev_to_fd(unix_sockets[0], sources, true);
  ASSERT_TRUE(sendmsg_result.status.ok()) << sendmsg_result.status.message();
  EXPECT_EQ(sendmsg_result.bytes_written, 4U);

  std::array<std::byte, 4> socket_buffer{};
  ASSERT_EQ(::recv(unix_sockets[1], socket_buffer.data(), socket_buffer.size(), 0), 4);

  EXPECT_TRUE(support::configure_socket_buffers(unix_sockets[0], 8192, 8192).ok());
  upr::Status bad_buffers = support::configure_socket_buffers(-1, 8192, 8192);
  EXPECT_FALSE(bad_buffers.ok());
  EXPECT_EQ(bad_buffers.code(), upr::StatusCode::kIoError);

  int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(listen_fd, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(0);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(::bind(listen_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
  ASSERT_EQ(::listen(listen_fd, 1), 0);

  socklen_t address_len = sizeof(address);
  ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&address), &address_len), 0);
  const uint16_t port = ntohs(address.sin_port);

  int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(client_fd, 0);
  ASSERT_EQ(::connect(client_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
  int server_fd = ::accept(listen_fd, nullptr, nullptr);
  ASSERT_GE(server_fd, 0);

  EXPECT_NE(support::socket_endpoint_string(client_fd, false).find(':'), std::string::npos);
  EXPECT_NE(support::socket_endpoint_string(client_fd, true), "");
  EXPECT_NE(support::socket_endpoint_string(server_fd, false), "");
  EXPECT_NE(port, 0);

  EXPECT_TRUE(support::close_fd(server_fd).ok());
  EXPECT_TRUE(support::close_fd(client_fd).ok());
  EXPECT_TRUE(support::close_fd(listen_fd).ok());
  EXPECT_TRUE(support::close_fd(unix_sockets[0]).ok());
  EXPECT_TRUE(support::close_fd(unix_sockets[1]).ok());
  EXPECT_TRUE(support::close_fd(pipe_fds[0]).ok());
  EXPECT_TRUE(support::close_fd(pipe_fds[1]).ok());
}

}  // namespace
