#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_RUNTIME__BYTE_RING_BUFFER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_RUNTIME__BYTE_RING_BUFFER_HPP_
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <span>

#include "universal_protocol_runtime/core/byte_view.hpp"

namespace universal_protocol_runtime {

#if defined(__cpp_lib_hardware_interference_size)
static constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
static constexpr std::size_t kCacheLine = 64;
#endif

template <size_t Capacity>
class ByteRingBuffer {
 public:
  static_assert(Capacity > 1);

  ~ByteRingBuffer() noexcept = default;

  static constexpr size_t capacity() { return Capacity - 1; }

  size_t size() const {
    const size_t head = head_;
    const size_t tail = tail_;
    if (tail >= head) {
      return tail - head;
    }
    return Capacity - head + tail;
  }

  size_t free_space() const { return capacity() - size(); }

  bool empty() const { return size() == 0; }

  bool has_wrapped_readable_data() const {
    const size_t head = head_;
    const size_t tail = tail_;
    return !empty() && tail <= head;
  }

  MutableByteSpan writable_span() {
    if (free_space() == 0) {
      return {};
    }
    const size_t head = head_;
    const size_t tail = tail_;
    if (tail >= head) {
      const size_t limit = head == 0 ? Capacity - 1 : Capacity;
      return {storage_.data() + tail, std::min(limit - tail, free_space())};
    }
    return {storage_.data() + tail, head - tail - 1};
  }

  void commit_write(size_t bytes_written) {
    assert(bytes_written <= writable_span().size());
    tail_ = (tail_ + bytes_written) % Capacity;
  }

  ByteSpan readable_span() const {
    if (empty()) {
      return {};
    }
    const size_t head = head_;
    const size_t tail = tail_;
    if (tail > head) {
      return {storage_.data() + head, tail - head};
    }
    return {storage_.data() + head, Capacity - head};
  }

  void consume(size_t bytes_consumed) {
    assert(bytes_consumed <= size());
    head_ = (head_ + bytes_consumed) % Capacity;
  }

  size_t linearize(MutableByteSpan destination) const {
    const size_t bytes_to_copy = std::min(size(), destination.size());
    const ByteSpan first = readable_span();
    const size_t first_copy = std::min(bytes_to_copy, first.size());
    std::copy_n(first.begin(), first_copy, destination.begin());
    if (first_copy == bytes_to_copy) {
      return bytes_to_copy;
    }
    const size_t second_copy = bytes_to_copy - first_copy;
    std::copy_n(storage_.begin(), second_copy, destination.begin() + static_cast<ptrdiff_t>(first_copy));
    return bytes_to_copy;
  }

 private:
  alignas(kCacheLine) size_t head_ = 0;
  alignas(kCacheLine) size_t tail_ = 0;
  alignas(kCacheLine) std::array<std::byte, Capacity> storage_{};
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_RUNTIME__BYTE_RING_BUFFER_HPP_
