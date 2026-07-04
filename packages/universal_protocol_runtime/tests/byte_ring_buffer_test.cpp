#include "universal_protocol_runtime/runtime/byte_ring_buffer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

namespace upr = universal_protocol_runtime;

namespace {

TEST(ByteRingBufferTest, SupportsWrapAroundAndLinearization) {
  upr::ByteRingBuffer<8> buffer;
  std::array<std::byte, 5> first = {
      std::byte{0x01},
      std::byte{0x02},
      std::byte{0x03},
      std::byte{0x04},
      std::byte{0x05},
  };
  auto writable = buffer.writable_span();
  std::copy(first.begin(), first.end(), writable.begin());
  buffer.commit_write(first.size());

  buffer.consume(3);

  std::array<std::byte, 4> second = {
      std::byte{0x06},
      std::byte{0x07},
      std::byte{0x08},
      std::byte{0x09},
  };
  writable = buffer.writable_span();
  std::copy(second.begin(), second.begin() + static_cast<ptrdiff_t>(writable.size()), writable.begin());
  buffer.commit_write(writable.size());
  writable = buffer.writable_span();
  std::copy(second.begin() + 3, second.end(), writable.begin());
  buffer.commit_write(1);

  EXPECT_TRUE(buffer.has_wrapped_readable_data());
  EXPECT_EQ(buffer.size(), 6U);

  std::array<std::byte, 6> linearized{};
  const size_t copied = buffer.linearize(linearized);
  EXPECT_EQ(copied, 6U);
  EXPECT_EQ(linearized[0], std::byte{0x04});
  EXPECT_EQ(linearized[5], std::byte{0x09});
}

TEST(ByteRingBufferTest, ReportsFullBufferAndContiguousLinearization) {
  upr::ByteRingBuffer<4> buffer;
  auto writable = buffer.writable_span();
  writable[0] = std::byte{0x11};
  writable[1] = std::byte{0x22};
  writable[2] = std::byte{0x33};
  buffer.commit_write(3);

  EXPECT_EQ(buffer.capacity(), 3U);
  EXPECT_EQ(buffer.size(), 3U);
  EXPECT_EQ(buffer.free_space(), 0U);
  EXPECT_FALSE(buffer.empty());
  EXPECT_TRUE(buffer.writable_span().empty());
  EXPECT_FALSE(buffer.has_wrapped_readable_data());

  std::array<std::byte, 2> destination{};
  const size_t copied = buffer.linearize(destination);
  EXPECT_EQ(copied, 2U);
  EXPECT_EQ(destination[0], std::byte{0x11});
  EXPECT_EQ(destination[1], std::byte{0x22});

  buffer.consume(3);
  EXPECT_TRUE(buffer.empty());
  EXPECT_TRUE(buffer.readable_span().empty());
}

}  // namespace
