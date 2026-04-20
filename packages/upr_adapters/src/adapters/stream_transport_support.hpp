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

inline Status status_from_errno(const std::string& operation) {
  return io_error(operation + ": " + std::string(std::strerror(errno)));
}

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

inline Status close_fd(int fd) {
  if (fd < 0) {
    return Status::ok_status();
  }
  if (::close(fd) < 0) {
    return status_from_errno("close");
  }
  return Status::ok_status();
}

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
