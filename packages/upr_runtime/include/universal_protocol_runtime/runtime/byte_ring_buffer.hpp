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

/**
 * @brief Single-producer/single-consumer-style byte ring buffer for stream runtimes.
 */
template <size_t Capacity>
class ByteRingBuffer {
 public:
  static_assert(Capacity > 1);

  /**
   * @brief Destroys the ring buffer.
   * @return No return value.
   */
  ~ByteRingBuffer() noexcept = default;

  /**
   * @brief Returns the usable payload capacity of the ring.
   * @return Maximum readable bytes excluding the sentinel slot.
   */
  static constexpr size_t capacity() { return Capacity - 1; }

  /**
   * @brief Returns the current readable byte count.
   * @return Number of readable bytes.
   */
  size_t size() const {
    const size_t head = head_;
    const size_t tail = tail_;
    if (tail >= head) {
      return tail - head;
    }
    return Capacity - head + tail;
  }

  /**
   * @brief Returns the remaining writable capacity.
   * @return Number of writable bytes.
   */
  size_t free_space() const { return capacity() - size(); }

  /**
   * @brief Checks whether the ring currently holds no readable bytes.
   * @return `true` when the buffer is empty.
   */
  bool empty() const { return size() == 0; }

  /**
   * @brief Checks whether readable bytes wrap past the end of storage.
   * @return `true` when readable bytes are split across the storage boundary.
   */
  bool has_wrapped_readable_data() const {
    const size_t head = head_;
    const size_t tail = tail_;
    return !empty() && tail <= head;
  }

  /**
   * @brief Returns the next contiguous writable region.
   * @return Writable byte span for the next write.
   */
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

  /**
   * @brief Commits bytes previously written into the writable span.
   * @param bytes_written Number of bytes written by the caller.
   * @return No return value.
   */
  void commit_write(size_t bytes_written) {
    assert(bytes_written <= writable_span().size());
    tail_ = (tail_ + bytes_written) % Capacity;
  }

  /**
   * @brief Returns the next contiguous readable region.
   * @return Readable byte span for the next read.
   */
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

  /**
   * @brief Consumes bytes from the readable region.
   * @param bytes_consumed Number of bytes to discard.
   * @return No return value.
   */
  void consume(size_t bytes_consumed) {
    assert(bytes_consumed <= size());
    head_ = (head_ + bytes_consumed) % Capacity;
  }

  /**
   * @brief Copies readable bytes into a linear destination span.
   * @param destination Destination span for linearized bytes.
   * @return Number of bytes copied into the destination.
   */
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
