#include "universal_protocol_runtime/adapters/unix_socket_transport.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <utility>

#include "packages/upr_adapters/src/adapters/stream_transport_support.hpp"

namespace universal_protocol_runtime {
namespace {

using stream_transport_support::close_fd;
using stream_transport_support::configure_socket_buffers;
using stream_transport_support::send_to_socket;
using stream_transport_support::set_non_blocking;
using stream_transport_support::socket_endpoint_string;
using stream_transport_support::status_from_errno;
using stream_transport_support::wait_for_fd;
using stream_transport_support::writev_to_fd;

Status initialize_socket(int fd, const SocketTransportOptions& options) {
  if (fd < 0) {
    return invalid_argument("Socket handle is invalid.");
  }
  if (options.non_blocking) {
    const Status status = set_non_blocking(fd);
    if (!status.ok()) {
      return status;
    }
  }
  return configure_socket_buffers(fd, options.send_buffer_bytes, options.receive_buffer_bytes);
}

sockaddr_un make_unix_address(const std::string& path) {
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", path.c_str());
  return address;
}

}  // namespace

UnixSocketTransport::UnixSocketTransport(int fd, SocketTransportOptions options)
    : fd_(fd), open_(fd >= 0), own_handle_(options.own_handle) {
  if (fd_ >= 0) {
    const Status status = initialize_socket(fd_, options);
    if (!status.ok()) {
      open_ = false;
      if (own_handle_) {
        (void)close_fd(fd_);
      }
      fd_ = -1;
    }
  }
}

UnixSocketTransport::~UnixSocketTransport() noexcept { (void)close(); }

UnixSocketTransport::UnixSocketTransport(UnixSocketTransport&& other) noexcept { move_from(&other); }

UnixSocketTransport& UnixSocketTransport::operator=(UnixSocketTransport&& other) noexcept {
  if (this != &other) {
    (void)close();
    move_from(&other);
  }
  return *this;
}

StatusOr<UnixSocketTransport> UnixSocketTransport::connect_to_path(const std::string& path,
                                                                   SocketTransportOptions options) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return status_from_errno("socket(AF_UNIX)");
  }
  const sockaddr_un address = make_unix_address(path);
  if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
    const Status status = status_from_errno("connect(" + path + ")");
    (void)close_fd(fd);
    return status;
  }
  return UnixSocketTransport(fd, options);
}

StatusOr<std::pair<UnixSocketTransport, UnixSocketTransport>> UnixSocketTransport::create_socket_pair(
    SocketTransportOptions options) {
  std::array<int, 2> fds = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds.data()) < 0) {
    return status_from_errno("socketpair(AF_UNIX)");
  }
  return std::make_pair(UnixSocketTransport(fds[0], options), UnixSocketTransport(fds[1], options));
}

ReadResult UnixSocketTransport::read(MutableByteSpan destination) {
  if (!is_open()) {
    return {.end_of_stream = true};
  }
  return stream_transport_support::recv_from_socket(fd_, destination, &open_);
}

WriteResult UnixSocketTransport::write(ByteSpan source) {
  if (!is_open()) {
    return {.status = io_error("Socket is closed.")};
  }
  return send_to_socket(fd_, source);
}

WriteResult UnixSocketTransport::writev(std::span<const ByteSpan> sources) {
  if (!is_open()) {
    return {.status = io_error("Socket is closed.")};
  }
  return writev_to_fd(fd_, sources, true);
}

Status UnixSocketTransport::close() {
  if (fd_ < 0) {
    open_ = false;
    return Status::ok_status();
  }
  const int fd = fd_;
  const bool own_handle = own_handle_;
  fd_ = -1;
  open_ = false;
  own_handle_ = false;
  if (!own_handle) {
    return Status::ok_status();
  }
  return close_fd(fd);
}

bool UnixSocketTransport::is_open() const { return open_ && fd_ >= 0; }

StatusOr<bool> UnixSocketTransport::wait_until_readable(int timeout_ms) const {
  return wait_for_fd(fd_, POLLIN, timeout_ms);
}

StatusOr<bool> UnixSocketTransport::wait_until_writable(int timeout_ms) const {
  return wait_for_fd(fd_, POLLOUT, timeout_ms);
}

TransportCapabilityMask UnixSocketTransport::capabilities() const {
  return capability_mask(TransportCapability::kStream);
}

std::string UnixSocketTransport::local_endpoint() const { return socket_endpoint_string(fd_, false); }

std::string UnixSocketTransport::peer_endpoint() const { return socket_endpoint_string(fd_, true); }

void UnixSocketTransport::move_from(UnixSocketTransport* other) noexcept {
  fd_ = other->fd_;
  open_ = other->open_;
  own_handle_ = other->own_handle_;
  other->fd_ = -1;
  other->open_ = false;
  other->own_handle_ = false;
}

UnixSocketListener::~UnixSocketListener() noexcept { (void)close(); }

UnixSocketListener::UnixSocketListener(UnixSocketListener&& other) noexcept { move_from(&other); }

UnixSocketListener& UnixSocketListener::operator=(UnixSocketListener&& other) noexcept {
  if (this != &other) {
    (void)close();
    move_from(&other);
  }
  return *this;
}

StatusOr<UnixSocketListener> UnixSocketListener::bind_path(const std::string& path, SocketTransportOptions options) {
  const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return status_from_errno("socket(AF_UNIX)");
  }
  const sockaddr_un address = make_unix_address(path);
  (void)::unlink(path.c_str());
  if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
    const Status status = status_from_errno("bind(" + path + ")");
    (void)close_fd(fd);
    return status;
  }
  if (::listen(fd, 16) < 0) {
    const Status status = status_from_errno("listen(" + path + ")");
    (void)::unlink(path.c_str());
    (void)close_fd(fd);
    return status;
  }
  if (options.non_blocking) {
    const Status status = set_non_blocking(fd);
    if (!status.ok()) {
      (void)::unlink(path.c_str());
      (void)close_fd(fd);
      return status;
    }
  }
  UnixSocketListener listener;
  listener.fd_ = fd;
  listener.open_ = true;
  listener.path_ = path;
  listener.options_ = options;
  return listener;
}

StatusOr<bool> UnixSocketListener::wait_for_connection(int timeout_ms) const {
  return wait_for_fd(fd_, POLLIN, timeout_ms);
}

StatusOr<std::unique_ptr<IByteStreamTransport>> UnixSocketListener::accept() {
  const int client_fd = ::accept(fd_, nullptr, nullptr);
  if (client_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return not_found("No pending Unix socket connection.");
    }
    return status_from_errno("accept");
  }
  return std::unique_ptr<IByteStreamTransport>(new UnixSocketTransport(client_fd, options_));
}

Status UnixSocketListener::close() {
  if (fd_ < 0) {
    open_ = false;
    return Status::ok_status();
  }
  const int fd = fd_;
  fd_ = -1;
  open_ = false;
  const std::string path = path_;
  path_.clear();
  const Status close_status = close_fd(fd);
  if (!path.empty()) {
    (void)::unlink(path.c_str());
  }
  return close_status;
}

bool UnixSocketListener::is_open() const { return open_ && fd_ >= 0; }

void UnixSocketListener::move_from(UnixSocketListener* other) noexcept {
  fd_ = other->fd_;
  open_ = other->open_;
  path_ = std::move(other->path_);
  options_ = other->options_;
  other->fd_ = -1;
  other->open_ = false;
  other->path_.clear();
}

}  // namespace universal_protocol_runtime
