#include "universal_protocol_runtime/adapters/local_shm_ring_transport.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "packages/upr_adapters/src/adapters/stream_transport_support.hpp"

namespace universal_protocol_runtime {
namespace {

using stream_transport_support::close_fd;
using stream_transport_support::status_from_errno;
using stream_transport_support::wait_for_fd;

constexpr uint32_t kRingMagic = 0x55505252U;

struct alignas(64) RingControl {
  std::atomic<uint32_t> head{0};
  std::atomic<uint32_t> tail{0};
  uint32_t slot_count = 0;
  uint32_t slot_size = 0;
};

struct alignas(64) SlotHeader {
  std::atomic<uint32_t> length{0};
};

struct SharedLayout {
  RingControl* first_control = nullptr;
  RingControl* second_control = nullptr;
  SlotHeader* first_headers = nullptr;
  SlotHeader* second_headers = nullptr;
  std::byte* first_payloads = nullptr;
  std::byte* second_payloads = nullptr;
};

size_t align_up(size_t value, size_t alignment) { return (value + alignment - 1U) & ~(alignment - 1U); }

int create_memfd(const char* name) {
#ifdef SYS_memfd_create
  return static_cast<int>(::syscall(SYS_memfd_create, name, 0));
#else
  (void)name;
  errno = ENOSYS;
  return -1;
#endif
}

void notify_eventfd(int fd) {
  const uint64_t increment = 1;
  (void)::write(fd, &increment, sizeof(increment));
}

void drain_eventfd(int fd) {
  uint64_t value = 0;
  while (::read(fd, &value, sizeof(value)) == sizeof(value)) {
  }
}

bool ring_has_data(const RingControl& control) {
  return control.head.load(std::memory_order_acquire) != control.tail.load(std::memory_order_acquire);
}

bool ring_has_space(const RingControl& control) {
  const uint32_t head = control.head.load(std::memory_order_acquire);
  const uint32_t tail = control.tail.load(std::memory_order_acquire);
  return ((tail + 1U) % control.slot_count) != head;
}

std::byte* slot_payload(std::byte* payload_base, uint32_t slot_size, uint32_t index) {
  return payload_base + (static_cast<ptrdiff_t>(index) * static_cast<ptrdiff_t>(slot_size));
}

}  // namespace

struct LocalShmRingTransport::SharedState {
  void* mapping = nullptr;
  size_t mapping_size = 0;
  int memfd = -1;
  int first_readable_eventfd = -1;
  int first_writable_eventfd = -1;
  int second_readable_eventfd = -1;
  int second_writable_eventfd = -1;
  SharedLayout layout{};

