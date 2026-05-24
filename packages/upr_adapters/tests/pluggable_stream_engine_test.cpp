#include "universal_protocol_runtime/adapters/pluggable_stream_engine.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

namespace upr = universal_protocol_runtime;

namespace {

class FakeEngine final : public upr::IByteStreamEngine {
 public:
  explicit FakeEngine(std::vector<std::byte> scripted_reads) : scripted_reads_(std::move(scripted_reads)) {}

  upr::ReadResult read(upr::MutableByteSpan destination) override {
    const size_t bytes_to_copy = std::min(destination.size(), scripted_reads_.size() - read_offset_);
    std::copy_n(scripted_reads_.begin() + static_cast<ptrdiff_t>(read_offset_), bytes_to_copy, destination.begin());
    read_offset_ += bytes_to_copy;
    return {
        .bytes_read = bytes_to_copy,
        .end_of_stream = read_offset_ == scripted_reads_.size(),
    };
  }

  upr::WriteResult write(upr::ByteSpan source) override {
    writes_.insert(writes_.end(), source.begin(), source.end());
    return {.bytes_written = source.size()};
  }

  upr::WriteResult writev(std::span<const upr::ByteSpan> sources) override {
    size_t bytes = 0;
    for (const upr::ByteSpan source : sources) {
      writes_.insert(writes_.end(), source.begin(), source.end());
      bytes += source.size();
    }
    return {.bytes_written = bytes};
  }

  upr::Status close() override {
    open_ = false;
    return upr::Status::ok_status();
  }

  bool is_open() const override { return open_; }
  int native_handle() const override { return 7; }
  upr::StatusOr<bool> wait_until_readable(int) const override { return true; }
  upr::StatusOr<bool> wait_until_writable(int) const override { return true; }
  upr::TransportCapabilityMask capabilities() const override {
    return upr::capability_mask(upr::TransportCapability::kStream);
  }
  std::string local_endpoint() const override { return "fake://local"; }
  std::string peer_endpoint() const override { return "fake://peer"; }
  upr::Status flush() override {
    flushed_ = true;
    return upr::Status::ok_status();
  }
  upr::Status shutdown_read() override {
    read_shutdown_ = true;
    return upr::Status::ok_status();
  }
  upr::Status shutdown_write() override {
    write_shutdown_ = true;
    return upr::Status::ok_status();
  }
  upr::StatusOr<upr::TransportBufferLease> try_acquire_receive_buffer() override {
    return upr::TransportBufferLease{
        .bytes = upr::ByteSpan(scripted_reads_.data(), scripted_reads_.size()), .token = 9, .valid = true};
  }
  upr::Status release_receive_buffer(const upr::TransportBufferLease& lease) override {
    released_token_ = lease.token;
    return upr::Status::ok_status();
  }

  const std::vector<std::byte>& writes() const { return writes_; }
  bool flushed() const { return flushed_; }
  bool read_shutdown() const { return read_shutdown_; }
  bool write_shutdown() const { return write_shutdown_; }
  uint64_t released_token() const { return released_token_; }

 private:
  std::vector<std::byte> scripted_reads_;
  std::vector<std::byte> writes_;
  size_t read_offset_ = 0;
  bool open_ = true;
  bool flushed_ = false;
  bool read_shutdown_ = false;
  bool write_shutdown_ = false;
  uint64_t released_token_ = 0;
};

TEST(PluggableStreamEngineTest, DelegatesReadWriteAndEndpoints) {
  auto engine = std::make_unique<FakeEngine>(std::vector<std::byte>{std::byte{0x1}, std::byte{0x2}, std::byte{0x3}});
  FakeEngine* raw_engine = engine.get();
  upr::PluggableStreamTransport transport(std::move(engine));

  std::array<std::byte, 3> buffer{};
  const upr::ReadResult read_result = transport.read(buffer);
  ASSERT_TRUE(read_result.status.ok()) << read_result.status.message();
  EXPECT_EQ(read_result.bytes_read, 3U);
  EXPECT_EQ(buffer[1], std::byte{0x2});

  const std::array<std::byte, 2> write_payload = {std::byte{0xA}, std::byte{0xB}};
  ASSERT_TRUE(transport.write(upr::ByteSpan(write_payload.data(), write_payload.size())).status.ok());
  ASSERT_EQ(raw_engine->writes().size(), 2U);
  EXPECT_EQ(raw_engine->writes()[0], std::byte{0xA});

  EXPECT_EQ(transport.local_endpoint(), "fake://local");
  EXPECT_EQ(transport.peer_endpoint(), "fake://peer");
  EXPECT_TRUE(transport.flush().ok());
  EXPECT_TRUE(raw_engine->flushed());
  EXPECT_TRUE(transport.shutdown_read().ok());
  EXPECT_TRUE(raw_engine->read_shutdown());
  EXPECT_TRUE(transport.shutdown_write().ok());
  EXPECT_TRUE(raw_engine->write_shutdown());
  auto readable = transport.wait_until_readable(0);
  ASSERT_TRUE(readable.ok()) << readable.status().message();
  EXPECT_TRUE(readable.value());
  auto writable = transport.wait_until_writable(0);
  ASSERT_TRUE(writable.ok()) << writable.status().message();
  EXPECT_TRUE(writable.value());
  auto lease = transport.try_acquire_receive_buffer();
  ASSERT_TRUE(lease.ok()) << lease.status().message();
  ASSERT_TRUE(lease.value().valid);
  EXPECT_TRUE(transport.release_receive_buffer(lease.value()).ok());
  EXPECT_EQ(raw_engine->released_token(), 9U);
  EXPECT_TRUE(transport.close().ok());
  EXPECT_FALSE(transport.is_open());
}

}  // namespace
