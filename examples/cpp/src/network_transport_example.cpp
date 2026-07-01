#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

#include "examples/cpp/include/hardware_telemetry_example_support.hpp"
#include "universal_protocol_runtime/adapters/frame_channel.hpp"
#include "universal_protocol_runtime/adapters/tcp_stream_transport.hpp"
#include "universal_protocol_runtime/adapters/unix_socket_transport.hpp"
#include "universal_protocol_runtime/adapters/upr_session.hpp"

namespace {

namespace example = hardware_telemetry_example;
namespace upr = universal_protocol_runtime;

constexpr auto kPollDelay = std::chrono::milliseconds(1);
constexpr auto kPollTimeout = std::chrono::seconds(5);
constexpr int kConnectionTimeoutMs = 3000;

upr::UprSessionHandshake handshake(uint64_t session_id) {
  return {
      .protocol_version = 1,
      .transport_mode = upr::UprTransportMode::kLengthPrefixedStream,
      .frame_codec = 1,
      .max_frame_bytes = 4096,
      .session_id = session_id,
  };
}

bool wait_for_payload(upr::UprSession* session, std::vector<std::byte>* payload) {
  const auto deadline = std::chrono::steady_clock::now() + kPollTimeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const upr::FrameChannelPollResult result = session->receive_payload_frame(payload);
    if (result.status == upr::FrameChannelPollStatus::kFrameReady) {
      return true;
    }
    if (result.status != upr::FrameChannelPollStatus::kWouldBlock &&
        result.status != upr::FrameChannelPollStatus::kNeedMoreData) {
      std::cerr << "receive_payload_frame failed with status=" << static_cast<int>(result.status) << '\n';
      return false;
    }
    std::this_thread::sleep_for(kPollDelay);
  }
  std::cerr << "timed out waiting for payload frame\n";
  return false;
}

bool decode_sensor_payload(std::string_view label,
                           const upr::CompiledProtocol& protocol,
                           const std::vector<std::byte>& payload) {
  upr::ProtocolDecoder decoder(protocol);
  upr::DecodedMessage message;
  const upr::DecodeStatus status =
      decoder.decode_as("SensorPacket", upr::ByteSpan(payload.data(), payload.size()), &message);
  if (status != upr::DecodeStatus::kOk) {
    std::cerr << "failed to decode " << label << " payload\n";
    return false;
  }
  const auto sample_count = message.get_unsigned("sample_count");
  const auto sample_bytes_len = message.get_unsigned("sample_bytes_len");
  if (!sample_count.has_value() || !sample_bytes_len.has_value()) {
    std::cerr << label << " payload missing expected fields\n";
    return false;
  }
  std::cout << label << " sample_count=" << *sample_count << " sample_bytes_len=" << *sample_bytes_len << '\n';
  return true;
}

bool run_unix_socket_pair(const upr::CompiledProtocol& protocol, const std::vector<std::byte>& payload) {
  auto socket_pair = upr::UnixSocketTransport::create_socket_pair();
  if (!socket_pair.ok()) {
    std::cerr << socket_pair.status().message() << '\n';
    return false;
  }

  upr::FrameChannel client_channel(socket_pair.value().first);
  upr::FrameChannel server_channel(socket_pair.value().second);
  upr::UprSession client(client_channel);
  upr::UprSession server(server_channel);

  upr::UprSessionHandshake server_seen{};
  upr::Status server_status;
  std::vector<std::byte> server_payload;
  std::thread server_thread([&]() {
    server_status = server.server_handshake(handshake(2002), &server_seen);
    if (server_status.ok()) {
      (void)wait_for_payload(&server, &server_payload);
    }
  });

  upr::UprSessionHandshake client_seen{};
  const upr::Status client_status = client.client_handshake(handshake(1001), &client_seen);
  if (client_status.ok()) {
    const upr::Status send_status = client.send_payload_frame(upr::ByteSpan(payload.data(), payload.size()));
    if (!send_status.ok()) {
      std::cerr << send_status.message() << '\n';
    }
  } else {
    std::cerr << client_status.message() << '\n';
  }
  server_thread.join();

  if (!client_status.ok() || !server_status.ok()) {
    if (!server_status.ok()) {
      std::cerr << server_status.message() << '\n';
    }
    return false;
  }
  std::cout << "unix_socket_pair client_seen_session=" << client_seen.session_id
            << " server_seen_session=" << server_seen.session_id << '\n';
  return decode_sensor_payload("unix_socket_pair payload", protocol, server_payload);
}

