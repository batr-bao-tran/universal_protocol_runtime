#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UTILS_INCLUDE_UTILS__XXHASH64_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UTILS_INCLUDE_UTILS__XXHASH64_HPP_
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace universal_protocol_runtime {

inline constexpr uint64_t kXxHash64Seed = 0ULL;

class XxHash64State {
 public:
  explicit constexpr XxHash64State(uint64_t seed = kXxHash64Seed) noexcept
      : seed_(seed), v1_(seed + kPrime1 + kPrime2), v2_(seed + kPrime2), v3_(seed), v4_(seed - kPrime1) {}

  ~XxHash64State() noexcept = default;

  void update(std::span<const std::byte> bytes) noexcept {
    if (bytes.empty()) {
      return;
    }

    total_length_ += static_cast<uint64_t>(bytes.size());
    const std::byte* data = bytes.data();
    size_t remaining = bytes.size();

    if (buffer_size_ + remaining < kStripeBytes) {
      std::copy_n(data, remaining, buffer_.data() + buffer_size_);
      buffer_size_ += remaining;
      return;
    }

    if (buffer_size_ != 0) {
      const size_t fill_size = kStripeBytes - buffer_size_;
      std::copy_n(data, fill_size, buffer_.data() + buffer_size_);
      consume_stripe(buffer_.data());
      data += fill_size;
      remaining -= fill_size;
      buffer_size_ = 0;
    }

    while (remaining >= kStripeBytes) {
      consume_stripe(data);
      data += kStripeBytes;
      remaining -= kStripeBytes;
    }

    if (remaining != 0) {
      std::copy_n(data, remaining, buffer_.data());
      buffer_size_ = remaining;
    }
  }

  void update(std::string_view value) noexcept { update(std::as_bytes(std::span(value.data(), value.size()))); }

  template <typename T>
  void update_integral(T value) noexcept {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);

    uint64_t normalized = 0;
    if constexpr (std::is_enum_v<T>) {
      using Underlying = std::underlying_type_t<T>;
      normalized = static_cast<uint64_t>(static_cast<Underlying>(value));
    } else {
      normalized = static_cast<uint64_t>(value);
    }

    std::array<std::byte, sizeof(normalized)> bytes{};
    for (size_t index = 0; index < bytes.size(); ++index) {
      bytes[index] = std::byte{static_cast<uint8_t>((normalized >> (index * kBitsPerByte)) & 0xFFU)};
    }
    update(bytes);
  }

  void update_bool(bool value) noexcept {
    const uint8_t normalized = value ? 1U : 0U;
    update(std::as_bytes(std::span(&normalized, kBoolBytes)));
  }

  uint64_t digest() const noexcept {
    uint64_t hash = 0;
    if (total_length_ >= kStripeBytes) {
      hash = std::rotl(v1_, kAccumulatorRotate1) + std::rotl(v2_, kAccumulatorRotate2) +
             std::rotl(v3_, kAccumulatorRotate3) + std::rotl(v4_, kAccumulatorRotate4);
      hash = merge_round(hash, v1_);
      hash = merge_round(hash, v2_);
      hash = merge_round(hash, v3_);
      hash = merge_round(hash, v4_);
    } else {
      hash = seed_ + kPrime5;
    }

    hash += total_length_;

    const std::byte* data = buffer_.data();
    size_t remaining = buffer_size_;
    while (remaining >= kUInt64Bytes) {
      const uint64_t lane = read_u64_le(data);
      hash ^= round(0, lane);
      hash = std::rotl(hash, kStripeMixRotate) * kPrime1 + kPrime4;
      data += kUInt64Bytes;
      remaining -= kUInt64Bytes;
    }

    if (remaining >= kUInt32Bytes) {
      hash ^= static_cast<uint64_t>(read_u32_le(data)) * kPrime1;
      hash = std::rotl(hash, kTailWordRotate) * kPrime2 + kPrime3;
      data += kUInt32Bytes;
      remaining -= kUInt32Bytes;
    }

    while (remaining != 0) {
      hash ^= static_cast<uint64_t>(std::to_integer<uint8_t>(*data)) * kPrime5;
      hash = std::rotl(hash, kTailByteRotate) * kPrime1;
      ++data;
      --remaining;
    }

    return avalanche(hash);
  }

 private:
  static constexpr size_t kBitsPerByte = 8U;
  static constexpr size_t kBoolBytes = 1U;
  static constexpr size_t kUInt32Bytes = sizeof(uint32_t);
  static constexpr size_t kUInt64Bytes = sizeof(uint64_t);
  static constexpr size_t kStripeBytes = 32;
  static constexpr uint64_t kPrime1 = 11400714785074694791ULL;
  static constexpr uint64_t kPrime2 = 14029467366897019727ULL;
  static constexpr uint64_t kPrime3 = 1609587929392839161ULL;
  static constexpr uint64_t kPrime4 = 9650029242287828579ULL;
  static constexpr uint64_t kPrime5 = 2870177450012600261ULL;
  static constexpr int kStripeLaneRotate = 31;
  static constexpr int kAccumulatorRotate1 = 1;
  static constexpr int kAccumulatorRotate2 = 7;
  static constexpr int kAccumulatorRotate3 = 12;
  static constexpr int kAccumulatorRotate4 = 18;
  static constexpr int kStripeMixRotate = 27;
  static constexpr int kTailWordRotate = 23;
  static constexpr int kTailByteRotate = 11;
  static constexpr int kAvalancheShift1 = 33;
  static constexpr int kAvalancheShift2 = 29;
  static constexpr int kAvalancheShift3 = 32;

  static constexpr uint32_t read_u32_le(const std::byte* data) noexcept {
    return static_cast<uint32_t>(std::to_integer<uint8_t>(data[0])) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(data[1])) << (1U * kBitsPerByte)) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(data[2])) << (2U * kBitsPerByte)) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(data[3])) << (3U * kBitsPerByte));
  }

  static constexpr uint64_t read_u64_le(const std::byte* data) noexcept {
    return static_cast<uint64_t>(std::to_integer<uint8_t>(data[0])) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(data[1])) << (1U * kBitsPerByte)) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(data[2])) << (2U * kBitsPerByte)) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(data[3])) << (3U * kBitsPerByte)) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(data[4])) << (4U * kBitsPerByte)) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(data[5])) << (5U * kBitsPerByte)) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(data[6])) << (6U * kBitsPerByte)) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(data[7])) << (7U * kBitsPerByte));
  }

  static constexpr uint64_t round(uint64_t accumulator, uint64_t lane) noexcept {
    accumulator += lane * kPrime2;
    accumulator = std::rotl(accumulator, kStripeLaneRotate);
    accumulator *= kPrime1;
    return accumulator;
  }

  static constexpr uint64_t merge_round(uint64_t accumulator, uint64_t value) noexcept {
    accumulator ^= round(0, value);
    accumulator = accumulator * kPrime1 + kPrime4;
    return accumulator;
  }

  static constexpr uint64_t avalanche(uint64_t hash) noexcept {
    hash ^= hash >> kAvalancheShift1;
    hash *= kPrime2;
    hash ^= hash >> kAvalancheShift2;
    hash *= kPrime3;
    hash ^= hash >> kAvalancheShift3;
    return hash;
  }

  void consume_stripe(const std::byte* stripe) noexcept {
    v1_ = round(v1_, read_u64_le(stripe));
    v2_ = round(v2_, read_u64_le(stripe + kUInt64Bytes));
    v3_ = round(v3_, read_u64_le(stripe + (2U * kUInt64Bytes)));
    v4_ = round(v4_, read_u64_le(stripe + (3U * kUInt64Bytes)));
  }

  uint64_t seed_ = kXxHash64Seed;
  uint64_t total_length_ = 0;
  uint64_t v1_ = kPrime1 + kPrime2;
  uint64_t v2_ = kPrime2;
  uint64_t v3_ = 0;
  uint64_t v4_ = 0 - kPrime1;
  std::array<std::byte, kStripeBytes> buffer_{};
  size_t buffer_size_ = 0;
};

inline uint64_t xxhash64(std::span<const std::byte> bytes, uint64_t seed = kXxHash64Seed) noexcept {
  XxHash64State hasher(seed);
  hasher.update(bytes);
  return hasher.digest();
}

inline uint64_t xxhash64(std::string_view value, uint64_t seed = kXxHash64Seed) noexcept {
  XxHash64State hasher(seed);
  hasher.update(value);
  return hasher.digest();
}

template <typename T>
inline uint64_t xxhash64_integral(T value, uint64_t seed = kXxHash64Seed) noexcept {
  XxHash64State hasher(seed);
  hasher.update_integral(value);
  return hasher.digest();
}

inline uint64_t xxhash64_bool(bool value, uint64_t seed = kXxHash64Seed) noexcept {
  XxHash64State hasher(seed);
  hasher.update_bool(value);
  return hasher.digest();
}

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UTILS_INCLUDE_UTILS__XXHASH64_HPP_
