#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__LOCAL_SHM_RING_TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__LOCAL_SHM_RING_TRANSPORT_HPP_

#include <memory>
#include <utility>

#include "universal_protocol_runtime/adapters/byte_stream_transport.hpp"

namespace universal_protocol_runtime {

struct LocalShmRingOptions {
  /**
   * @brief Number of ring slots per direction.
   */
  size_t slot_count = 256;
  /**
   * @brief Maximum bytes stored in each slot.
   */
  size_t slot_size = 4096;
};

/**
 * @brief Shared-memory ring transport optimized for same-host IPC.
 */
class LocalShmRingTransport final : public IByteStreamTransport {
 public:
  LocalShmRingTransport() = default;
  /**
   * @brief Destroys the transport and releases shared resources.
   */
  ~LocalShmRingTransport() noexcept override;

  LocalShmRingTransport(const LocalShmRingTransport&) = delete;
  LocalShmRingTransport& operator=(const LocalShmRingTransport&) = delete;
  LocalShmRingTransport(LocalShmRingTransport&& other) noexcept;
  LocalShmRingTransport& operator=(LocalShmRingTransport&& other) noexcept;

  /**
   * @brief Creates a connected bidirectional shared-memory pair.
   */
  static StatusOr<std::pair<LocalShmRingTransport, LocalShmRingTransport>> create_pair(
      LocalShmRingOptions options = {});

  /**
   * @brief Reads bytes from the inbound ring.
   */
  ReadResult read(MutableByteSpan destination) override;
  /**
   * @brief Writes bytes into the outbound ring.
   */
  WriteResult write(ByteSpan source) override;
  /**
   * @brief Closes this transport endpoint.
   */
  Status close() override;
  /**
   * @brief Indicates whether the endpoint is open.
   */
  bool is_open() const override;
  /**
   * @brief Returns the backing shared-memory file descriptor.
   */
  int native_handle() const override;
  /**
   * @brief Waits until data is readable.
   */
  StatusOr<bool> wait_until_readable(int timeout_ms) const override;
  /**
   * @brief Waits until space is writable.
   */
  StatusOr<bool> wait_until_writable(int timeout_ms) const override;
  /**
   * @brief Returns capability flags for this transport.
   */
  TransportCapabilityMask capabilities() const override;
  /**
   * @brief Returns a local endpoint description string.
   */
  std::string local_endpoint() const override;
  /**
   * @brief Returns a peer endpoint description string.
   */
  std::string peer_endpoint() const override;
  /**
   * @brief Attempts to lease a zero-copy receive buffer.
   */
  StatusOr<TransportBufferLease> try_acquire_receive_buffer() override;
  /**
   * @brief Releases a previously leased receive buffer.
   */
  Status release_receive_buffer(const TransportBufferLease& lease) override;

 private:
  struct SharedState;
  struct DirectionView;

  LocalShmRingTransport(std::shared_ptr<SharedState> shared,
                        const DirectionView& write_direction,
                        const DirectionView& read_direction,
                        size_t endpoint_id);
  void move_from(LocalShmRingTransport* other) noexcept;

  std::shared_ptr<SharedState> shared_;
  std::unique_ptr<DirectionView> write_direction_;
  std::unique_ptr<DirectionView> read_direction_;
  size_t endpoint_id_ = 0;
  uint32_t read_slot_index_ = 0;
  size_t read_slot_offset_ = 0;
  bool read_slot_active_ = false;
  bool zero_copy_active_ = false;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__LOCAL_SHM_RING_TRANSPORT_HPP_
