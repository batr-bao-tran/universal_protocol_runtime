#include "universal_protocol_runtime/adapters/upr_session.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

#include "universal_protocol_runtime/adapters/unix_socket_transport.hpp"

namespace upr = universal_protocol_runtime;

namespace {

constexpr auto kFramePollDelay = std::chrono::milliseconds(1);
constexpr auto kFramePollTimeout = std::chrono::seconds(5);

upr::FrameChannelPollResult wait_for_frame(upr::FrameChannel* channel, std::vector<std::byte>* frame) {
  const auto deadline = std::chrono::steady_clock::now() + kFramePollTimeout;
  upr::FrameChannelPollResult poll_result;
  while (std::chrono::steady_clock::now() < deadline) {
    poll_result = channel->receive_frame(frame);
    if (poll_result.status == upr::FrameChannelPollStatus::kFrameReady ||
        (poll_result.status != upr::FrameChannelPollStatus::kWouldBlock &&
         poll_result.status != upr::FrameChannelPollStatus::kNeedMoreData)) {
      return poll_result;
    }
    std::this_thread::sleep_for(kFramePollDelay);
  }
  return poll_result;
}

TEST(UprSessionTest, CompletesClientServerHandshakeAndTransfersPayload) {
  auto pair = upr::UnixSocketTransport::create_socket_pair();
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  upr::FrameChannel client_channel(pair.value().first);
  upr::FrameChannel server_channel(pair.value().second);
  upr::UprSession client(client_channel);
  upr::UprSession server(server_channel);

  upr::UprSessionHandshake client_local{
      .protocol_version = 1,
      .transport_mode = upr::UprTransportMode::kLengthPrefixedStream,
      .frame_codec = 1,
      .max_frame_bytes = 4096,
      .session_id = 42,
  };
  upr::UprSessionHandshake server_local = client_local;
  server_local.session_id = 99;

  upr::UprSessionHandshake server_seen{};
  upr::Status server_status;
  std::thread server_thread([&]() { server_status = server.server_handshake(server_local, &server_seen); });

  upr::UprSessionHandshake client_seen{};
  const upr::Status client_status = client.client_handshake(client_local, &client_seen);
  server_thread.join();

  ASSERT_TRUE(client_status.ok()) << client_status.message();
  ASSERT_TRUE(server_status.ok()) << server_status.message();
  EXPECT_EQ(server_seen.session_id, client_local.session_id);
  EXPECT_EQ(client_seen.session_id, server_local.session_id);

  constexpr std::string_view kPayload = "hello-upr";
  ASSERT_TRUE(
      client.send_payload_frame(upr::ByteSpan(reinterpret_cast<const std::byte*>(kPayload.data()), kPayload.size()))
          .ok());

  std::vector<std::byte> received;
  const upr::FrameChannelPollResult result = server.receive_payload_frame(&received);
  ASSERT_EQ(result.status, upr::FrameChannelPollStatus::kFrameReady);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(received.data()), received.size()), kPayload);
}

TEST(UprSessionTest, RejectsIncompatibleHandshakeVersions) {
  upr::UprSessionHandshake local{.protocol_version = 1, .max_frame_bytes = 1024};
  upr::UprSessionHandshake remote{.protocol_version = 2, .max_frame_bytes = 1024};
  const std::vector<std::byte> encoded = upr::UprSession::encode_handshake(remote);
  auto decoded = upr::UprSession::decode_handshake(upr::ByteSpan(encoded.data(), encoded.size()));
  ASSERT_TRUE(decoded.ok()) << decoded.status().message();
  EXPECT_NE(local.protocol_version, decoded.value().protocol_version);
}

