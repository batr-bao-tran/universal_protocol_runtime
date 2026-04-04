#include "universal_protocol_runtime/adapters/posix_socket_transport.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>

namespace universal_protocol_runtime {
namespace {

Status status_from_errno(const std::string& operation) {
  return io_error(operation + ": " + std::string(std::strerror(errno)));
}

Status set_non_blocking(int fd, bool enabled) {
  const int existing_flags = ::fcntl(fd, F_GETFL, 0);
  if (existing_flags < 0) {
    return status_from_errno("fcntl(F_GETFL)");
  }
  int next_flags = existing_flags;
  if (enabled) {
    next_flags |= O_NONBLOCK;
  } else {
    next_flags &= ~O_NONBLOCK;
  }
  if (::fcntl(fd, F_SETFL, next_flags) < 0) {
    return status_from_errno("fcntl(F_SETFL)");
  }
  return Status::ok_status();
}

StatusOr<bool> wait_for_readable(int fd, int timeout_ms) {
  if (fd < 0) {
    return invalid_argument("Cannot wait on a closed socket.");
  }
  struct pollfd descriptor {
    .fd = fd, .events = POLLIN, .revents = 0,
  };
  while (true) {
    const int result = ::poll(&descriptor, 1, timeout_ms);
    if (result > 0) {
      return true;
    }
    if (result == 0) {
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    return status_from_errno("poll");
  }
}

ReadResult read_from_socket(int fd, MutableByteSpan destination, bool* open_flag) {
  while (true) {
    const ssize_t bytes_read = ::recv(fd, destination.data(), destination.size(), 0);
    if (bytes_read > 0) {
      return {
          .bytes_read = static_cast<size_t>(bytes_read),
      };
    }
    if (bytes_read == 0) {
      if (open_flag != nullptr) {
        *open_flag = false;
      }
      return {
          .end_of_stream = true,
      };
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return {
          .would_block = true,
      };
    }
    return {
        .status = status_from_errno("recv"),
    };
  }
}

std::string service_string(uint16_t port) {
  std::ostringstream stream;
  stream << port;
  return stream.str();
}

}  // namespace

PosixSocketTransport::PosixSocketTransport(int fd, PosixTransportOptions options)
    : fd_(fd), open_(fd >= 0), own_handle_(options.own_handle) {
  if (fd_ >= 0 && options.non_blocking) {
    const Status status = set_non_blocking(fd_, true);
    if (!status.ok()) {
      open_ = false;
      if (own_handle_) {
        ::close(fd_);
      }
      fd_ = -1;
    }
  }
}

PosixSocketTransport::~PosixSocketTransport() noexcept { (void)close(); }

PosixSocketTransport::PosixSocketTransport(PosixSocketTransport&& other) noexcept { move_from(&other); }

PosixSocketTransport& PosixSocketTransport::operator=(PosixSocketTransport&& other) noexcept {
  if (this != &other) {
    (void)close();
    move_from(&other);
  }
  return *this;
}

StatusOr<PosixSocketTransport> PosixSocketTransport::connect_tcp(const std::string& host,
                                                                 uint16_t port,
                                                                 TcpClientOptions options) {
  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo* addresses = nullptr;
  const std::string service = service_string(port);
  const int lookup_status = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
  if (lookup_status != 0) {
    return io_error("getaddrinfo(" + host + "): " + std::string(::gai_strerror(lookup_status)));
  }

  int connected_fd = -1;
  for (struct addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
    const int fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (fd < 0) {
      continue;
    }
    if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
      connected_fd = fd;
      break;
    }
    ::close(fd);
  }
  ::freeaddrinfo(addresses);

  if (connected_fd < 0) {
    return io_error("Unable to connect TCP socket to " + host + ":" + service);
  }

  PosixSocketTransport transport(connected_fd, {.own_handle = options.own_handle});
  if (options.non_blocking_after_connect) {
    const Status status = set_non_blocking(connected_fd, true);
    if (!status.ok()) {
      (void)transport.close();
      return status;
    }
  }
  return transport;
}

ReadResult PosixSocketTransport::read(MutableByteSpan destination) {
  if (!open_ || fd_ < 0) {
    return {
        .end_of_stream = true,
    };
  }
  return read_from_socket(fd_, destination, &open_);
}

bool PosixSocketTransport::is_open() const { return open_ && fd_ >= 0; }

Status PosixSocketTransport::close() {
  if (fd_ < 0) {
    open_ = false;
    return Status::ok_status();
  }
  const int fd = fd_;
  fd_ = -1;
  const bool close_handle = own_handle_;
  open_ = false;
  own_handle_ = false;
  if (!close_handle) {
    return Status::ok_status();
  }
  if (::close(fd) < 0) {
    return status_from_errno("close");
  }
  return Status::ok_status();
}

StatusOr<bool> PosixSocketTransport::wait_until_readable(int timeout_ms) const {
  return wait_for_readable(fd_, timeout_ms);
}

void PosixSocketTransport::move_from(PosixSocketTransport* other) noexcept {
  fd_ = other->fd_;
  open_ = other->open_;
  own_handle_ = other->own_handle_;
  other->fd_ = -1;
  other->open_ = false;
  other->own_handle_ = false;
}

}  // namespace universal_protocol_runtime
