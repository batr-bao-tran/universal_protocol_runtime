#include "universal_protocol_runtime/adapters/io_uring_reactor.hpp"

#include <fcntl.h>
#include <liburing.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "packages/upr_adapters/src/adapters/stream_transport_support.hpp"

namespace universal_protocol_runtime {
namespace {

using stream_transport_support::close_fd;
using stream_transport_support::status_from_errno;

Status ensure_non_blocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return status_from_errno("fcntl(F_GETFL)");
  }
  if ((flags & O_NONBLOCK) != 0) {
    return Status::ok_status();
  }
  if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return status_from_errno("fcntl(F_SETFL)");
  }
  return Status::ok_status();
}

Status status_from_uring_errno(const std::string& operation, int result) {
  return io_error(operation + ": " + std::string(std::strerror(-result)));
}

bool is_kernel_io_uring_supported() {
  io_uring ring{};
  const int result = ::io_uring_queue_init(2, &ring, 0);
  if (result < 0) {
    return false;
  }
  ::io_uring_queue_exit(&ring);
  return true;
}

struct IoUringAvailabilitySnapshot {
  IoUringAvailability availability;
  std::string_view reason;
};

IoUringAvailabilitySnapshot detect_io_uring_availability() {
  if (is_kernel_io_uring_supported()) {
    return {
        .availability = IoUringAvailability::kAvailable,
        .reason = "io_uring is available.",
    };
  }
  return {
      .availability = IoUringAvailability::kUnavailable,
      .reason = "io_uring is unavailable on this kernel or process.",
  };
}

const IoUringAvailabilitySnapshot& cached_io_uring_availability() {
  static const IoUringAvailabilitySnapshot kSnapshot = detect_io_uring_availability();
  return kSnapshot;
}

__kernel_timespec make_timeout(int timeout_ms) {
  return {
      .tv_sec = timeout_ms / 1000,
      .tv_nsec = static_cast<long>((timeout_ms % 1000) * 1000000L),
  };
}

struct CompletionSnapshot {
  uint64_t user_data = 0;
  int result = 0;
  unsigned flags = 0;
};

CompletionSnapshot snapshot_cqe(const io_uring_cqe& cqe) {
  return {
      .user_data = cqe.user_data,
      .result = cqe.res,
      .flags = cqe.flags,
  };
}

}  // namespace

struct IoUringStreamEngine::Impl {
  io_uring ring{};
  int fd = -1;
  bool open = false;
  bool own_handle = true;
  bool use_send_zerocopy = false;
  size_t send_zerocopy_threshold_bytes = 16384;
  std::string local;
  std::string peer;
  uint64_t next_user_data = 1;
};

IoUringAvailability IoUringReactor::availability() { return cached_io_uring_availability().availability; }

bool IoUringReactor::is_supported() {
  return cached_io_uring_availability().availability == IoUringAvailability::kAvailable;
}

std::string_view IoUringReactor::reason() { return cached_io_uring_availability().reason; }

