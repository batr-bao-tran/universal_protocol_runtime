#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__TCP_STREAM_TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__TCP_STREAM_TRANSPORT_HPP_

#include <cstdint>
#include <memory>
#include <string>

#include "universal_protocol_runtime/adapters/byte_stream_transport.hpp"
#include "universal_protocol_runtime/adapters/unix_socket_transport.hpp"

namespace universal_protocol_runtime {

struct TcpTransportOptions : SocketTransportOptions {
  /**
   * @brief Enables TCP_NODELAY when true.
   */
  bool tcp_no_delay = true;
};

/**
 * @brief TCP stream transport for cross-host communication.
 */
class TcpStreamTransport final : public IByteStreamTransport {
 public:
  /**
   * @brief Constructs an empty TCP transport.
   */
  TcpStreamTransport() = default;
  /**
   * @brief Constructs a transport from a socket descriptor.
   */
  explicit TcpStreamTransport(int fd, TcpTransportOptions options = {});
  /**
   * @brief Destroys the transport and closes owned resources.
   */
  ~TcpStreamTransport() noexcept override;

  TcpStreamTransport(const TcpStreamTransport&) = delete;
  TcpStreamTransport& operator=(const TcpStreamTransport&) = delete;
  TcpStreamTransport(TcpStreamTransport&& other) noexcept;
  TcpStreamTransport& operator=(TcpStreamTransport&& other) noexcept;

  /**
   * @brief Connects to a remote host and TCP port.
   */
  static StatusOr<TcpStreamTransport> connect_to_host(const std::string& host,
                                                      uint16_t port,
                                                      TcpTransportOptions options = {});

  /**
   * @brief Reads bytes from the TCP socket.
   */
  ReadResult read(MutableByteSpan destination) override;
  /**
   * @brief Writes bytes to the TCP socket.
   */
  WriteResult write(ByteSpan source) override;
  /**
   * @brief Writes multiple byte spans to the TCP socket.
   */
  WriteResult writev(std::span<const ByteSpan> sources) override;
  /**
   * @brief Closes the TCP transport.
   */
  Status close() override;
  /**
   * @brief Indicates whether the TCP socket is open.
   */
  bool is_open() const override;
  /**
   * @brief Returns the native socket descriptor.
   */
  int native_handle() const override { return fd_; }
  /**
   * @brief Waits until the socket becomes readable.
   */
  StatusOr<bool> wait_until_readable(int timeout_ms) const override;
  /**
   * @brief Waits until the socket becomes writable.
   */
  StatusOr<bool> wait_until_writable(int timeout_ms) const override;
  /**
   * @brief Returns capability flags for this transport.
   */
  TransportCapabilityMask capabilities() const override;
  /**
   * @brief Returns a local endpoint description.
   */
  std::string local_endpoint() const override;
  /**
   * @brief Returns a peer endpoint description.
   */
  std::string peer_endpoint() const override;

 private:
  void move_from(TcpStreamTransport* other) noexcept;

  int fd_ = -1;
  bool open_ = false;
  bool own_handle_ = true;
};

/**
 * @brief TCP loopback listener for accepting client connections.
 */
class TcpListener final : public IListenerTransport {
 public:
  /**
   * @brief Constructs an empty listener.
   */
  TcpListener() = default;
  /**
   * @brief Destroys the listener and closes owned resources.
   */
  ~TcpListener() noexcept override;

  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;
  TcpListener(TcpListener&& other) noexcept;
  TcpListener& operator=(TcpListener&& other) noexcept;

  /**
   * @brief Binds and listens on the loopback interface.
   */
  static StatusOr<TcpListener> bind_loopback(uint16_t port, TcpTransportOptions options = {});

  /**
   * @brief Returns the bound local port.
   */
  uint16_t port() const { return port_; }

  /**
   * @brief Waits until a connection is ready to accept.
   */
  StatusOr<bool> wait_for_connection(int timeout_ms) const override;
  /**
   * @brief Accepts a pending TCP connection.
   */
  StatusOr<std::unique_ptr<IByteStreamTransport>> accept() override;
  /**
   * @brief Closes the listener socket.
   */
  Status close() override;
  /**
   * @brief Returns the native listener descriptor.
   */
  int native_handle() const override { return fd_; }
  /**
   * @brief Indicates whether the listener is open.
   */
  bool is_open() const override;
  /**
   * @brief Returns the local endpoint string.
   */
  std::string local_endpoint() const override;

 private:
  void move_from(TcpListener* other) noexcept;

  int fd_ = -1;
  bool open_ = false;
  uint16_t port_ = 0;
  TcpTransportOptions options_{};
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__TCP_STREAM_TRANSPORT_HPP_
