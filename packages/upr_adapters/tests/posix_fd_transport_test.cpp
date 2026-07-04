#include "universal_protocol_runtime/adapters/posix_fd_transport.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace upr = universal_protocol_runtime;

namespace {

TEST(PosixFdTransportTest, ReadsFromPipeAndReportsWouldBlockAndEndOfStream) {
  std::array<int, 2> pipe_fds = {-1, -1};
  ASSERT_EQ(::pipe(pipe_fds.data()), 0) << std::strerror(errno);

  upr::PosixFdTransport transport(pipe_fds[0], {.own_handle = true, .non_blocking = true});
  ASSERT_TRUE(transport.is_open());

  std::array<std::byte, 8> buffer{};
  upr::ReadResult blocked = transport.read(buffer);
  EXPECT_TRUE(blocked.would_block);
  EXPECT_EQ(blocked.bytes_read, 0U);

  constexpr std::string_view kPayload = "abc";
  ASSERT_EQ(::write(pipe_fds[1], kPayload.data(), kPayload.size()), static_cast<ssize_t>(kPayload.size()));

  upr::ReadResult first_read = transport.read(buffer);
  ASSERT_TRUE(first_read.status.ok()) << first_read.status.message();
  EXPECT_EQ(first_read.bytes_read, 3U);
  EXPECT_EQ(std::to_integer<char>(buffer[0]), 'a');
  EXPECT_EQ(std::to_integer<char>(buffer[1]), 'b');
  EXPECT_EQ(std::to_integer<char>(buffer[2]), 'c');

  ASSERT_EQ(::close(pipe_fds[1]), 0);
  upr::ReadResult eof = transport.read(buffer);
  EXPECT_TRUE(eof.end_of_stream);
  EXPECT_FALSE(transport.is_open());
}

TEST(PosixFdTransportTest, OpensRegularFilesAsDeviceLikeSources) {
  std::array<char, sizeof("/tmp/upr_fd_transport_XXXXXX")> file_template{};
  std::memcpy(file_template.data(), "/tmp/upr_fd_transport_XXXXXX", file_template.size());
  const int temp_fd = ::mkstemp(file_template.data());
  ASSERT_GE(temp_fd, 0) << std::strerror(errno);
  ASSERT_EQ(::write(temp_fd, "frame", 5), 5);
  ASSERT_EQ(::close(temp_fd), 0);

  upr::StatusOr<upr::PosixFdTransport> opened =
      upr::PosixFdTransport::open_device(file_template.data(), O_RDONLY, {.own_handle = true, .non_blocking = false});
  ASSERT_TRUE(opened.ok()) << opened.status().message();

  std::array<std::byte, 8> buffer{};
  upr::ReadResult result = opened.value().read(buffer);
  EXPECT_EQ(result.bytes_read, 5U);
  EXPECT_EQ(std::to_integer<char>(buffer[0]), 'f');
  EXPECT_EQ(std::to_integer<char>(buffer[4]), 'e');

  EXPECT_TRUE(opened.value().close().ok());
  ASSERT_EQ(::unlink(file_template.data()), 0);
}

TEST(PosixFdTransportTest, SupportsWaitsMovesAndNonOwningClosePaths) {
  std::array<int, 2> pipe_fds = {-1, -1};
  ASSERT_EQ(::pipe(pipe_fds.data()), 0) << std::strerror(errno);

  upr::PosixFdTransport source(pipe_fds[0], {.own_handle = false, .non_blocking = true});
  ASSERT_TRUE(source.is_open());

  upr::StatusOr<bool> initially_readable = source.wait_until_readable(0);
  ASSERT_TRUE(initially_readable.ok()) << initially_readable.status().message();
  EXPECT_FALSE(initially_readable.value());

  constexpr std::string_view kPayload = "z";
  ASSERT_EQ(::write(pipe_fds[1], kPayload.data(), kPayload.size()), static_cast<ssize_t>(kPayload.size()));

  upr::StatusOr<bool> after_write = source.wait_until_readable(1000);
  ASSERT_TRUE(after_write.ok()) << after_write.status().message();
  EXPECT_TRUE(after_write.value());

  upr::PosixFdTransport moved;
  moved = std::move(source);
  EXPECT_TRUE(moved.is_open());

  EXPECT_TRUE(moved.close().ok());
  EXPECT_FALSE(moved.is_open());

  std::array<std::byte, 1> buffer{};
  upr::ReadResult closed_read = moved.read(buffer);
  EXPECT_TRUE(closed_read.end_of_stream);

  ASSERT_EQ(::close(pipe_fds[0]), 0);
  ASSERT_EQ(::close(pipe_fds[1]), 0);
}

TEST(PosixFdTransportTest, ReportsClosedHandlesAndOpenFailuresDefensively) {
  upr::PosixFdTransport closed;
  EXPECT_TRUE(closed.close().ok());
  EXPECT_FALSE(closed.is_open());

  std::array<std::byte, 1> buffer{};
  upr::ReadResult closed_read = closed.read(buffer);
  EXPECT_TRUE(closed_read.end_of_stream);

  upr::StatusOr<bool> closed_wait = closed.wait_until_readable(0);
  EXPECT_FALSE(closed_wait.ok());
  EXPECT_EQ(closed_wait.status().code(), upr::StatusCode::kInvalidArgument);

  upr::StatusOr<upr::PosixFdTransport> missing =
      upr::PosixFdTransport::open_device("/tmp/upr_missing_device", O_RDONLY, {.own_handle = true});
  EXPECT_FALSE(missing.ok());

  std::array<int, 2> pipe_fds = {-1, -1};
  ASSERT_EQ(::pipe(pipe_fds.data()), 0) << std::strerror(errno);
  ASSERT_EQ(::close(pipe_fds[0]), 0);

  upr::PosixFdTransport invalid(pipe_fds[0], {.own_handle = true, .non_blocking = true});
  EXPECT_FALSE(invalid.is_open());
  EXPECT_EQ(invalid.native_handle(), -1);

  ASSERT_EQ(::close(pipe_fds[1]), 0);
}

}  // namespace
