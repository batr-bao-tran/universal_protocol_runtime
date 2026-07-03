#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_SRC_ADAPTERS__STREAM_TRANSPORT_SUPPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_SRC_ADAPTERS__STREAM_TRANSPORT_SUPPORT_HPP_

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "universal_protocol_runtime/adapters/byte_stream_transport.hpp"

namespace universal_protocol_runtime::stream_transport_support {

/**
 * @brief Converts `errno` into a transport `Status`.
 * @param operation Name of the failing operation.
 * @return Error status describing the failure.
 */
inline Status status_from_errno(const std::string& operation) {
  return io_error(operation + ": " + std::string(std::strerror(errno)));
}

/**
 * @brief Enables non-blocking mode for a file descriptor.
 * @param fd File descriptor to update.
 * @return Status describing success or failure.
 */
inline Status set_non_blocking(int fd) {
  const int existing_flags = ::fcntl(fd, F_GETFL, 0);
  if (existing_flags < 0) {
    return status_from_errno("fcntl(F_GETFL)");
  }
  if (::fcntl(fd, F_SETFL, existing_flags | O_NONBLOCK) < 0) {
    return status_from_errno("fcntl(F_SETFL)");
  }
  return Status::ok_status();
}

/**
 * @brief Waits for readiness events on a file descriptor.
 * @param fd File descriptor to poll.
 * @param events Requested `poll(2)` event mask.
 * @param timeout_ms Poll timeout in milliseconds.
 * @return `true` when the descriptor became ready.
 */
inline StatusOr<bool> wait_for_fd(int fd, short events, int timeout_ms) {
  if (fd < 0) {
    return invalid_argument("Cannot wait on a closed file descriptor.");
  }
  struct pollfd descriptor {
    .fd = fd, .events = events, .revents = 0,
  };
  int poll_result;
  do {
    poll_result = ::poll(&descriptor, 1, timeout_ms);
  } while (poll_result < 0 && errno == EINTR);
  if (poll_result == 0) {
    return false;
  }
  if (poll_result < 0) {
    return status_from_errno("poll");
  }
  return true;
}

/**
 * @brief Connects a socket with a bounded timeout.
 * @param fd Socket file descriptor to connect.
 * @param address Destination address.
 * @param address_length Size of the destination address.
 * @param timeout_ms Maximum time to wait for the connection to complete.
 * @return Ok status on success; an error status on failure or timeout.
 *
 * The socket is switched to non-blocking for the duration of the attempt so a
 * dropped or unreachable peer cannot stall the caller, then its original flags
 * are restored for the caller to apply its final mode.
 */
inline Status connect_with_timeout(int fd, const sockaddr* address, socklen_t address_length, int timeout_ms) {
  const int existing_flags = ::fcntl(fd, F_GETFL, 0);
  if (existing_flags < 0) {
    return status_from_errno("fcntl(F_GETFL)");
  }
  if (::fcntl(fd, F_SETFL, existing_flags | O_NONBLOCK) < 0) {
    return status_from_errno("fcntl(F_SETFL)");
  }

  int connect_result;
  do {
    connect_result = ::connect(fd, address, address_length);
  } while (connect_result < 0 && errno == EINTR);

  Status status = Status::ok_status();
  if (connect_result < 0 && (errno == EINPROGRESS || errno == EALREADY || errno == EWOULDBLOCK)) {
    const StatusOr<bool> ready = wait_for_fd(fd, POLLOUT, timeout_ms);
    if (!ready.ok()) {
      status = ready.status();
    } else if (!ready.value()) {
      status = io_error("connect timed out");
    } else {
      int socket_error = 0;
      socklen_t error_length = sizeof(socket_error);
      if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) < 0) {
        status = status_from_errno("getsockopt(SO_ERROR)");
      } else if (socket_error != 0) {
        errno = socket_error;
        status = status_from_errno("connect");
      }
    }
  } else if (connect_result < 0) {
    status = status_from_errno("connect");
  }

  if (::fcntl(fd, F_SETFL, existing_flags) < 0) {
    return status_from_errno("fcntl(F_SETFL restore)");
  }
  return status;
}

/**
 * @brief Reads from a file descriptor into a destination span.
 * @param fd File descriptor to read from.
 * @param destination Destination byte span.
 * @param open_flag Optional flag updated on end-of-stream.
 * @param operation Operation name for error reporting.
 * @return Read result describing the read outcome.
 */
inline ReadResult read_from_fd(int fd, MutableByteSpan destination, bool* open_flag, const char* operation) {
  ssize_t bytes_read;
  do {
    bytes_read = ::read(fd, destination.data(), destination.size());
  } while (bytes_read < 0 && errno == EINTR);
  if (bytes_read > 0) {
    return {.bytes_read = static_cast<size_t>(bytes_read)};
  }
  if (bytes_read == 0) {
    if (open_flag != nullptr) {
      *open_flag = false;
    }
    return {.end_of_stream = true};
  }
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    return {.would_block = true};
  }
  return {.status = status_from_errno(operation)};
}

/**
 * @brief Receives bytes from a socket into a destination span.
 * @param fd Socket file descriptor to read from.
 * @param destination Destination byte span.
 * @param open_flag Optional flag updated on end-of-stream.
 * @return Read result describing the receive outcome.
 */
inline ReadResult recv_from_socket(int fd, MutableByteSpan destination, bool* open_flag) {
  ssize_t bytes_read;
  do {
    bytes_read = ::recv(fd, destination.data(), destination.size(), 0);
  } while (bytes_read < 0 && errno == EINTR);
  if (bytes_read > 0) {
    return {.bytes_read = static_cast<size_t>(bytes_read)};
  }
  if (bytes_read == 0) {
    if (open_flag != nullptr) {
      *open_flag = false;
    }
    return {.end_of_stream = true};
  }
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    return {.would_block = true};
  }
  return {.status = status_from_errno("recv")};
}