TEST(UprSessionTest, RejectsMalformedAndIncompatibleHandshakes) {
  const std::vector<std::byte> short_bytes(3, std::byte{0});
  auto short_decode = upr::UprSession::decode_handshake(upr::ByteSpan(short_bytes.data(), short_bytes.size()));
  EXPECT_FALSE(short_decode.ok());
  EXPECT_EQ(short_decode.status().code(), upr::StatusCode::kInvalidArgument);

  std::vector<std::byte> bad_magic = upr::UprSession::encode_handshake({});
  bad_magic[0] = std::byte{'B'};
  auto bad_magic_decode = upr::UprSession::decode_handshake(upr::ByteSpan(bad_magic.data(), bad_magic.size()));
  EXPECT_FALSE(bad_magic_decode.ok());
  EXPECT_EQ(bad_magic_decode.status().code(), upr::StatusCode::kInvalidArgument);

  upr::UprSessionHandshake base{
      .protocol_version = 1,
      .transport_mode = upr::UprTransportMode::kLengthPrefixedStream,
      .frame_codec = 2,
      .max_frame_bytes = 1024,
      .session_id = 5,
  };
  upr::UprSessionHandshake transport_mismatch = base;
  transport_mismatch.transport_mode = upr::UprTransportMode::kDescriptorRing;
  auto transport_status =
      upr::UprSession::decode_handshake(upr::ByteSpan(upr::UprSession::encode_handshake(transport_mismatch).data(),
                                                      upr::UprSession::encode_handshake(transport_mismatch).size()));
  ASSERT_TRUE(transport_status.ok()) << transport_status.status().message();
  EXPECT_NE(transport_status.value().transport_mode, base.transport_mode);

  upr::UprSessionHandshake codec_mismatch = base;
  codec_mismatch.frame_codec = 9;
  auto codec_status =
      upr::UprSession::decode_handshake(upr::ByteSpan(upr::UprSession::encode_handshake(codec_mismatch).data(),
                                                      upr::UprSession::encode_handshake(codec_mismatch).size()));
  ASSERT_TRUE(codec_status.ok()) << codec_status.status().message();
  EXPECT_NE(codec_status.value().frame_codec, base.frame_codec);

  upr::UprSessionHandshake frame_limit_mismatch = base;
  frame_limit_mismatch.max_frame_bytes = 8;
  auto limit_status =
      upr::UprSession::decode_handshake(upr::ByteSpan(upr::UprSession::encode_handshake(frame_limit_mismatch).data(),
                                                      upr::UprSession::encode_handshake(frame_limit_mismatch).size()));
  ASSERT_TRUE(limit_status.ok()) << limit_status.status().message();
  EXPECT_LT(limit_status.value().max_frame_bytes, base.max_frame_bytes);
}

TEST(UprSessionTest, ReportsClientHandshakeTransportAndCompatibilityFailures) {
  auto pair = upr::UnixSocketTransport::create_socket_pair();
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  upr::FrameChannel client_channel(pair.value().first);
  upr::FrameChannel peer_channel(pair.value().second);
  upr::UprSession client(client_channel);

  upr::UprSessionHandshake local{
      .protocol_version = 1,
      .transport_mode = upr::UprTransportMode::kLengthPrefixedStream,
      .frame_codec = 1,
      .max_frame_bytes = 64,
      .session_id = 7,
  };
  upr::UprSessionHandshake remote = local;
  remote.protocol_version = 2;

  std::thread server_thread([&]() {
    std::vector<std::byte> request_buffer;
    upr::FrameChannelPollResult poll_result = wait_for_frame(&peer_channel, &request_buffer);
    ASSERT_EQ(poll_result.status, upr::FrameChannelPollStatus::kFrameReady);
    const std::vector<std::byte> encoded = upr::UprSession::encode_handshake(remote);
    ASSERT_TRUE(peer_channel.send_frame(upr::ByteSpan(encoded.data(), encoded.size())).ok());
  });

  upr::UprSessionHandshake client_seen{};
  const upr::Status client_status = client.client_handshake(local, &client_seen);
  server_thread.join();

  EXPECT_FALSE(client_status.ok());
  EXPECT_EQ(client_status.code(), upr::StatusCode::kInvalidArgument);
}

TEST(UprSessionTest, ReportsAllClientHandshakeCompatibilityFailures) {
  upr::UprSessionHandshake local{
      .protocol_version = 1,
      .transport_mode = upr::UprTransportMode::kLengthPrefixedStream,
      .frame_codec = 1,
      .max_frame_bytes = 64,
      .session_id = 7,
  };

  std::vector<upr::UprSessionHandshake> remotes;
  upr::UprSessionHandshake transport_mismatch = local;
  transport_mismatch.transport_mode = upr::UprTransportMode::kDatagram;
  remotes.push_back(transport_mismatch);
  upr::UprSessionHandshake codec_mismatch = local;
  codec_mismatch.frame_codec = local.frame_codec + 1;
  remotes.push_back(codec_mismatch);
  upr::UprSessionHandshake max_frame_mismatch = local;
  max_frame_mismatch.max_frame_bytes = local.max_frame_bytes - 1;
  remotes.push_back(max_frame_mismatch);

  for (const upr::UprSessionHandshake& remote : remotes) {
    auto pair = upr::UnixSocketTransport::create_socket_pair();
    ASSERT_TRUE(pair.ok()) << pair.status().message();

    upr::FrameChannel client_channel(pair.value().first);
    upr::FrameChannel peer_channel(pair.value().second);
    upr::UprSession client(client_channel);

    std::thread server_thread([&]() {
      std::vector<std::byte> request_buffer;
      upr::FrameChannelPollResult poll_result = wait_for_frame(&peer_channel, &request_buffer);
      ASSERT_EQ(poll_result.status, upr::FrameChannelPollStatus::kFrameReady);
      const std::vector<std::byte> encoded = upr::UprSession::encode_handshake(remote);
      ASSERT_TRUE(peer_channel.send_frame(upr::ByteSpan(encoded.data(), encoded.size())).ok());
    });

    upr::UprSessionHandshake client_seen{};
    const upr::Status client_status = client.client_handshake(local, &client_seen);
    server_thread.join();

    EXPECT_FALSE(client_status.ok());
    EXPECT_EQ(client_status.code(), upr::StatusCode::kInvalidArgument);
  }
}

