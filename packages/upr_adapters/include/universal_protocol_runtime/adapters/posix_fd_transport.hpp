#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_FD_TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_FD_TRANSPORT_HPP_

#include <string>

#include "universal_protocol_runtime/transport/transport.hpp"

namespace universal_protocol_runtime {

struct PosixTransportOptions {
  bool own_handle = true;
  bool non_blocking = false;
};

class PosixFdTransport final : public ITransport {
 public:
  PosixFdTransport() = default;
  explicit PosixFdTransport(int fd, PosixTransportOptions options = {});
  ~PosixFdTransport() noexcept override;

  PosixFdTransport(const PosixFdTransport&) = delete;
  PosixFdTransport& operator=(const PosixFdTransport&) = delete;
  PosixFdTransport(PosixFdTransport&& other) noexcept;
  PosixFdTransport& operator=(PosixFdTransport&& other) noexcept;

  static StatusOr<PosixFdTransport> open_device(const std::string& path,
                                                int open_flags,
                                                PosixTransportOptions options = {});

  ReadResult read(MutableByteSpan destination) override;
  bool is_open() const override;

  int native_handle() const { return fd_; }
  Status close();
  StatusOr<bool> wait_until_readable(int timeout_ms) const;

 private:
  void move_from(PosixFdTransport* other) noexcept;

  int fd_ = -1;
  bool open_ = false;
  bool own_handle_ = true;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_FD_TRANSPORT_HPP_
