#include "universal_protocol_runtime/adapters/tcp_stream_transport.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sstream>
#include <utility>

#include "packages/upr_adapters/src/adapters/stream_transport_support.hpp"

namespace universal_protocol_runtime {
namespace {

using stream_transport_support::close_fd;
using stream_transport_support::configure_socket_buffers;
using stream_transport_support::connect_with_timeout;
using stream_transport_support::send_to_socket;
using stream_transport_support::set_non_blocking;
using stream_transport_support::socket_endpoint_string;
using stream_transport_support::status_from_errno;
using stream_transport_support::wait_for_fd;
using stream_transport_support::writev_to_fd;

Status configure_tcp_socket(int fd, const TcpTransportOptions& options) {
  if (options.non_blocking) {
    const Status status = set_non_blocking(fd);
    if (!status.ok()) {
      return status;
    }
  }
  if (options.tcp_no_delay) {
    int enabled = 1;
    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) < 0) {
      return status_from_errno("setsockopt(TCP_NODELAY)");
    }
  }
  return configure_socket_buffers(fd, options.send_buffer_bytes, options.receive_buffer_bytes);
}

std::string service_string(uint16_t port) {
  std::ostringstream stream;
  stream << port;
  return stream.str();
}

}  // namespace

TcpStreamTransport::TcpStreamTransport(int fd, TcpTransportOptions options)
    : fd_(fd), open_(fd >= 0), own_handle_(options.own_handle) {
  if (fd_ >= 0) {
    const Status status = configure_tcp_socket(fd_, options);
    if (!status.ok()) {
      open_ = false;
      if (own_handle_) {
        (void)close_fd(fd_);
      }
      fd_ = -1;
    }
  }
}

TcpStreamTransport::~TcpStreamTransport() noexcept { (void)close(); }

TcpStreamTransport::TcpStreamTransport(TcpStreamTransport&& other) noexcept { move_from(&other); }

TcpStreamTransport& TcpStreamTransport::operator=(TcpStreamTransport&& other) noexcept {
  if (this != &other) {
    (void)close();
    move_from(&other);
  }
  return *this;
}

StatusOr<TcpStreamTransport> TcpStreamTransport::connect_to_host(const std::string& host,
                                                                 uint16_t port,
                                                                 TcpTransportOptions options) {
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
    const TcpTransportOptions blocking_options = [](TcpTransportOptions original) {
      original.non_blocking = false;
      return original;
    }(options);
    const Status configure_status = configure_tcp_socket(fd, blocking_options);
    if (!configure_status.ok()) {
      (void)close_fd(fd);
      continue;
    }
    if (connect_with_timeout(fd, address->ai_addr, address->ai_addrlen, options.connect_timeout_ms).ok()) {
      connected_fd = fd;
      break;
    }
    (void)close_fd(fd);
  }
  ::freeaddrinfo(addresses);

  if (connected_fd < 0) {
    return io_error("Unable to connect TCP socket to " + host + ":" + service);
  }
  if (options.non_blocking) {
    const Status status = set_non_blocking(connected_fd);
    if (!status.ok()) {
      (void)close_fd(connected_fd);
      return status;
    }
  }
  TcpTransportOptions initialized_options = options;
  initialized_options.non_blocking = false;
  return TcpStreamTransport(connected_fd, initialized_options);
}

ReadResult TcpStreamTransport::read(MutableByteSpan destination) {
  if (!is_open()) {
    return {.end_of_stream = true};
  }
  return stream_transport_support::recv_from_socket(fd_, destination, &open_);
}

WriteResult TcpStreamTransport::write(ByteSpan source) {
  if (!is_open()) {
    return {.status = io_error("Socket is closed.")};
  }
  return send_to_socket(fd_, source);
}

WriteResult TcpStreamTransport::writev(std::span<const ByteSpan> sources) {
  if (!is_open()) {
    return {.status = io_error("Socket is closed.")};
  }
  return writev_to_fd(fd_, sources, true);
}

Status TcpStreamTransport::close() {
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

bool TcpStreamTransport::is_open() const { return open_ && fd_ >= 0; }

StatusOr<bool> TcpStreamTransport::wait_until_readable(int timeout_ms) const {
  return wait_for_fd(fd_, POLLIN, timeout_ms);
}

StatusOr<bool> TcpStreamTransport::wait_until_writable(int timeout_ms) const {
  return wait_for_fd(fd_, POLLOUT, timeout_ms);
}

TransportCapabilityMask TcpStreamTransport::capabilities() const {
  return capability_mask(TransportCapability::kStream);
}

std::string TcpStreamTransport::local_endpoint() const { return socket_endpoint_string(fd_, false); }

std::string TcpStreamTransport::peer_endpoint() const { return socket_endpoint_string(fd_, true); }

void TcpStreamTransport::move_from(TcpStreamTransport* other) noexcept {
  fd_ = other->fd_;
  open_ = other->open_;
  own_handle_ = other->own_handle_;
  other->fd_ = -1;
  other->open_ = false;
  other->own_handle_ = false;
}

TcpListener::~TcpListener() noexcept { (void)close(); }

TcpListener::TcpListener(TcpListener&& other) noexcept { move_from(&other); }

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
  if (this != &other) {
    (void)close();
    move_from(&other);
  }
  return *this;
}

StatusOr<TcpListener> TcpListener::bind_loopback(uint16_t port, TcpTransportOptions options) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return status_from_errno("socket(AF_INET)");
  }
  int reuse_addr = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof(reuse_addr)) < 0) {
    const Status status = status_from_errno("setsockopt(SO_REUSEADDR)");
    (void)close_fd(fd);
    return status;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
    const Status status = status_from_errno("bind(loopback)");
    (void)close_fd(fd);
    return status;
  }
  if (::listen(fd, 32) < 0) {
    const Status status = status_from_errno("listen(loopback)");
    (void)close_fd(fd);
    return status;
  }
  if (options.non_blocking) {
    const Status status = set_non_blocking(fd);
    if (!status.ok()) {
      (void)close_fd(fd);
      return status;
    }
  }
  TcpListener listener;
  listener.fd_ = fd;
  listener.open_ = true;
  listener.options_ = options;
  socklen_t address_length = sizeof(address);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &address_length) == 0) {
    listener.port_ = ntohs(address.sin_port);
  }
  return listener;
}

StatusOr<bool> TcpListener::wait_for_connection(int timeout_ms) const { return wait_for_fd(fd_, POLLIN, timeout_ms); }

StatusOr<std::unique_ptr<IByteStreamTransport>> TcpListener::accept() {
  const int client_fd = ::accept(fd_, nullptr, nullptr);
  if (client_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return not_found("No pending TCP connection.");
    }
    return status_from_errno("accept");
  }
  return std::unique_ptr<IByteStreamTransport>(new TcpStreamTransport(client_fd, options_));
}

Status TcpListener::close() {
  if (fd_ < 0) {
    open_ = false;
    return Status::ok_status();
  }
  const int fd = fd_;
  fd_ = -1;
  open_ = false;
  port_ = 0;
  return close_fd(fd);
}

bool TcpListener::is_open() const { return open_ && fd_ >= 0; }

std::string TcpListener::local_endpoint() const { return std::string("127.0.0.1:") + std::to_string(port_); }

void TcpListener::move_from(TcpListener* other) noexcept {
  fd_ = other->fd_;
  open_ = other->open_;
  port_ = other->port_;
  options_ = other->options_;
  other->fd_ = -1;
  other->open_ = false;
  other->port_ = 0;
}

}  // namespace universal_protocol_runtime
