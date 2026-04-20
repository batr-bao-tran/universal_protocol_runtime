#include "universal_protocol_runtime/adapters/upr_session.hpp"

#include <array>

namespace universal_protocol_runtime {
namespace {

constexpr std::array<std::byte, 4> kMagic = {
    std::byte{'U'},
    std::byte{'P'},
    std::byte{'R'},
    std::byte{'1'},
};

template <typename Integer>
void append_le(Integer value, std::vector<std::byte>* output) {
  for (size_t index = 0; index < sizeof(Integer); ++index) {
    output->push_back(static_cast<std::byte>((static_cast<uint64_t>(value) >> (index * 8U)) & 0xFFU));
  }
}

template <typename Integer>
Integer decode_le(ByteSpan bytes, size_t offset) {
  uint64_t value = 0;
  for (size_t index = 0; index < sizeof(Integer); ++index) {
    value |= static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[offset + index])) << (index * 8U);
  }
  return static_cast<Integer>(value);
}

}  // namespace

Status UprSession::client_handshake(const UprSessionHandshake& local, UprSessionHandshake* remote) {
  const std::vector<std::byte> request = encode_handshake(local);
  const Status send_status = channel_->send_frame(ByteSpan(request.data(), request.size()));
  if (!send_status.ok()) {
    return send_status;
  }
  std::vector<std::byte> response;
  while (true) {
    const FrameChannelPollResult poll_result = channel_->receive_frame(&response);
    if (poll_result.status == FrameChannelPollStatus::kFrameReady) {
      break;
    }
    if (poll_result.status == FrameChannelPollStatus::kWouldBlock ||
        poll_result.status == FrameChannelPollStatus::kNeedMoreData) {
      continue;
    }
    return io_error("Server handshake response failed.");
  }
  StatusOr<UprSessionHandshake> decoded = decode_handshake(ByteSpan(response.data(), response.size()));
  if (!decoded.ok()) {
    return decoded.status();
  }
  const Status compatibility = validate_compatibility(local, decoded.value());
  if (!compatibility.ok()) {
    return compatibility;
  }
  *remote = decoded.value();
  return Status::ok_status();
}

Status UprSession::server_handshake(const UprSessionHandshake& local, UprSessionHandshake* remote) {
  std::vector<std::byte> request;
  while (true) {
    const FrameChannelPollResult poll_result = channel_->receive_frame(&request);
    if (poll_result.status == FrameChannelPollStatus::kFrameReady) {
      break;
    }
    if (poll_result.status == FrameChannelPollStatus::kWouldBlock ||
        poll_result.status == FrameChannelPollStatus::kNeedMoreData) {
      continue;
    }
    return io_error("Client handshake request failed.");
  }
  StatusOr<UprSessionHandshake> decoded = decode_handshake(ByteSpan(request.data(), request.size()));
  if (!decoded.ok()) {
    return decoded.status();
  }
  const Status compatibility = validate_compatibility(local, decoded.value());
  if (!compatibility.ok()) {
    return compatibility;
  }
  *remote = decoded.value();
  const std::vector<std::byte> encoded = encode_handshake(local);
  return channel_->send_frame(ByteSpan(encoded.data(), encoded.size()));
}

std::vector<std::byte> UprSession::encode_handshake(const UprSessionHandshake& handshake) {
  std::vector<std::byte> encoded;
  encoded.reserve(24);
  encoded.insert(encoded.end(), kMagic.begin(), kMagic.end());
  append_le<uint16_t>(handshake.protocol_version, &encoded);
  append_le<uint16_t>(handshake.flags, &encoded);
  append_le<uint16_t>(static_cast<uint16_t>(handshake.transport_mode), &encoded);
  append_le<uint16_t>(handshake.frame_codec, &encoded);
  append_le<uint32_t>(handshake.max_frame_bytes, &encoded);
  append_le<uint64_t>(handshake.session_id, &encoded);
  return encoded;
}

StatusOr<UprSessionHandshake> UprSession::decode_handshake(ByteSpan bytes) {
  if (bytes.size() != 24U) {
    return invalid_argument("UPR handshake frame has an unexpected size.");
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    return invalid_argument("UPR handshake magic is invalid.");
  }
  return UprSessionHandshake{
      .protocol_version = decode_le<uint16_t>(bytes, 4U),
      .flags = decode_le<uint16_t>(bytes, 6U),
      .transport_mode = static_cast<UprTransportMode>(decode_le<uint16_t>(bytes, 8U)),
      .frame_codec = decode_le<uint16_t>(bytes, 10U),
      .max_frame_bytes = decode_le<uint32_t>(bytes, 12U),
      .session_id = decode_le<uint64_t>(bytes, 16U),
  };
}

Status UprSession::validate_compatibility(const UprSessionHandshake& local, const UprSessionHandshake& remote) {
  if (local.protocol_version != remote.protocol_version) {
    return invalid_argument("UPR protocol versions do not match.");
  }
  if (local.transport_mode != remote.transport_mode) {
    return invalid_argument("UPR transport modes do not match.");
  }
  if (local.frame_codec != remote.frame_codec) {
    return invalid_argument("UPR frame codecs do not match.");
  }
  if (remote.max_frame_bytes < local.max_frame_bytes) {
    return invalid_argument("Remote max frame size is smaller than the local requirement.");
  }
  return Status::ok_status();
}

}  // namespace universal_protocol_runtime
