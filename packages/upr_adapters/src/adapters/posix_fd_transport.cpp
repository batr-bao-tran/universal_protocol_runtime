#include "universal_protocol_runtime/adapters/posix_fd_transport.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
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
    return invalid_argument("Cannot wait on a closed file descriptor.");
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

ReadResult read_from_fd(int fd, MutableByteSpan destination, bool* open_flag) {
  while (true) {
    const ssize_t bytes_read = ::read(fd, destination.data(), destination.size());
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
        .status = status_from_errno("read"),
    };
  }
}

}  // namespace

PosixFdTransport::PosixFdTransport(int fd, PosixTransportOptions options)
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

PosixFdTransport::~PosixFdTransport() noexcept { (void)close(); }

PosixFdTransport::PosixFdTransport(PosixFdTransport&& other) noexcept { move_from(&other); }

PosixFdTransport& PosixFdTransport::operator=(PosixFdTransport&& other) noexcept {
  if (this != &other) {
    (void)close();
    move_from(&other);
  }
  return *this;
}

StatusOr<PosixFdTransport> PosixFdTransport::open_device(const std::string& path,
                                                         int open_flags,
                                                         PosixTransportOptions options) {
  const int fd = ::open(path.c_str(), open_flags);
  if (fd < 0) {
    return status_from_errno("open(" + path + ")");
  }
  PosixFdTransport transport(fd, options);
  if (!transport.is_open()) {
    return io_error("Failed to initialize file descriptor transport for '" + path + "'.");
  }
  return transport;
}

ReadResult PosixFdTransport::read(MutableByteSpan destination) {
  if (!open_ || fd_ < 0) {
    return {
        .end_of_stream = true,
    };
  }
  return read_from_fd(fd_, destination, &open_);
}

bool PosixFdTransport::is_open() const { return open_ && fd_ >= 0; }

Status PosixFdTransport::close() {
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

StatusOr<bool> PosixFdTransport::wait_until_readable(int timeout_ms) const {
  return wait_for_readable(fd_, timeout_ms);
}

void PosixFdTransport::move_from(PosixFdTransport* other) noexcept {
  fd_ = other->fd_;
  open_ = other->open_;
  own_handle_ = other->own_handle_;
  other->fd_ = -1;
  other->open_ = false;
  other->own_handle_ = false;
}

}  // namespace universal_protocol_runtime
