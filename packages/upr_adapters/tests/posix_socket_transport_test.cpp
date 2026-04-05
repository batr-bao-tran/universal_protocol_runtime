#include "universal_protocol_runtime/adapters/posix_socket_transport.hpp"

#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <string_view>
#include <thread>

namespace upr = universal_protocol_runtime;

namespace {

TEST(PosixSocketTransportTest, ReadsFromSocketPairsAndReportsWouldBlock) {
  std::array<int, 2> sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0) << std::strerror(errno);

  upr::PosixSocketTransport transport(sockets[0], {.own_handle = true, .non_blocking = true});
  ASSERT_TRUE(transport.is_open());

  std::array<std::byte, 8> buffer{};
  upr::ReadResult blocked = transport.read(buffer);
  EXPECT_TRUE(blocked.would_block);

  constexpr std::string_view kPayload = "xy";
  ASSERT_EQ(::write(sockets[1], kPayload.data(), kPayload.size()), static_cast<ssize_t>(kPayload.size()));
  upr::ReadResult first_read = transport.read(buffer);
  EXPECT_EQ(first_read.bytes_read, 2U);
  EXPECT_EQ(std::to_integer<char>(buffer[0]), 'x');
  EXPECT_EQ(std::to_integer<char>(buffer[1]), 'y');

  ASSERT_EQ(::close(sockets[1]), 0);
  upr::ReadResult eof = transport.read(buffer);
  EXPECT_TRUE(eof.end_of_stream);
}

TEST(PosixSocketTransportTest, ConnectsToLoopbackTcpServers) {
  const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(server_fd, 0) << std::strerror(errno);

  int reuse_addr = 1;
  ASSERT_EQ(::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)), 0)
      << std::strerror(errno);

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = 0;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(::bind(server_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0) << std::strerror(errno);
  ASSERT_EQ(::listen(server_fd, 1), 0) << std::strerror(errno);

  socklen_t address_length = sizeof(address);
  ASSERT_EQ(::getsockname(server_fd, reinterpret_cast<sockaddr*>(&address), &address_length), 0)
      << std::strerror(errno);
  const uint16_t port = ntohs(address.sin_port);

  std::thread server_thread([server_fd]() {
    const int client_fd = ::accept(server_fd, nullptr, nullptr);
    ASSERT_GE(client_fd, 0) << std::strerror(errno);
    constexpr std::string_view kPayload = "hello";
    ASSERT_EQ(::write(client_fd, kPayload.data(), kPayload.size()), static_cast<ssize_t>(kPayload.size()));
    ASSERT_EQ(::close(client_fd), 0);
    ASSERT_EQ(::close(server_fd), 0);
  });

  upr::StatusOr<upr::PosixSocketTransport> transport =
      upr::PosixSocketTransport::connect_tcp("127.0.0.1", port, {.non_blocking_after_connect = true});
  ASSERT_TRUE(transport.ok()) << transport.status().message();

  upr::StatusOr<bool> readable = transport.value().wait_until_readable(1000);
  ASSERT_TRUE(readable.ok()) << readable.status().message();
  EXPECT_TRUE(readable.value());

  std::array<std::byte, 16> buffer{};
  upr::ReadResult result = transport.value().read(buffer);
  EXPECT_EQ(result.bytes_read, 5U);
  EXPECT_EQ(std::to_integer<char>(buffer[0]), 'h');
  EXPECT_EQ(std::to_integer<char>(buffer[4]), 'o');

  server_thread.join();
}

