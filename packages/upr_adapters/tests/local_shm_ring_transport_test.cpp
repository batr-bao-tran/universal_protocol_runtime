#include "universal_protocol_runtime/adapters/local_shm_ring_transport.hpp"

#include <gtest/gtest.h>

#include <array>

namespace upr = universal_protocol_runtime;

namespace {

TEST(LocalShmRingTransportTest, TransfersFramesAndSupportsZeroCopyReceive) {
  auto pair = upr::LocalShmRingTransport::create_pair({.slot_count = 4, .slot_size = 128});
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  const std::array<std::byte, 4> payload = {
      std::byte{0x01},
      std::byte{0x02},
      std::byte{0x03},
      std::byte{0x04},
  };
  ASSERT_TRUE(pair.value().first.write(upr::ByteSpan(payload.data(), payload.size())).status.ok());

  auto lease = pair.value().second.try_acquire_receive_buffer();
  ASSERT_TRUE(lease.ok()) << lease.status().message();
  ASSERT_TRUE(lease.value().valid);
  ASSERT_EQ(lease.value().bytes.size(), payload.size());
  EXPECT_EQ(lease.value().bytes[3], std::byte{0x04});
  EXPECT_TRUE(pair.value().second.release_receive_buffer(lease.value()).ok());
}

TEST(LocalShmRingTransportTest, SupportsPartialReadsAndWouldBlockWhenEmpty) {
  auto pair = upr::LocalShmRingTransport::create_pair({.slot_count = 4, .slot_size = 128});
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  const std::array<std::byte, 6> payload = {
      std::byte{'a'},
      std::byte{'b'},
      std::byte{'c'},
      std::byte{'d'},
      std::byte{'e'},
      std::byte{'f'},
  };
  ASSERT_TRUE(pair.value().first.write(upr::ByteSpan(payload.data(), payload.size())).status.ok());

  std::array<std::byte, 3> first_half{};
  upr::ReadResult first_result = pair.value().second.read(first_half);
  ASSERT_TRUE(first_result.status.ok()) << first_result.status.message();
  EXPECT_EQ(first_result.bytes_read, 3U);
  EXPECT_EQ(first_half[0], std::byte{'a'});

  std::array<std::byte, 3> second_half{};
  upr::ReadResult second_result = pair.value().second.read(second_half);
  ASSERT_TRUE(second_result.status.ok()) << second_result.status.message();
  EXPECT_EQ(second_result.bytes_read, 3U);
  EXPECT_EQ(second_half[2], std::byte{'f'});

  std::array<std::byte, 1> empty{};
  upr::ReadResult blocked = pair.value().second.read(empty);
  EXPECT_TRUE(blocked.would_block);
}

TEST(LocalShmRingTransportTest, ReportsWouldBlockWhenRingIsFull) {
  auto pair = upr::LocalShmRingTransport::create_pair({.slot_count = 2, .slot_size = 64});
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  const std::array<std::byte, 4> payload = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
  ASSERT_TRUE(pair.value().first.write(upr::ByteSpan(payload.data(), payload.size())).status.ok());

  const upr::WriteResult second_write = pair.value().first.write(upr::ByteSpan(payload.data(), payload.size()));
  EXPECT_TRUE(second_write.would_block);

  auto writable = pair.value().first.wait_until_writable(0);
  ASSERT_TRUE(writable.ok()) << writable.status().message();
  EXPECT_FALSE(writable.value());
}

TEST(LocalShmRingTransportTest, ValidatesOptionsAndClosedErrorPaths) {
  auto bad_slot_count = upr::LocalShmRingTransport::create_pair({.slot_count = 1, .slot_size = 64});
  EXPECT_FALSE(bad_slot_count.ok());
  EXPECT_EQ(bad_slot_count.status().code(), upr::StatusCode::kInvalidArgument);

  auto bad_slot_size = upr::LocalShmRingTransport::create_pair({.slot_count = 4, .slot_size = 0});
  EXPECT_FALSE(bad_slot_size.ok());
  EXPECT_EQ(bad_slot_size.status().code(), upr::StatusCode::kInvalidArgument);

  auto pair = upr::LocalShmRingTransport::create_pair({.slot_count = 4, .slot_size = 4});
  ASSERT_TRUE(pair.ok()) << pair.status().message();
  EXPECT_GE(pair.value().first.native_handle(), 0);
  EXPECT_EQ(pair.value().first.local_endpoint(), "shm://endpoint0");
  EXPECT_EQ(pair.value().first.peer_endpoint(), "shm://endpoint1");
  EXPECT_TRUE(upr::has_capability(pair.value().first.capabilities(), upr::TransportCapability::kSharedMemory));

  EXPECT_TRUE(pair.value().first.close().ok());
  EXPECT_FALSE(pair.value().first.is_open());
  const auto closed_readable = pair.value().first.wait_until_readable(0);
  EXPECT_FALSE(closed_readable.ok());
  EXPECT_EQ(closed_readable.status().code(), upr::StatusCode::kInvalidArgument);
  const auto closed_writable = pair.value().first.wait_until_writable(0);
  EXPECT_FALSE(closed_writable.ok());
  EXPECT_EQ(closed_writable.status().code(), upr::StatusCode::kInvalidArgument);
  const std::array<std::byte, 1> payload = {std::byte{0x1}};
  const upr::WriteResult write_result = pair.value().first.write(upr::ByteSpan(payload.data(), payload.size()));
  EXPECT_FALSE(write_result.status.ok());
}

TEST(LocalShmRingTransportTest, ValidatesZeroCopyAndOversizeErrorPaths) {
  auto pair = upr::LocalShmRingTransport::create_pair({.slot_count = 4, .slot_size = 4});
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  auto empty_lease = pair.value().second.try_acquire_receive_buffer();
  EXPECT_FALSE(empty_lease.ok());
  EXPECT_EQ(empty_lease.status().code(), upr::StatusCode::kNotFound);

  const std::array<std::byte, 8> too_large_payload = {
      std::byte{0},
      std::byte{1},
      std::byte{2},
      std::byte{3},
      std::byte{4},
      std::byte{5},
      std::byte{6},
      std::byte{7},
  };
  const upr::WriteResult oversize_write =
      pair.value().first.write(upr::ByteSpan(too_large_payload.data(), too_large_payload.size()));
  EXPECT_FALSE(oversize_write.status.ok());
  EXPECT_EQ(oversize_write.status.code(), upr::StatusCode::kExhausted);

  const std::array<std::byte, 3> payload = {std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
  ASSERT_TRUE(pair.value().first.write(upr::ByteSpan(payload.data(), payload.size())).status.ok());
  auto lease = pair.value().second.try_acquire_receive_buffer();
  ASSERT_TRUE(lease.ok()) << lease.status().message();

  std::array<std::byte, 1> single{};
  const upr::ReadResult blocked_read = pair.value().second.read(single);
  EXPECT_FALSE(blocked_read.status.ok());
  EXPECT_EQ(blocked_read.status.code(), upr::StatusCode::kInvalidArgument);

  auto second_lease = pair.value().second.try_acquire_receive_buffer();
  EXPECT_FALSE(second_lease.ok());
  EXPECT_EQ(second_lease.status().code(), upr::StatusCode::kInvalidArgument);

  upr::TransportBufferLease invalid_lease = lease.value();
  invalid_lease.token += 1;
  const upr::Status invalid_release = pair.value().second.release_receive_buffer(invalid_lease);
  EXPECT_FALSE(invalid_release.ok());
  EXPECT_EQ(invalid_release.code(), upr::StatusCode::kInvalidArgument);

  EXPECT_TRUE(pair.value().second.release_receive_buffer(lease.value()).ok());
  const upr::Status no_active_release = pair.value().second.release_receive_buffer(lease.value());
  EXPECT_FALSE(no_active_release.ok());
  EXPECT_EQ(no_active_release.code(), upr::StatusCode::kInvalidArgument);
}

TEST(LocalShmRingTransportTest, CoversMoveAssignmentReadabilityAndClosedReceiveStates) {
  auto pair = upr::LocalShmRingTransport::create_pair({.slot_count = 4, .slot_size = 8});
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  upr::LocalShmRingTransport moved = std::move(pair.value().first);
  EXPECT_TRUE(moved.is_open());
  EXPECT_TRUE(moved.wait_until_writable(0).value());

  const std::array<std::byte, 4> payload = {std::byte{'d'}, std::byte{'a'}, std::byte{'t'}, std::byte{'a'}};
  ASSERT_TRUE(moved.write(upr::ByteSpan(payload.data(), payload.size())).status.ok());
  EXPECT_TRUE(pair.value().second.wait_until_readable(0).value());

  std::array<std::byte, 2> partial{};
  upr::ReadResult first_part = pair.value().second.read(partial);
  ASSERT_TRUE(first_part.status.ok()) << first_part.status.message();
  EXPECT_EQ(first_part.bytes_read, 2U);

  auto blocked_lease = pair.value().second.try_acquire_receive_buffer();
  EXPECT_FALSE(blocked_lease.ok());
  EXPECT_EQ(blocked_lease.status().code(), upr::StatusCode::kInvalidArgument);

  std::array<std::byte, 2> second_part{};
  upr::ReadResult second_read = pair.value().second.read(second_part);
  ASSERT_TRUE(second_read.status.ok()) << second_read.status.message();
  EXPECT_EQ(second_read.bytes_read, 2U);

  EXPECT_TRUE(moved.close().ok());
  std::array<std::byte, 1> byte{};
  upr::ReadResult closed_read = moved.read(byte);
  EXPECT_TRUE(closed_read.end_of_stream);
  auto closed_lease = moved.try_acquire_receive_buffer();
  EXPECT_FALSE(closed_lease.ok());
  EXPECT_EQ(closed_lease.status().code(), upr::StatusCode::kInvalidArgument);
}

}  // namespace
