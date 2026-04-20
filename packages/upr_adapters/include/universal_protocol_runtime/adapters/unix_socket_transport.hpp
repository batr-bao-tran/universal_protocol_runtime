#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__UNIX_SOCKET_TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__UNIX_SOCKET_TRANSPORT_HPP_

#include <memory>
#include <string>
#include <utility>

#include "universal_protocol_runtime/adapters/byte_stream_transport.hpp"

namespace universal_protocol_runtime {

struct SocketTransportOptions {
  /**
   * @brief Indicates whether the transport closes the provided handle.
   */
  bool own_handle = true;
  /**
   * @brief Indicates whether sockets should be configured non-blocking.
   */
  bool non_blocking = true;
  /**
   * @brief Optional send buffer size in bytes.
   */
  int send_buffer_bytes = 0;
  /**
   * @brief Optional receive buffer size in bytes.
   */
  int receive_buffer_bytes = 0;
};

/**
 * @brief Unix domain stream transport implementation.
 */
class UnixSocketTransport final : public IByteStreamTransport {
 public:
  /**
   * @brief Constructs an empty Unix socket transport.
   */
  UnixSocketTransport() = default;
  /**
   * @brief Constructs a transport from a socket descriptor.
   */
  explicit UnixSocketTransport(int fd, SocketTransportOptions options = {});
  /**
   * @brief Destroys the transport and closes owned resources.
   */
  ~UnixSocketTransport() noexcept override;

  UnixSocketTransport(const UnixSocketTransport&) = delete;
  UnixSocketTransport& operator=(const UnixSocketTransport&) = delete;
  UnixSocketTransport(UnixSocketTransport&& other) noexcept;
  UnixSocketTransport& operator=(UnixSocketTransport&& other) noexcept;

  /**
   * @brief Connects to a Unix domain socket path.
   */
  static StatusOr<UnixSocketTransport> connect_to_path(const std::string& path, SocketTransportOptions options = {});
  /**
   * @brief Creates a connected Unix socket pair.
   */
  static StatusOr<std::pair<UnixSocketTransport, UnixSocketTransport>> create_socket_pair(
      SocketTransportOptions options = {});

  /**
   * @brief Reads bytes from the socket.
   */
  ReadResult read(MutableByteSpan destination) override;
  /**
   * @brief Writes bytes to the socket.
   */
  WriteResult write(ByteSpan source) override;
  /**
   * @brief Writes multiple byte spans to the socket.
   */
  WriteResult writev(std::span<const ByteSpan> sources) override;
  /**
   * @brief Closes the socket transport.
   */
  Status close() override;
  /**
   * @brief Indicates whether the socket is open.
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
  void move_from(UnixSocketTransport* other) noexcept;

  int fd_ = -1;
  bool open_ = false;
  bool own_handle_ = true;
};

/**
 * @brief Unix domain socket listener for inbound stream connections.
 */
class UnixSocketListener final : public IListenerTransport {
 public:
  /**
   * @brief Constructs an empty listener.
   */
  UnixSocketListener() = default;
  /**
   * @brief Destroys the listener and closes owned resources.
   */
  ~UnixSocketListener() noexcept override;

  UnixSocketListener(const UnixSocketListener&) = delete;
  UnixSocketListener& operator=(const UnixSocketListener&) = delete;
  UnixSocketListener(UnixSocketListener&& other) noexcept;
  UnixSocketListener& operator=(UnixSocketListener&& other) noexcept;

  /**
   * @brief Binds and starts listening on a Unix socket path.
   */
  static StatusOr<UnixSocketListener> bind_path(const std::string& path, SocketTransportOptions options = {});

  /**
   * @brief Waits until a connection is ready to accept.
   */
  StatusOr<bool> wait_for_connection(int timeout_ms) const override;
  /**
   * @brief Accepts a pending Unix socket connection.
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
   * @brief Returns the bound local endpoint path.
   */
  std::string local_endpoint() const override { return path_; }

 private:
  void move_from(UnixSocketListener* other) noexcept;

  int fd_ = -1;
  bool open_ = false;
  std::string path_;
  SocketTransportOptions options_{};
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__UNIX_SOCKET_TRANSPORT_HPP_
