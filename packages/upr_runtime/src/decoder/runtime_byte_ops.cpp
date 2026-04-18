#include <cstddef>
#include <cstdint>
#include <vector>

#include "crc32c/crc32c.h"
#include "simdutf.h"
#include "universal_protocol_runtime/decoder/direct_decode_support.hpp"

#ifndef HWY_DISABLED_TARGETS
// Clang sanitizer builds trip over Highway's AVX-512 target matrix on this host/toolchain.
// Keeping AVX2 and below for non-CI builds.
#define HWY_DISABLED_TARGETS (HWY_AVX10_2 | HWY_AVX3_SPR | HWY_AVX3_ZEN4 | HWY_AVX3_DL | HWY_AVX3)
#endif

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "packages/upr_runtime/src/decoder/runtime_byte_ops.cpp"
#include "hwy/foreach_target.h"  // NOLINT(misc-header-include-cycle)
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();

namespace universal_protocol_runtime::direct_decode_support::detail {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;
namespace {

HWY_ATTR uint8_t hwy_checksum_xor8(const uint8_t* data, std::size_t size) {
  const hn::ScalableTag<uint8_t> d;
  const std::size_t lanes = hn::Lanes(d);
  std::size_t offset = 0;
  auto accumulator = hn::Zero(d);
  for (; offset + lanes <= size; offset += lanes) {
    accumulator = hn::Xor(accumulator, hn::LoadU(d, data + offset));
  }

  thread_local std::vector<uint8_t> scratch;
  scratch.resize(lanes);
  hn::StoreU(accumulator, d, scratch.data());

  uint8_t value = 0;
  for (uint8_t lane : scratch) {
    value ^= lane;
  }
  for (; offset < size; ++offset) {
    value ^= data[offset];
  }
  return value;
}

HWY_ATTR uint16_t hwy_checksum_sum16(const uint8_t* data, std::size_t size) {
  const hn::ScalableTag<uint8_t> d8;
  const hn::Repartition<uint64_t, decltype(d8)> d64;
  const std::size_t lanes = hn::Lanes(d8);
  std::size_t offset = 0;
  uint64_t sum = 0;
  for (; offset + lanes <= size; offset += lanes) {
    const auto chunk = hn::LoadU(d8, data + offset);
    sum += hn::GetLane(hn::SumOfLanes(d64, hn::SumsOf8(chunk)));
  }
  for (; offset < size; ++offset) {
    sum += data[offset];
  }
  return static_cast<uint16_t>(sum & 0xFFFFU);
}

}  // namespace
}  // namespace HWY_NAMESPACE
}  // namespace universal_protocol_runtime::direct_decode_support::detail

HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace universal_protocol_runtime::direct_decode_support::detail {
namespace {

const char* byte_chars(ByteSpan bytes) noexcept { return reinterpret_cast<const char*>(bytes.data()); }

const uint8_t* byte_data(ByteSpan bytes) noexcept { return reinterpret_cast<const uint8_t*>(bytes.data()); }

}  // namespace

HWY_EXPORT(hwy_checksum_xor8);   // NOLINT(modernize-avoid-c-arrays)
HWY_EXPORT(hwy_checksum_sum16);  // NOLINT(modernize-avoid-c-arrays)

bool runtime_is_valid_ascii(ByteSpan bytes) noexcept {
  if (bytes.size() < kAsciiSimdThresholdBytes) {
    return is_valid_ascii_scalar(bytes);
  }
  return simdutf::validate_ascii(byte_chars(bytes), bytes.size());
}

bool runtime_is_valid_utf8(ByteSpan bytes) noexcept {
  if (bytes.size() < kUtf8SimdThresholdBytes) {
    return is_valid_utf8(bytes);
  }
  return simdutf::validate_utf8(byte_chars(bytes), bytes.size());
}

uint8_t runtime_checksum_xor8(ByteSpan bytes) noexcept {
  if (bytes.size() < kChecksumSimdThresholdBytes) {
    return checksum_xor8_scalar(bytes);
  }
  return HWY_DYNAMIC_DISPATCH(hwy_checksum_xor8)(byte_data(bytes), bytes.size());
}

uint16_t runtime_checksum_sum16(ByteSpan bytes) noexcept {
  if (bytes.size() < kChecksumSimdThresholdBytes) {
    return checksum_sum16_scalar(bytes);
  }
  return HWY_DYNAMIC_DISPATCH(hwy_checksum_sum16)(byte_data(bytes), bytes.size());
}

uint32_t runtime_checksum_crc32c(ByteSpan bytes) noexcept {
  if (bytes.size() < kCrc32cSimdThresholdBytes) {
    return static_cast<uint32_t>(checksum_crc32c(bytes));
  }
  return crc32c::Crc32c(byte_data(bytes), bytes.size());
}

}  // namespace universal_protocol_runtime::direct_decode_support::detail

#endif  // HWY_ONCE