  ~SharedState() noexcept {
    if (mapping != nullptr && mapping != MAP_FAILED) {
      (void)::munmap(mapping, mapping_size);
    }
    (void)close_fd(memfd);
    (void)close_fd(first_readable_eventfd);
    (void)close_fd(first_writable_eventfd);
    (void)close_fd(second_readable_eventfd);
    (void)close_fd(second_writable_eventfd);
  }
};

struct LocalShmRingTransport::DirectionView {
  RingControl* control = nullptr;
  SlotHeader* headers = nullptr;
  std::byte* payloads = nullptr;
  int readable_eventfd = -1;
  int writable_eventfd = -1;
  std::string endpoint;
};

LocalShmRingTransport::LocalShmRingTransport(std::shared_ptr<SharedState> shared,
                                             const DirectionView& write_direction,
                                             const DirectionView& read_direction,
                                             size_t endpoint_id)
    : shared_(std::move(shared)),
      write_direction_(std::make_unique<DirectionView>(write_direction)),
      read_direction_(std::make_unique<DirectionView>(read_direction)),
      endpoint_id_(endpoint_id) {}

LocalShmRingTransport::~LocalShmRingTransport() noexcept { (void)close(); }

LocalShmRingTransport::LocalShmRingTransport(LocalShmRingTransport&& other) noexcept { move_from(&other); }

LocalShmRingTransport& LocalShmRingTransport::operator=(LocalShmRingTransport&& other) noexcept {
  if (this != &other) {
    (void)close();
    move_from(&other);
  }
  return *this;
}

StatusOr<std::pair<LocalShmRingTransport, LocalShmRingTransport>> LocalShmRingTransport::create_pair(
    LocalShmRingOptions options) {
  if (options.slot_count < 2U) {
    return invalid_argument("LocalShmRingTransport requires at least two slots.");
  }
  if (options.slot_size == 0U) {
    return invalid_argument("LocalShmRingTransport requires a non-zero slot size.");
  }

  const size_t ring_headers_bytes = align_up(sizeof(SlotHeader) * options.slot_count, 64U);
  const size_t payload_bytes = align_up(options.slot_count * options.slot_size, 64U);
  const size_t total_bytes = align_up(sizeof(RingControl) * 2U, 64U) + (ring_headers_bytes * 2U) + (payload_bytes * 2U);

  const int memfd = create_memfd("upr_shm_ring");
  if (memfd < 0) {
    return status_from_errno("memfd_create");
  }
  if (::ftruncate(memfd, static_cast<off_t>(total_bytes)) < 0) {
    const Status status = status_from_errno("ftruncate");
    (void)close_fd(memfd);
    return status;
  }
  void* mapping = ::mmap(nullptr, total_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
  if (mapping == MAP_FAILED) {
    const Status status = status_from_errno("mmap");
    (void)close_fd(memfd);
    return status;
  }

  const int first_readable_eventfd = ::eventfd(0, EFD_NONBLOCK);
  const int first_writable_eventfd = ::eventfd(0, EFD_NONBLOCK);
  const int second_readable_eventfd = ::eventfd(0, EFD_NONBLOCK);
  const int second_writable_eventfd = ::eventfd(0, EFD_NONBLOCK);
  if (first_readable_eventfd < 0 || first_writable_eventfd < 0 || second_readable_eventfd < 0 ||
      second_writable_eventfd < 0) {
    const Status status = status_from_errno("eventfd");
    if (mapping != MAP_FAILED) {
      (void)::munmap(mapping, total_bytes);
    }
    (void)close_fd(memfd);
    (void)close_fd(first_readable_eventfd);
    (void)close_fd(first_writable_eventfd);
    (void)close_fd(second_readable_eventfd);
    (void)close_fd(second_writable_eventfd);
    return status;
  }

  auto shared = std::make_shared<SharedState>();
  shared->mapping = mapping;
  shared->mapping_size = total_bytes;
  shared->memfd = memfd;
  shared->first_readable_eventfd = first_readable_eventfd;
  shared->first_writable_eventfd = first_writable_eventfd;
  shared->second_readable_eventfd = second_readable_eventfd;
  shared->second_writable_eventfd = second_writable_eventfd;

  auto* bytes = static_cast<std::byte*>(mapping);
  shared->layout.first_control = reinterpret_cast<RingControl*>(bytes);
  shared->layout.second_control =
      reinterpret_cast<RingControl*>(bytes + (align_up(sizeof(RingControl) * 2U, 64U) / 2U));
  std::byte* cursor = bytes + align_up(sizeof(RingControl) * 2U, 64U);
  shared->layout.first_headers = reinterpret_cast<SlotHeader*>(cursor);
  cursor += ring_headers_bytes;
  shared->layout.second_headers = reinterpret_cast<SlotHeader*>(cursor);
  cursor += ring_headers_bytes;
  shared->layout.first_payloads = cursor;
  cursor += payload_bytes;
  shared->layout.second_payloads = cursor;

  shared->layout.first_control->slot_count = static_cast<uint32_t>(options.slot_count);
  shared->layout.first_control->slot_size = static_cast<uint32_t>(options.slot_size);
  shared->layout.second_control->slot_count = static_cast<uint32_t>(options.slot_count);
  shared->layout.second_control->slot_size = static_cast<uint32_t>(options.slot_size);

  for (size_t index = 0; index < options.slot_count; ++index) {
    shared->layout.first_headers[index].length.store(0, std::memory_order_relaxed);
    shared->layout.second_headers[index].length.store(0, std::memory_order_relaxed);
  }
  DirectionView first_write{
      .control = shared->layout.first_control,
      .headers = shared->layout.first_headers,
      .payloads = shared->layout.first_payloads,
      .readable_eventfd = second_readable_eventfd,
      .writable_eventfd = first_writable_eventfd,
      .endpoint = "shm://endpoint0->endpoint1",
  };
  DirectionView first_read{
      .control = shared->layout.second_control,
      .headers = shared->layout.second_headers,
      .payloads = shared->layout.second_payloads,
      .readable_eventfd = first_readable_eventfd,
      .writable_eventfd = second_writable_eventfd,
      .endpoint = "shm://endpoint1->endpoint0",
  };
  DirectionView second_write{
      .control = shared->layout.second_control,
      .headers = shared->layout.second_headers,
      .payloads = shared->layout.second_payloads,
      .readable_eventfd = first_readable_eventfd,
      .writable_eventfd = second_writable_eventfd,
      .endpoint = "shm://endpoint1->endpoint0",
  };
  DirectionView second_read{
      .control = shared->layout.first_control,
      .headers = shared->layout.first_headers,
      .payloads = shared->layout.first_payloads,
      .readable_eventfd = second_readable_eventfd,
      .writable_eventfd = first_writable_eventfd,
      .endpoint = "shm://endpoint0->endpoint1",
  };

  return std::make_pair(LocalShmRingTransport(shared, std::move(first_write), std::move(first_read), 0U),
                        LocalShmRingTransport(shared, std::move(second_write), std::move(second_read), 1U));
}

ReadResult LocalShmRingTransport::read(MutableByteSpan destination) {
  if (!is_open()) {
    return {.end_of_stream = true};
  }
  if (zero_copy_active_) {
    return {.status = invalid_argument("Cannot read while a zero-copy receive buffer is leased.")};
  }
  RingControl* control = read_direction_->control;
  if (!read_slot_active_) {
    if (!ring_has_data(*control)) {
      return {.would_block = true};
    }
    read_slot_index_ = control->head.load(std::memory_order_acquire);
    read_slot_offset_ = 0;
    read_slot_active_ = true;
  }
  const uint32_t length = read_direction_->headers[read_slot_index_].length.load(std::memory_order_acquire);
  const size_t remaining = length - read_slot_offset_;
  const size_t bytes_to_copy = std::min(remaining, destination.size());
  std::memcpy(destination.data(),
              slot_payload(read_direction_->payloads, control->slot_size, read_slot_index_) + read_slot_offset_,
              bytes_to_copy);
  read_slot_offset_ += bytes_to_copy;
  if (read_slot_offset_ == length) {
    control->head.store((read_slot_index_ + 1U) % control->slot_count, std::memory_order_release);
    read_slot_active_ = false;
    read_slot_offset_ = 0;
    notify_eventfd(read_direction_->writable_eventfd);
    if (!ring_has_data(*control)) {
      drain_eventfd(read_direction_->readable_eventfd);
    }
  }
  return {.bytes_read = bytes_to_copy};
}

WriteResult LocalShmRingTransport::write(ByteSpan source) {
  if (!is_open()) {
    return {.status = io_error("Shared-memory transport is closed.")};
  }
  RingControl* control = write_direction_->control;
  if (!ring_has_space(*control)) {
    return {.would_block = true};
  }
  if (source.size() > control->slot_size) {
    return {.status = exhausted("Frame exceeds shared-memory slot size.")};
  }

  const uint32_t tail = control->tail.load(std::memory_order_acquire);
  std::byte* destination = slot_payload(write_direction_->payloads, control->slot_size, tail);
  std::memcpy(destination, source.data(), source.size());
  write_direction_->headers[tail].length.store(static_cast<uint32_t>(source.size()), std::memory_order_release);
  control->tail.store((tail + 1U) % control->slot_count, std::memory_order_release);
  notify_eventfd(write_direction_->readable_eventfd);
  return {.bytes_written = source.size()};
}

Status LocalShmRingTransport::close() {
  shared_.reset();
  write_direction_.reset();
  read_direction_.reset();
  read_slot_active_ = false;
  zero_copy_active_ = false;
  return Status::ok_status();
}

bool LocalShmRingTransport::is_open() const {
  return shared_ != nullptr && write_direction_ != nullptr && read_direction_ != nullptr;
}

int LocalShmRingTransport::native_handle() const { return shared_ == nullptr ? -1 : shared_->memfd; }

StatusOr<bool> LocalShmRingTransport::wait_until_readable(int timeout_ms) const {
  if (!is_open()) {
    return invalid_argument("Shared-memory transport is closed.");
  }
  if (ring_has_data(*read_direction_->control)) {
    return true;
  }
  return wait_for_fd(read_direction_->readable_eventfd, POLLIN, timeout_ms);
}

StatusOr<bool> LocalShmRingTransport::wait_until_writable(int timeout_ms) const {
  if (!is_open()) {
    return invalid_argument("Shared-memory transport is closed.");
  }
  if (ring_has_space(*write_direction_->control)) {
    return true;
  }
  return wait_for_fd(write_direction_->writable_eventfd, POLLIN, timeout_ms);
}

TransportCapabilityMask LocalShmRingTransport::capabilities() const {
  return capability_mask(TransportCapability::kSharedMemory) | TransportCapability::kZeroCopyReceive |
         TransportCapability::kPreservesFrameBoundaries;
}

std::string LocalShmRingTransport::local_endpoint() const { return "shm://endpoint" + std::to_string(endpoint_id_); }

std::string LocalShmRingTransport::peer_endpoint() const {
  return "shm://endpoint" + std::to_string(1U - endpoint_id_);
}

StatusOr<TransportBufferLease> LocalShmRingTransport::try_acquire_receive_buffer() {
  if (!is_open()) {
    return invalid_argument("Shared-memory transport is closed.");
  }
  if (read_slot_active_) {
    return invalid_argument("Cannot acquire zero-copy frame while a copied read is in progress.");
  }
  if (zero_copy_active_) {
    return invalid_argument("A zero-copy frame is already leased.");
  }
  RingControl* control = read_direction_->control;
  if (!ring_has_data(*control)) {
    return not_found("No frame is ready in shared memory.");
  }
  const uint32_t head = control->head.load(std::memory_order_acquire);
  const uint32_t length = read_direction_->headers[head].length.load(std::memory_order_acquire);
  zero_copy_active_ = true;
  read_slot_index_ = head;
  return TransportBufferLease{
      .bytes = ByteSpan(slot_payload(read_direction_->payloads, control->slot_size, head), length),
      .token = head,
      .valid = true,
  };
}

Status LocalShmRingTransport::release_receive_buffer(const TransportBufferLease& lease) {
  if (!zero_copy_active_) {
    return invalid_argument("No zero-copy frame is currently leased.");
  }
  RingControl* control = read_direction_->control;
  const uint32_t head = control->head.load(std::memory_order_acquire);
  if (!lease.valid || lease.token != head) {
    return invalid_argument("Zero-copy frame lease does not match the active shared-memory slot.");
  }
  control->head.store((head + 1U) % control->slot_count, std::memory_order_release);
  zero_copy_active_ = false;
  notify_eventfd(read_direction_->writable_eventfd);
  if (!ring_has_data(*control)) {
    drain_eventfd(read_direction_->readable_eventfd);
  }
  return Status::ok_status();
}

void LocalShmRingTransport::move_from(LocalShmRingTransport* other) noexcept {
  shared_ = std::move(other->shared_);
  write_direction_ = std::move(other->write_direction_);
  read_direction_ = std::move(other->read_direction_);
  endpoint_id_ = other->endpoint_id_;
  read_slot_index_ = other->read_slot_index_;
  read_slot_offset_ = other->read_slot_offset_;
  read_slot_active_ = other->read_slot_active_;
  zero_copy_active_ = other->zero_copy_active_;
  other->endpoint_id_ = 0;
  other->read_slot_index_ = 0;
  other->read_slot_offset_ = 0;
  other->read_slot_active_ = false;
  other->zero_copy_active_ = false;
}

}  // namespace universal_protocol_runtime
