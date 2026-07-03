#include "universal_protocol_runtime/adapters/tcp_stream_transport.hpp"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <thread>

namespace upr = universal_protocol_runtime;

namespace {

inline constexpr int kNetworkTimeoutMs(3000);

TEST(TcpStreamTransportTest, ConnectsToLoopbackListenerAndTransfersBytes) {
  auto listener = upr::TcpListener::bind_loopback(0);
  ASSERT_TRUE(listener.ok()) << listener.status().message();

  std::thread server_thread([&]() {
    const auto ready = listener.value().wait_for_connection(kNetworkTimeoutMs);
    ASSERT_TRUE(ready.ok()) << ready.status().message();
    ASSERT_TRUE(ready.value());
    auto accepted = listener.value().accept();
    ASSERT_TRUE(accepted.ok()) << accepted.status().message();
    const std::array<std::byte, 5> payload = {
        std::byte{'h'},
        std::byte{'e'},
        std::byte{'l'},
        std::byte{'l'},
        std::byte{'o'},
    };
    ASSERT_TRUE(accepted.value()->write(upr::ByteSpan(payload.data(), payload.size())).status.ok());
  });

  auto client = upr::TcpStreamTransport::connect_to_host("127.0.0.1", listener.value().port());
  ASSERT_TRUE(client.ok()) << client.status().message();

  auto readable = client.value().wait_until_readable(kNetworkTimeoutMs);
  ASSERT_TRUE(readable.ok()) << readable.status().message();
  EXPECT_TRUE(readable.value());

  std::array<std::byte, 5> buffer{};
  const upr::ReadResult read_result = client.value().read(buffer);
  ASSERT_TRUE(read_result.status.ok()) << read_result.status.message();
  EXPECT_EQ(read_result.bytes_read, 5U);
  EXPECT_EQ(buffer[1], std::byte{'e'});

  server_thread.join();
}

TEST(TcpStreamTransportTest, SupportsVectoredWriteOverLoopbackConnection) {
  auto listener = upr::TcpListener::bind_loopback(0);
  ASSERT_TRUE(listener.ok()) << listener.status().message();

  std::thread server_thread([&]() {
    const auto ready = listener.value().wait_for_connection(kNetworkTimeoutMs);
    ASSERT_TRUE(ready.ok()) << ready.status().message();
    ASSERT_TRUE(ready.value());
    auto accepted = listener.value().accept();
    ASSERT_TRUE(accepted.ok()) << accepted.status().message();
    const auto readable = accepted.value()->wait_until_readable(kNetworkTimeoutMs);
    ASSERT_TRUE(readable.ok()) << readable.status().message();
    ASSERT_TRUE(readable.value());
    std::array<std::byte, 5> buffer{};
    const upr::ReadResult read_result = accepted.value()->read(buffer);
    ASSERT_TRUE(read_result.status.ok()) << read_result.status.message();
    ASSERT_EQ(read_result.bytes_read, 5U);
    EXPECT_EQ(buffer[0], std::byte{'h'});
    EXPECT_EQ(buffer[4], std::byte{'o'});
  });

  auto client = upr::TcpStreamTransport::connect_to_host("127.0.0.1", listener.value().port());
  ASSERT_TRUE(client.ok()) << client.status().message();

  const std::array<std::byte, 2> first = {std::byte{'h'}, std::byte{'e'}};
  const std::array<std::byte, 3> second = {std::byte{'l'}, std::byte{'l'}, std::byte{'o'}};
  const std::array<upr::ByteSpan, 2> spans = {
      upr::ByteSpan(first.data(), first.size()),
      upr::ByteSpan(second.data(), second.size()),
  };
  const upr::WriteResult write_result = client.value().writev(spans);
  ASSERT_TRUE(write_result.status.ok()) << write_result.status.message();
  EXPECT_EQ(write_result.bytes_written, 5U);
  EXPECT_FALSE(write_result.would_block);

  server_thread.join();
}

TEST(TcpStreamTransportTest, ReportsClosedAndNoPendingConnectionFailuresDefensively) {
  upr::TcpStreamTransport closed;
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

  auto listener = upr::TcpListener::bind_loopback(0);
  ASSERT_TRUE(listener.ok()) << listener.status().message();
  auto ready = listener.value().wait_for_connection(0);
  ASSERT_TRUE(ready.ok()) << ready.status().message();
  EXPECT_FALSE(ready.value());
  auto no_connection = listener.value().accept();
  EXPECT_FALSE(no_connection.ok());
  EXPECT_EQ(no_connection.status().code(), upr::StatusCode::kNotFound);
  EXPECT_TRUE(listener.value().close().ok());
  EXPECT_FALSE(listener.value().is_open());
}

TEST(TcpStreamTransportTest, ReportsConnectionFailureForUnusedPort) {
  auto listener = upr::TcpListener::bind_loopback(0);
  ASSERT_TRUE(listener.ok()) << listener.status().message();
  const uint16_t unused_port = listener.value().port();
  ASSERT_TRUE(listener.value().close().ok());

  auto connect_result = upr::TcpStreamTransport::connect_to_host("127.0.0.1", unused_port);
  EXPECT_FALSE(connect_result.ok());
}

TEST(TcpStreamTransportTest, ReportsLookupFailureForInvalidHost) {
  upr::TcpTransportOptions options;
  options.connect_timeout_ms = 1000;
  auto connect_result = upr::TcpStreamTransport::connect_to_host("invalid.invalid.upr.test", 12345, options);
  EXPECT_FALSE(connect_result.ok());
  EXPECT_EQ(connect_result.status().code(), upr::StatusCode::kIoError);
}

TEST(TcpStreamTransportTest, SupportsEndpointsMoveAssignmentAndListenerMetadata) {
  upr::TcpTransportOptions listener_options;
  listener_options.non_blocking = false;
  auto listener = upr::TcpListener::bind_loopback(0, listener_options);
  ASSERT_TRUE(listener.ok()) << listener.status().message();
  EXPECT_TRUE(listener.value().is_open());
  EXPECT_NE(listener.value().local_endpoint().find("127.0.0.1:"), std::string::npos);
  EXPECT_GE(listener.value().native_handle(), 0);
  const uint16_t port = listener.value().port();

  std::thread client_thread([port]() {
    upr::TcpTransportOptions client_options;
    client_options.non_blocking = true;
    auto client = upr::TcpStreamTransport::connect_to_host("127.0.0.1", port, client_options);
    ASSERT_TRUE(client.ok()) << client.status().message();
    EXPECT_TRUE(client.value().is_open());
    EXPECT_NE(client.value().local_endpoint().find(':'), std::string::npos);
    EXPECT_NE(client.value().peer_endpoint().find("127.0.0.1:"), std::string::npos);
    EXPECT_EQ(client.value().capabilities(), upr::capability_mask(upr::TransportCapability::kStream));
  });

  const auto ready = listener.value().wait_for_connection(kNetworkTimeoutMs);
  ASSERT_TRUE(ready.ok()) << ready.status().message();
  ASSERT_TRUE(ready.value());
  auto accepted = listener.value().accept();
  ASSERT_TRUE(accepted.ok()) << accepted.status().message();
  EXPECT_TRUE(accepted.value()->is_open());
  EXPECT_NE(accepted.value()->local_endpoint().find("127.0.0.1:"), std::string::npos);

  client_thread.join();

  upr::TcpListener moved;
  moved = std::move(listener.value());
  EXPECT_TRUE(moved.is_open());
  EXPECT_TRUE(moved.close().ok());
  EXPECT_FALSE(moved.is_open());
}

TEST(TcpStreamTransportTest, ConstructorHandlesOwnershipAndInvalidDescriptors) {
  auto listener = upr::TcpListener::bind_loopback(0);
  ASSERT_TRUE(listener.ok()) << listener.status().message();

  std::thread client_thread([&]() {
    int raw_client = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(raw_client, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(listener.value().port());
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::connect(raw_client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);

    upr::TcpTransportOptions borrowed_options;
    borrowed_options.own_handle = false;
    upr::TcpStreamTransport borrowed(raw_client, borrowed_options);
    EXPECT_TRUE(borrowed.is_open());
    EXPECT_TRUE(borrowed.close().ok());
    EXPECT_FALSE(borrowed.is_open());

    const char byte = 'r';
    EXPECT_EQ(::send(raw_client, &byte, 1, MSG_NOSIGNAL), 1);
    EXPECT_EQ(::close(raw_client), 0);
  });

  const auto ready = listener.value().wait_for_connection(kNetworkTimeoutMs);
  ASSERT_TRUE(ready.ok()) << ready.status().message();
  ASSERT_TRUE(ready.value());
  auto accepted = listener.value().accept();
  ASSERT_TRUE(accepted.ok()) << accepted.status().message();
  const auto readable = accepted.value()->wait_until_readable(kNetworkTimeoutMs);
  ASSERT_TRUE(readable.ok()) << readable.status().message();
  ASSERT_TRUE(readable.value());
  std::array<std::byte, 1> buffer{};
  upr::ReadResult read_result = accepted.value()->read(buffer);
  ASSERT_TRUE(read_result.status.ok()) << read_result.status.message();
  EXPECT_EQ(read_result.bytes_read, 1U);
  EXPECT_EQ(buffer[0], std::byte{'r'});
  client_thread.join();

  int stale_socket = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(stale_socket, 0);
  ASSERT_EQ(::close(stale_socket), 0);
  upr::TcpStreamTransport invalid(stale_socket);
  EXPECT_FALSE(invalid.is_open());
  EXPECT_TRUE(invalid.close().ok());
}

}  // namespace