TEST(UprSessionTest, ReportsHandshakeFrameDecodeFailures) {
  auto pair = upr::UnixSocketTransport::create_socket_pair();
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  upr::FrameChannel client_channel(pair.value().first);
  upr::FrameChannel peer_channel(pair.value().second);
  upr::UprSession client(client_channel);
  upr::UprSessionHandshake local{
      .protocol_version = 1,
      .transport_mode = upr::UprTransportMode::kLengthPrefixedStream,
      .frame_codec = 1,
      .max_frame_bytes = 64,
      .session_id = 7,
  };

  std::thread server_thread([&]() {
    const std::array<std::byte, 3> malformed = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    EXPECT_TRUE(peer_channel.send_frame(upr::ByteSpan(malformed.data(), malformed.size())).ok());
  });

  upr::UprSessionHandshake remote{};
  const upr::Status status = client.client_handshake(local, &remote);
  server_thread.join();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), upr::StatusCode::kInvalidArgument);
}

TEST(UprSessionTest, ReportsServerHandshakeFrameTransportFailure) {
  auto pair = upr::UnixSocketTransport::create_socket_pair();
  ASSERT_TRUE(pair.ok()) << pair.status().message();

  upr::FrameChannel server_channel(pair.value().second);
  upr::UprSession server(server_channel);
  upr::UprSessionHandshake local{
      .protocol_version = 1,
      .transport_mode = upr::UprTransportMode::kLengthPrefixedStream,
      .frame_codec = 1,
      .max_frame_bytes = 64,
      .session_id = 9,
  };

  const std::array<std::byte, 4> oversized_prefix = {
      std::byte{0xFF},
      std::byte{0xFF},
      std::byte{0xFF},
      std::byte{0x7F},
  };
  ASSERT_TRUE(pair.value().first.write(upr::ByteSpan(oversized_prefix.data(), oversized_prefix.size())).status.ok());
  upr::UprSessionHandshake remote{};
  const upr::Status status = server.server_handshake(local, &remote);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), upr::StatusCode::kIoError);
}

TEST(UprSessionTest, ReportsServerHandshakeDecodeAndCompatibilityFailures) {
  upr::UprSessionHandshake local{
      .protocol_version = 1,
      .transport_mode = upr::UprTransportMode::kLengthPrefixedStream,
      .frame_codec = 1,
      .max_frame_bytes = 64,
      .session_id = 9,
  };

  {
    auto pair = upr::UnixSocketTransport::create_socket_pair();
    ASSERT_TRUE(pair.ok()) << pair.status().message();
    upr::FrameChannel peer_channel(pair.value().first);
    upr::FrameChannel server_channel(pair.value().second);
    upr::UprSession server(server_channel);
    const std::array<std::byte, 3> malformed = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    ASSERT_TRUE(peer_channel.send_frame(upr::ByteSpan(malformed.data(), malformed.size())).ok());
    upr::UprSessionHandshake remote{};
    const upr::Status status = server.server_handshake(local, &remote);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), upr::StatusCode::kInvalidArgument);
  }
  {
    auto pair = upr::UnixSocketTransport::create_socket_pair();
    ASSERT_TRUE(pair.ok()) << pair.status().message();
    upr::FrameChannel peer_channel(pair.value().first);
    upr::FrameChannel server_channel(pair.value().second);
    upr::UprSession server(server_channel);
    upr::UprSessionHandshake incompatible = local;
    incompatible.protocol_version = local.protocol_version + 1;
    const std::vector<std::byte> encoded = upr::UprSession::encode_handshake(incompatible);

    ASSERT_TRUE(peer_channel.send_frame(upr::ByteSpan(encoded.data(), encoded.size())).ok());
    upr::UprSessionHandshake remote{};
    const upr::Status status = server.server_handshake(local, &remote);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), upr::StatusCode::kInvalidArgument);
  }
}

}  // namespace