TEST(PosixSocketTransportTest, SupportsWaitsMovesAndNonOwningClosePaths) {
  std::array<int, 2> sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0) << std::strerror(errno);

  upr::PosixSocketTransport source(sockets[0], {.own_handle = false, .non_blocking = true});
  ASSERT_TRUE(source.is_open());

  upr::StatusOr<bool> initially_readable = source.wait_until_readable(0);
  ASSERT_TRUE(initially_readable.ok()) << initially_readable.status().message();
  EXPECT_FALSE(initially_readable.value());

  constexpr std::string_view kPayload = "p";
  ASSERT_EQ(::write(sockets[1], kPayload.data(), kPayload.size()), static_cast<ssize_t>(kPayload.size()));

  upr::StatusOr<bool> after_write = source.wait_until_readable(1000);
  ASSERT_TRUE(after_write.ok()) << after_write.status().message();
  EXPECT_TRUE(after_write.value());

  upr::PosixSocketTransport moved;
  moved = std::move(source);
  EXPECT_TRUE(moved.is_open());

  EXPECT_TRUE(moved.close().ok());
  EXPECT_FALSE(moved.is_open());

  std::array<std::byte, 1> buffer{};
  upr::ReadResult closed_read = moved.read(buffer);
  EXPECT_TRUE(closed_read.end_of_stream);

  ASSERT_EQ(::close(sockets[0]), 0);
  ASSERT_EQ(::close(sockets[1]), 0);
}

TEST(PosixSocketTransportTest, ReportsConnectionAndClosedSocketFailuresDefensively) {
  upr::PosixSocketTransport closed;
  EXPECT_TRUE(closed.close().ok());
  EXPECT_FALSE(closed.is_open());

  std::array<std::byte, 1> buffer{};
  upr::ReadResult closed_read = closed.read(buffer);
  EXPECT_TRUE(closed_read.end_of_stream);

  upr::StatusOr<bool> closed_wait = closed.wait_until_readable(0);
  EXPECT_FALSE(closed_wait.ok());
  EXPECT_EQ(closed_wait.status().code(), upr::StatusCode::kInvalidArgument);

  upr::StatusOr<upr::PosixSocketTransport> bad_host =
      upr::PosixSocketTransport::connect_tcp("definitely.invalid", 12345);
  EXPECT_FALSE(bad_host.ok());

  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(listener, 0) << std::strerror(errno);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = 0;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0) << std::strerror(errno);

  socklen_t address_length = sizeof(address);
  ASSERT_EQ(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_length), 0) << std::strerror(errno);
  const uint16_t unused_port = ntohs(address.sin_port);
  ASSERT_EQ(::close(listener), 0);

  upr::StatusOr<upr::PosixSocketTransport> refused = upr::PosixSocketTransport::connect_tcp("127.0.0.1", unused_port);
  EXPECT_FALSE(refused.ok());

  std::array<int, 2> sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0) << std::strerror(errno);
  ASSERT_EQ(::close(sockets[0]), 0);

  upr::PosixSocketTransport invalid(sockets[0], {.own_handle = true, .non_blocking = true});
  EXPECT_FALSE(invalid.is_open());
  EXPECT_EQ(invalid.native_handle(), -1);

  ASSERT_EQ(::close(sockets[1]), 0);
}

TEST(PosixSocketTransportTest, ReportsErrorsFromExternallyClosedSockets) {
  std::array<int, 2> sockets = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0) << std::strerror(errno);

  upr::PosixSocketTransport transport(sockets[0], {.own_handle = true, .non_blocking = false});
  ASSERT_TRUE(transport.is_open());

  ASSERT_EQ(::close(sockets[0]), 0);

  std::array<std::byte, 1> buffer{};
  const upr::ReadResult read_result = transport.read(buffer);
  EXPECT_FALSE(read_result.status.ok());
  EXPECT_EQ(read_result.status.code(), upr::StatusCode::kIoError);

  const upr::StatusOr<bool> wait_result = transport.wait_until_readable(0);
  ASSERT_TRUE(wait_result.ok()) << wait_result.status().message();
  EXPECT_TRUE(wait_result.value());

  const upr::Status close_status = transport.close();
  EXPECT_FALSE(close_status.ok());
  EXPECT_EQ(close_status.code(), upr::StatusCode::kIoError);

  ASSERT_EQ(::close(sockets[1]), 0);
}

}  // namespace