bool run_tcp_loopback(const upr::CompiledProtocol& protocol, const std::vector<std::byte>& payload) {
  auto listener = upr::TcpListener::bind_loopback(0);
  if (!listener.ok()) {
    std::cerr << listener.status().message() << '\n';
    return false;
  }
  const uint16_t port = listener.value().port();

  upr::Status server_status;
  upr::UprSessionHandshake server_seen{};
  std::vector<std::byte> server_payload;
  std::string server_endpoint;
  std::thread server_thread([&]() {
    const auto ready = listener.value().wait_for_connection(kConnectionTimeoutMs);
    if (!ready.ok() || !ready.value()) {
      server_status = ready.ok() ? upr::exhausted("Timed out waiting for TCP connection.") : ready.status();
      return;
    }
    auto accepted = listener.value().accept();
    if (!accepted.ok()) {
      server_status = accepted.status();
      return;
    }
    server_endpoint = accepted.value()->local_endpoint();
    upr::FrameChannel server_channel(*accepted.value());
    upr::UprSession server(server_channel);
    server_status = server.server_handshake(handshake(4004), &server_seen);
    if (server_status.ok()) {
      (void)wait_for_payload(&server, &server_payload);
    }
  });

  auto client = upr::TcpStreamTransport::connect_to_host("127.0.0.1", port);
  if (!client.ok()) {
    std::cerr << client.status().message() << '\n';
    server_thread.join();
    return false;
  }
  upr::FrameChannel client_channel(client.value());
  upr::UprSession client_session(client_channel);
  upr::UprSessionHandshake client_seen{};
  const upr::Status client_status = client_session.client_handshake(handshake(3003), &client_seen);
  if (client_status.ok()) {
    const upr::Status send_status = client_session.send_payload_frame(upr::ByteSpan(payload.data(), payload.size()));
    if (!send_status.ok()) {
      std::cerr << send_status.message() << '\n';
    }
  } else {
    std::cerr << client_status.message() << '\n';
  }
  server_thread.join();

  if (!client_status.ok() || !server_status.ok()) {
    if (!server_status.ok()) {
      std::cerr << server_status.message() << '\n';
    }
    return false;
  }
  std::cout << "tcp_loopback port=" << port << " client_peer=" << client.value().peer_endpoint()
            << " server_endpoint=" << server_endpoint << " remote_session=" << client_seen.session_id << '\n';
  return decode_sensor_payload("tcp_loopback payload", protocol, server_payload);
}

}  // namespace

int main(int argc, char** argv) {
  const std::filesystem::path schema_path = argc > 1 ? std::filesystem::path(argv[1]) : example::default_schema_path();
  example::LoadedProtocol loaded;
  if (!example::load_protocol(schema_path, &loaded)) {
    return 1;
  }

  const std::array<std::byte, 8> sample_bytes = {
      std::byte{0x10},
      std::byte{0x11},
      std::byte{0x12},
      std::byte{0x13},
      std::byte{0x20},
      std::byte{0x21},
      std::byte{0x22},
      std::byte{0x23},
  };

  const auto payload = example::encode_sensor_packet(
      loaded.compiled, 2U, 2U, upr::ByteSpan(sample_bytes.data(), sample_bytes.size()), false);
  if (!payload.has_value()) {
    std::cerr << "failed to encode SensorPacket payload\n";
    return 1;
  }

  if (!run_unix_socket_pair(loaded.compiled, *payload) || !run_tcp_loopback(loaded.compiled, *payload)) {
    return 1;
  }
  return 0;
}
