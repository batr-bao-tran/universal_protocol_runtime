#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_SOCKET_TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_SOCKET_TRANSPORT_HPP_

#include <cstdint>
#include <string>

#include "universal_protocol_runtime/adapters/posix_fd_transport.hpp"
#include "universal_protocol_runtime/transport/transport.hpp"

namespace universal_protocol_runtime {

struct TcpClientOptions {
  bool own_handle = true;
  bool non_blocking_after_connect = true;
};

class PosixSocketTransport final : public ITransport {
 public:
  PosixSocketTransport() = default;
  explicit PosixSocketTransport(int fd, PosixTransportOptions options = {});
  ~PosixSocketTransport() noexcept override;

  PosixSocketTransport(const PosixSocketTransport&) = delete;
  PosixSocketTransport& operator=(const PosixSocketTransport&) = delete;
  PosixSocketTransport(PosixSocketTransport&& other) noexcept;
  PosixSocketTransport& operator=(PosixSocketTransport&& other) noexcept;

  static StatusOr<PosixSocketTransport> connect_tcp(const std::string& host,
                                                    uint16_t port,
                                                    TcpClientOptions options = {});

  ReadResult read(MutableByteSpan destination) override;
  bool is_open() const override;

  int native_handle() const { return fd_; }
  Status close();
  StatusOr<bool> wait_until_readable(int timeout_ms) const;

 private:
  void move_from(PosixSocketTransport* other) noexcept;

  int fd_ = -1;
  bool open_ = false;
  bool own_handle_ = true;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_SOCKET_TRANSPORT_HPP_
