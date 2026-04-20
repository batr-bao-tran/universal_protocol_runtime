#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_FD_TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_FD_TRANSPORT_HPP_

#include <string>

#include "universal_protocol_runtime/transport/transport.hpp"

namespace universal_protocol_runtime {

struct PosixTransportOptions {
  /**
   * @brief Indicates whether the transport closes the provided handle.
   */
  bool own_handle = true;
  /**
   * @brief Enables non-blocking mode when true.
   */
  bool non_blocking = false;
};

/**
 * @brief POSIX file-descriptor transport adapter.
 */
class PosixFdTransport final : public ITransport {
 public:
  /**
   * @brief Constructs an empty fd transport.
   */
  PosixFdTransport() = default;
  /**
   * @brief Constructs a transport from an existing fd.
   */
  explicit PosixFdTransport(int fd, PosixTransportOptions options = {});
  /**
   * @brief Destroys the transport and closes owned resources.
   */
  ~PosixFdTransport() noexcept override;

  PosixFdTransport(const PosixFdTransport&) = delete;
  PosixFdTransport& operator=(const PosixFdTransport&) = delete;
  PosixFdTransport(PosixFdTransport&& other) noexcept;
  PosixFdTransport& operator=(PosixFdTransport&& other) noexcept;

  /**
   * @brief Opens a device path and wraps its descriptor.
   */
  static StatusOr<PosixFdTransport> open_device(const std::string& path,
                                                int open_flags,
                                                PosixTransportOptions options = {});

  /**
   * @brief Reads bytes from the descriptor.
   */
  ReadResult read(MutableByteSpan destination) override;
  /**
   * @brief Indicates whether the descriptor is open.
   */
  bool is_open() const override;

  /**
   * @brief Returns the native file descriptor.
   */
  int native_handle() const { return fd_; }
  /**
   * @brief Closes the transport descriptor.
   */
  Status close();
  /**
   * @brief Waits until the descriptor is readable.
   */
  StatusOr<bool> wait_until_readable(int timeout_ms) const;

 private:
  void move_from(PosixFdTransport* other) noexcept;

  int fd_ = -1;
  bool open_ = false;
  bool own_handle_ = true;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__POSIX_FD_TRANSPORT_HPP_