IoUringStreamEngine::IoUringStreamEngine(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

IoUringStreamEngine::~IoUringStreamEngine() noexcept { (void)close(); }

IoUringStreamEngine::IoUringStreamEngine(IoUringStreamEngine&& other) noexcept { move_from(&other); }

IoUringStreamEngine& IoUringStreamEngine::operator=(IoUringStreamEngine&& other) noexcept {
  if (this != &other) {
    (void)close();
    move_from(&other);
  }
  return *this;
}

StatusOr<std::unique_ptr<IByteStreamEngine>> IoUringStreamEngine::create(int fd,
                                                                         std::string local_endpoint,
                                                                         std::string peer_endpoint,
                                                                         IoUringOptions options) {
  if (fd < 0) {
    return invalid_argument("Cannot create io_uring engine from an invalid file descriptor.");
  }
  if (!IoUringReactor::is_supported()) {
    return not_found(std::string(IoUringReactor::reason()));
  }
  const Status non_blocking_status = ensure_non_blocking(fd);
  if (!non_blocking_status.ok()) {
    if (options.own_handle) {
      (void)close_fd(fd);
    }
    return non_blocking_status;
  }

  auto impl = std::make_unique<Impl>();
  io_uring_params params{};
  if (options.setup_sqpoll) {
    params.flags |= IORING_SETUP_SQPOLL;
    params.sq_thread_idle = options.sq_thread_idle_ms;
  }
  const int init_result = ::io_uring_queue_init_params(options.queue_depth, &impl->ring, &params);
  if (init_result < 0) {
    if (options.own_handle) {
      (void)close_fd(fd);
    }
    return status_from_uring_errno("io_uring_queue_init_params", init_result);
  }
  impl->fd = fd;
  impl->open = true;
  impl->own_handle = options.own_handle;
  impl->use_send_zerocopy = options.use_send_zerocopy;
  impl->send_zerocopy_threshold_bytes = options.send_zerocopy_threshold_bytes;
  impl->local = std::move(local_endpoint);
  impl->peer = std::move(peer_endpoint);
  return std::unique_ptr<IByteStreamEngine>(new IoUringStreamEngine(std::move(impl)));
}

ReadResult IoUringStreamEngine::read(MutableByteSpan destination) {
  if (!is_open()) {
    return {.end_of_stream = true};
  }
  io_uring_sqe* sqe = ::io_uring_get_sqe(&impl_->ring);
  if (sqe == nullptr) {
    return {.status = exhausted("io_uring submission queue is full.")};
  }
  ::io_uring_prep_recv(sqe, impl_->fd, destination.data(), destination.size(), 0);
  const int submit_result = ::io_uring_submit(&impl_->ring);
  if (submit_result < 0) {
    return {.status = status_from_uring_errno("io_uring_submit(recv)", submit_result)};
  }
  io_uring_cqe* cqe = nullptr;
  const int wait_result = ::io_uring_wait_cqe(&impl_->ring, &cqe);
  if (wait_result < 0) {
    return {.status = status_from_uring_errno("io_uring_wait_cqe(recv)", wait_result)};
  }
  const int result = cqe->res;
  ::io_uring_cqe_seen(&impl_->ring, cqe);
  if (result > 0) {
    return {.bytes_read = static_cast<size_t>(result)};
  }
  if (result == 0) {
    impl_->open = false;
    return {.end_of_stream = true};
  }
  if (result == -EAGAIN || result == -EWOULDBLOCK) {
    return {.would_block = true};
  }
  return {.status = status_from_uring_errno("recv", result)};
}

WriteResult IoUringStreamEngine::write(ByteSpan source) {
  if (!is_open()) {
    return {.status = io_error("io_uring stream engine is closed.")};
  }
  io_uring_sqe* sqe = ::io_uring_get_sqe(&impl_->ring);
  if (sqe == nullptr) {
    return {.status = exhausted("io_uring submission queue is full.")};
  }
  if (impl_->use_send_zerocopy && source.size() >= impl_->send_zerocopy_threshold_bytes) {
    ::io_uring_prep_send_zc(sqe, impl_->fd, source.data(), source.size(), MSG_NOSIGNAL, 0U);
  } else {
    ::io_uring_prep_send(sqe, impl_->fd, source.data(), source.size(), MSG_NOSIGNAL);
  }
  const int submit_result = ::io_uring_submit(&impl_->ring);
  if (submit_result < 0) {
    return {.status = status_from_uring_errno("io_uring_submit(send)", submit_result)};
  }
  io_uring_cqe* cqe = nullptr;
  const int wait_result = ::io_uring_wait_cqe(&impl_->ring, &cqe);
  if (wait_result < 0) {
    return {.status = status_from_uring_errno("io_uring_wait_cqe(send)", wait_result)};
  }
  const int result = cqe->res;
  ::io_uring_cqe_seen(&impl_->ring, cqe);
  if (result >= 0) {
    return {.bytes_written = static_cast<size_t>(result)};
  }
  if (result == -EAGAIN || result == -EWOULDBLOCK) {
    return {.would_block = true};
  }
  return {.status = status_from_uring_errno("send", result)};
}

WriteResult IoUringStreamEngine::writev(std::span<const ByteSpan> sources) {
  if (!is_open()) {
    return {.status = io_error("io_uring stream engine is closed.")};
  }
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
  struct msghdr message {};
  message.msg_iov = iovecs.data();
  message.msg_iovlen = iovecs.size();

  io_uring_sqe* sqe = ::io_uring_get_sqe(&impl_->ring);
  if (sqe == nullptr) {
    return {.status = exhausted("io_uring submission queue is full.")};
  }
  ::io_uring_prep_sendmsg(sqe, impl_->fd, &message, MSG_NOSIGNAL);
  const int submit_result = ::io_uring_submit(&impl_->ring);
  if (submit_result < 0) {
    return {.status = status_from_uring_errno("io_uring_submit(sendmsg)", submit_result)};
  }
  io_uring_cqe* cqe = nullptr;
  const int wait_result = ::io_uring_wait_cqe(&impl_->ring, &cqe);
  if (wait_result < 0) {
    return {.status = status_from_uring_errno("io_uring_wait_cqe(sendmsg)", wait_result)};
  }
  const int result = cqe->res;
  ::io_uring_cqe_seen(&impl_->ring, cqe);
  if (result >= 0) {
    return {.bytes_written = static_cast<size_t>(result)};
  }
  if (result == -EAGAIN || result == -EWOULDBLOCK) {
    return {.would_block = true};
  }
  return {.status = status_from_uring_errno("sendmsg", result)};
}

Status IoUringStreamEngine::close() {
  if (impl_ == nullptr) {
    return Status::ok_status();
  }
  auto owned = std::move(impl_);
  if (owned->open) {
    ::io_uring_queue_exit(&owned->ring);
  }
  owned->open = false;
  if (owned->own_handle) {
    return close_fd(owned->fd);
  }
  owned->fd = -1;
  return Status::ok_status();
}

bool IoUringStreamEngine::is_open() const { return impl_ != nullptr && impl_->open && impl_->fd >= 0; }

int IoUringStreamEngine::native_handle() const { return impl_ == nullptr ? -1 : impl_->fd; }

StatusOr<bool> IoUringStreamEngine::wait_until_readable(int timeout_ms) const {
  if (!is_open()) {
    return invalid_argument("io_uring stream engine is closed.");
  }
  const uint64_t poll_tag = impl_->next_user_data++;
  io_uring_sqe* poll_sqe = ::io_uring_get_sqe(&impl_->ring);
  if (poll_sqe == nullptr) {
    return exhausted("io_uring submission queue is full.");
  }
  ::io_uring_prep_poll_add(poll_sqe, impl_->fd, POLLIN);
  ::io_uring_sqe_set_data64(poll_sqe, poll_tag);

  uint64_t timeout_tag = 0;
  if (timeout_ms >= 0) {
    timeout_tag = impl_->next_user_data++;
    io_uring_sqe* timeout_sqe = ::io_uring_get_sqe(&impl_->ring);
    if (timeout_sqe == nullptr) {
      return exhausted("io_uring submission queue is full.");
    }
    __kernel_timespec timeout = make_timeout(timeout_ms);
    ::io_uring_prep_link_timeout(timeout_sqe, &timeout, 0);
    poll_sqe->flags |= IOSQE_IO_LINK;
    ::io_uring_sqe_set_data64(timeout_sqe, timeout_tag);
  }

  const int submit_result = ::io_uring_submit(&impl_->ring);
  if (submit_result < 0) {
    return status_from_uring_errno("io_uring_submit(poll read)", submit_result);
  }

  bool saw_poll = false;
  bool saw_timeout = timeout_ms < 0;
  bool ready = false;
  while (!saw_poll || !saw_timeout) {
    io_uring_cqe* cqe = nullptr;
    const int wait_result = ::io_uring_wait_cqe(&impl_->ring, &cqe);
    if (wait_result < 0) {
      return status_from_uring_errno("io_uring_wait_cqe(poll read)", wait_result);
    }
    const CompletionSnapshot completion = snapshot_cqe(*cqe);
    ::io_uring_cqe_seen(&impl_->ring, cqe);
    if (completion.user_data == poll_tag) {
      saw_poll = true;
      if (completion.result >= 0) {
        ready = true;
      } else if (completion.result != -ECANCELED) {
        return status_from_uring_errno("poll(POLLIN)", completion.result);
      }
      continue;
    }
    if (completion.user_data == timeout_tag) {
      saw_timeout = true;
      if (completion.result != -ECANCELED && completion.result != -ETIME && completion.result != -ENOENT) {
        return status_from_uring_errno("link_timeout(POLLIN)", completion.result);
      }
      continue;
    }
    return io_error("Unexpected io_uring completion while waiting for readability.");
  }
  return ready;
}

StatusOr<bool> IoUringStreamEngine::wait_until_writable(int timeout_ms) const {
  if (!is_open()) {
    return invalid_argument("io_uring stream engine is closed.");
  }
  const uint64_t poll_tag = impl_->next_user_data++;
  io_uring_sqe* poll_sqe = ::io_uring_get_sqe(&impl_->ring);
  if (poll_sqe == nullptr) {
    return exhausted("io_uring submission queue is full.");
  }
  ::io_uring_prep_poll_add(poll_sqe, impl_->fd, POLLOUT);
  ::io_uring_sqe_set_data64(poll_sqe, poll_tag);

  uint64_t timeout_tag = 0;
  if (timeout_ms >= 0) {
    timeout_tag = impl_->next_user_data++;
    io_uring_sqe* timeout_sqe = ::io_uring_get_sqe(&impl_->ring);
    if (timeout_sqe == nullptr) {
      return exhausted("io_uring submission queue is full.");
    }
    __kernel_timespec timeout = make_timeout(timeout_ms);
    ::io_uring_prep_link_timeout(timeout_sqe, &timeout, 0);
    poll_sqe->flags |= IOSQE_IO_LINK;
    ::io_uring_sqe_set_data64(timeout_sqe, timeout_tag);
  }

  const int submit_result = ::io_uring_submit(&impl_->ring);
  if (submit_result < 0) {
    return status_from_uring_errno("io_uring_submit(poll write)", submit_result);
  }

  bool saw_poll = false;
  bool saw_timeout = timeout_ms < 0;
  bool ready = false;
  while (!saw_poll || !saw_timeout) {
    io_uring_cqe* cqe = nullptr;
    const int wait_result = ::io_uring_wait_cqe(&impl_->ring, &cqe);
    if (wait_result < 0) {
      return status_from_uring_errno("io_uring_wait_cqe(poll write)", wait_result);
    }
    const CompletionSnapshot completion = snapshot_cqe(*cqe);
    ::io_uring_cqe_seen(&impl_->ring, cqe);
    if (completion.user_data == poll_tag) {
      saw_poll = true;
      if (completion.result >= 0) {
        ready = true;
      } else if (completion.result != -ECANCELED) {
        return status_from_uring_errno("poll(POLLOUT)", completion.result);
      }
      continue;
    }
    if (completion.user_data == timeout_tag) {
      saw_timeout = true;
      if (completion.result != -ECANCELED && completion.result != -ETIME && completion.result != -ENOENT) {
        return status_from_uring_errno("link_timeout(POLLOUT)", completion.result);
      }
      continue;
    }
    return io_error("Unexpected io_uring completion while waiting for writability.");
  }
  return ready;
}

TransportCapabilityMask IoUringStreamEngine::capabilities() const {
  TransportCapabilityMask mask = capability_mask(TransportCapability::kStream) | TransportCapability::kKernelBatching;
  if (impl_ != nullptr && impl_->use_send_zerocopy) {
    mask = mask | TransportCapability::kZeroCopySend;
  }
  return mask;
}

std::string IoUringStreamEngine::local_endpoint() const { return impl_ == nullptr ? std::string() : impl_->local; }

std::string IoUringStreamEngine::peer_endpoint() const { return impl_ == nullptr ? std::string() : impl_->peer; }

Status IoUringStreamEngine::shutdown_read() {
  if (!is_open()) {
    return Status::ok_status();
  }
  if (::shutdown(impl_->fd, SHUT_RD) < 0) {
    return status_from_errno("shutdown(SHUT_RD)");
  }
  return Status::ok_status();
}

Status IoUringStreamEngine::shutdown_write() {
  if (!is_open()) {
    return Status::ok_status();
  }
  if (::shutdown(impl_->fd, SHUT_WR) < 0) {
    return status_from_errno("shutdown(SHUT_WR)");
  }
  return Status::ok_status();
}

void IoUringStreamEngine::move_from(IoUringStreamEngine* other) noexcept { impl_ = std::move(other->impl_); }

}  // namespace universal_protocol_runtime
