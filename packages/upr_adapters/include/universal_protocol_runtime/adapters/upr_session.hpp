#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__UPR_SESSION_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__UPR_SESSION_HPP_

#include <cstdint>
#include <vector>

#include "universal_protocol_runtime/adapters/frame_channel.hpp"

namespace universal_protocol_runtime {

enum class UprTransportMode : uint16_t {
  kLengthPrefixedStream = 1,
  kDescriptorRing = 2,
  kDatagram = 3,
};

struct UprSessionHandshake {
  /**
   * @brief Negotiated protocol version.
   */
  uint16_t protocol_version = 1;
  /**
   * @brief Session flags exchanged during handshake.
   */
  uint16_t flags = 0;
  /**
   * @brief Transport mode expected by both peers.
   */
  UprTransportMode transport_mode = UprTransportMode::kLengthPrefixedStream;
  /**
   * @brief Framing/codec version for payload frames.
   */
  uint16_t frame_codec = 1;
  /**
   * @brief Maximum accepted frame size in bytes.
   */
  uint32_t max_frame_bytes = 1U << 20U;
  /**
   * @brief Peer-visible session identifier.
   */
  uint64_t session_id = 0;
};

/**
 * @brief Session helper for UPR handshake and framed payload exchange.
 */
class UprSession {
 public:
  /**
   * @brief Constructs a session over a frame channel.
   */
  explicit UprSession(FrameChannel& channel) : channel_(&channel) {}

  /**
   * @brief Runs the client-side handshake sequence.
   */
  Status client_handshake(const UprSessionHandshake& local, UprSessionHandshake* remote);
  /**
   * @brief Runs the server-side handshake sequence.
   */
  Status server_handshake(const UprSessionHandshake& local, UprSessionHandshake* remote);

  /**
   * @brief Sends one payload frame on the session channel.
   */
  Status send_payload_frame(ByteSpan payload) { return channel_->send_frame(payload); }
  /**
   * @brief Receives one payload frame from the session channel.
   */
  FrameChannelPollResult receive_payload_frame(std::vector<std::byte>* payload) {
    return channel_->receive_frame(payload);
  }

  /**
   * @brief Encodes a handshake structure into bytes.
   */
  static std::vector<std::byte> encode_handshake(const UprSessionHandshake& handshake);
  /**
   * @brief Decodes a handshake structure from bytes.
   */
  static StatusOr<UprSessionHandshake> decode_handshake(ByteSpan bytes);

 private:
  static Status validate_compatibility(const UprSessionHandshake& local, const UprSessionHandshake& remote);

  FrameChannel* channel_ = nullptr;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__UPR_SESSION_HPP_