/**
 * @brief Writes bytes to a file descriptor.
 * @param fd File descriptor to write to.
 * @param source Source byte span.
 * @param operation Operation name for error reporting.
 * @return Write result describing the write outcome.
 */
inline WriteResult write_to_fd(int fd, ByteSpan source, const char* operation) {
  ssize_t bytes_written;
  do {
    bytes_written = ::write(fd, source.data(), source.size());
  } while (bytes_written < 0 && errno == EINTR);
  if (bytes_written >= 0) {
    return {.bytes_written = static_cast<size_t>(bytes_written)};
  }
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    return {.would_block = true};
  }
  return {.status = status_from_errno(operation)};
}

/**
 * @brief Sends bytes to a socket.
 * @param fd Socket file descriptor to write to.
 * @param source Source byte span.
 * @return Write result describing the send outcome.
 */
inline WriteResult send_to_socket(int fd, ByteSpan source) {
  ssize_t bytes_written;
  do {
    bytes_written = ::send(fd, source.data(), source.size(), MSG_NOSIGNAL);
  } while (bytes_written < 0 && errno == EINTR);
  if (bytes_written >= 0) {
    return {.bytes_written = static_cast<size_t>(bytes_written)};
  }
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    return {.would_block = true};
  }
  return {.status = status_from_errno("send")};
}

/**
 * @brief Writes multiple byte spans with `writev` or `sendmsg`.
 * @param fd File descriptor or socket to write to.
 * @param sources Source spans to transmit.
 * @param socket_send `true` to use socket send semantics.
 * @return Write result describing the vectored write outcome.
 */
inline WriteResult writev_to_fd(int fd, std::span<const ByteSpan> sources, bool socket_send) {
  std::vector<struct iovec> iovecs;
  iovecs.reserve(sources.size());
  for (const ByteSpan source : sources) {
    if (source.empty()) {
      continue;
    }
    iovecs.push_back({
        .iov_base = const_cast<std::byte*>(source.data()),
        .iov_len = source.size(),
    });
  }
  if (iovecs.empty()) {
    return {};
  }
  ssize_t bytes_written;
  if (socket_send) {
    struct msghdr message {};
    message.msg_iov = iovecs.data();
    message.msg_iovlen = iovecs.size();
    do {
      bytes_written = ::sendmsg(fd, &message, MSG_NOSIGNAL);
    } while (bytes_written < 0 && errno == EINTR);
  } else {
    do {
      bytes_written = ::writev(fd, iovecs.data(), static_cast<int>(iovecs.size()));
    } while (bytes_written < 0 && errno == EINTR);
  }
  if (bytes_written >= 0) {
    return {.bytes_written = static_cast<size_t>(bytes_written)};
  }
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    return {.would_block = true};
  }
  return {.status = status_from_errno(socket_send ? "sendmsg" : "writev")};
}

/**
 * @brief Closes a file descriptor when it is open.
 * @param fd File descriptor to close.
 * @return Status describing success or failure.
 */
inline Status close_fd(int fd) {
  if (fd < 0) {
    return Status::ok_status();
  }
  if (::close(fd) < 0) {
    return status_from_errno("close");
  }
  return Status::ok_status();
}

/**
 * @brief Applies socket send and receive buffer sizes.
 * @param fd Socket file descriptor to configure.
 * @param send_buffer_bytes Requested send buffer size in bytes.
 * @param receive_buffer_bytes Requested receive buffer size in bytes.
 * @return Status describing success or failure.
 */
inline Status configure_socket_buffers(int fd, int send_buffer_bytes, int receive_buffer_bytes) {
  if (send_buffer_bytes > 0 &&
      ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &send_buffer_bytes, sizeof(send_buffer_bytes)) < 0) {
    return status_from_errno("setsockopt(SO_SNDBUF)");
  }
  if (receive_buffer_bytes > 0 &&
      ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer_bytes, sizeof(receive_buffer_bytes)) < 0) {
    return status_from_errno("setsockopt(SO_RCVBUF)");
  }
  return Status::ok_status();
}

/**
 * @brief Renders the local or peer endpoint for a socket as text.
 * @param fd Socket file descriptor to inspect.
 * @param peer `true` to return the peer endpoint, `false` for the local endpoint.
 * @return Endpoint string when inspection succeeds.
 */
inline std::string socket_endpoint_string(int fd, bool peer) {
  sockaddr_storage address{};
  socklen_t address_length = sizeof(address);
  const int result = peer ? ::getpeername(fd, reinterpret_cast<sockaddr*>(&address), &address_length)
                          : ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &address_length);
  if (result < 0) {
    return {};
  }
  if (address.ss_family == AF_UNIX) {
    const auto* unix_address = reinterpret_cast<const sockaddr_un*>(&address);
    return unix_address->sun_path;
  }
  std::array<char, NI_MAXHOST> host_buffer{};
  std::array<char, NI_MAXSERV> service_buffer{};
  if (::getnameinfo(reinterpret_cast<const sockaddr*>(&address),
                    address_length,
                    host_buffer.data(),
                    host_buffer.size(),
                    service_buffer.data(),
                    service_buffer.size(),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
    return {};
  }
  return std::string(host_buffer.data()) + ":" + service_buffer.data();
}

}  // namespace universal_protocol_runtime::stream_transport_support

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_SRC_ADAPTERS__STREAM_TRANSPORT_SUPPORT_HPP_
