#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_SOCKET_TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_SOCKET_TRANSPORT_HPP_

#include <cstdint>
#include <string>

#include "universal_protocol_runtime/adapters/posix_fd_transport.hpp"
#include "universal_protocol_runtime/transport/transport.hpp"

namespace universal_protocol_runtime {

struct TcpClientOptions {
  /**
   * @brief Indicates whether the transport closes the connected socket.
   */
  bool own_handle = true;
  /**
   * @brief Enables non-blocking mode after connect when true.
   */
  bool non_blocking_after_connect = true;
  /**
   * @brief Maximum time in milliseconds to wait for a connect to complete.
   */
  int connect_timeout_ms = 5000;
};

/**
 * @brief POSIX TCP socket transport adapter.
 */
class PosixSocketTransport final : public ITransport {
 public:
  /**
   * @brief Constructs an empty socket transport.
   */
  PosixSocketTransport() = default;
  /**
   * @brief Constructs a transport from an existing socket fd.
   */
  explicit PosixSocketTransport(int fd, PosixTransportOptions options = {});
  /**
   * @brief Destroys the transport and closes owned resources.
   */
  ~PosixSocketTransport() noexcept override;

  PosixSocketTransport(const PosixSocketTransport&) = delete;
  PosixSocketTransport& operator=(const PosixSocketTransport&) = delete;
  PosixSocketTransport(PosixSocketTransport&& other) noexcept;
  PosixSocketTransport& operator=(PosixSocketTransport&& other) noexcept;

  /**
   * @brief Connects to a TCP endpoint and wraps the socket.
   */
  static StatusOr<PosixSocketTransport> connect_tcp(const std::string& host,
                                                    uint16_t port,
                                                    TcpClientOptions options = {});

  /**
   * @brief Reads bytes from the socket.
   */
  ReadResult read(MutableByteSpan destination) override;
  /**
   * @brief Indicates whether the socket is open.
   */
  bool is_open() const override;

  /**
   * @brief Returns the native socket descriptor.
   */
  int native_handle() const { return fd_; }
  /**
   * @brief Closes the socket transport.
   */
  Status close();
  /**
   * @brief Waits until the socket is readable.
   */
  StatusOr<bool> wait_until_readable(int timeout_ms) const;

 private:
  void move_from(PosixSocketTransport* other) noexcept;

  int fd_ = -1;
  bool open_ = false;
  bool own_handle_ = true;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_SOCKET_TRANSPORT_HPP_
