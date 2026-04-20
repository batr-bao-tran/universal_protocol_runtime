#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__BYTE_STREAM_TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__BYTE_STREAM_TRANSPORT_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "universal_protocol_runtime/adapters/transport_capabilities.hpp"
#include "universal_protocol_runtime/transport/transport.hpp"

namespace universal_protocol_runtime {

struct WriteResult {
  /**
   * @brief Number of bytes written by the transport.
   */
  size_t bytes_written = 0;
  /**
   * @brief Indicates the transport would block before completing.
   */
  bool would_block = false;
  /**
   * @brief Operation status for the write attempt.
   */
  Status status = Status::ok_status();
};

struct TransportBufferLease {
  /**
   * @brief Borrowed byte view for a leased receive buffer.
   */
  ByteSpan bytes{};
  /**
   * @brief Opaque lease token returned by the transport.
   */
  uint64_t token = 0;
  /**
   * @brief Indicates whether this lease instance is valid.
   */
  bool valid = false;
};

/**
 * @brief Abstract byte-stream transport interface for read/write I/O.
 */
class IByteStreamTransport : public ITransport {
 public:
  /**
   * @brief Destroys the transport interface.
   */
  ~IByteStreamTransport() noexcept override = default;

  /**
   * @brief Writes bytes to the underlying transport.
   */
  virtual WriteResult write(ByteSpan source) = 0;
  /**
   * @brief Writes multiple byte spans in sequence.
   */
  virtual WriteResult writev(std::span<const ByteSpan> sources);
  /**
   * @brief Flushes buffered outbound data.
   */
  virtual Status flush() { return Status::ok_status(); }
  /**
   * @brief Shuts down the read side of the transport.
   */
  virtual Status shutdown_read() { return Status::ok_status(); }
  /**
   * @brief Shuts down the write side of the transport.
   */
  virtual Status shutdown_write() { return Status::ok_status(); }
  /**
   * @brief Closes the transport handle.
   */
  virtual Status close() = 0;
  /**
   * @brief Returns the native OS handle for this transport.
   */
  virtual int native_handle() const = 0;
  /**
   * @brief Waits until the transport becomes readable.
   */
  virtual StatusOr<bool> wait_until_readable(int timeout_ms) const = 0;
  /**
   * @brief Waits until the transport becomes writable.
   */
  virtual StatusOr<bool> wait_until_writable(int timeout_ms) const = 0;
  /**
   * @brief Returns capability flags supported by this transport.
   */
  virtual TransportCapabilityMask capabilities() const = 0;
  /**
   * @brief Returns a local endpoint description string.
   */
  virtual std::string local_endpoint() const = 0;
  /**
   * @brief Returns a peer endpoint description string.
   */
  virtual std::string peer_endpoint() const = 0;

  /**
   * @brief Attempts to lease a zero-copy receive buffer.
   */
  virtual StatusOr<TransportBufferLease> try_acquire_receive_buffer() {
    return not_found("Zero-copy receive is not supported by this transport.");
  }

  /**
   * @brief Releases a previously leased receive buffer.
   */
  virtual Status release_receive_buffer(const TransportBufferLease&) {
    return invalid_argument("Zero-copy receive is not supported by this transport.");
  }
};

/**
 * @brief Abstract listener interface for accepting stream transports.
 */
class IListenerTransport {
 public:
  /**
   * @brief Destroys the listener interface.
   */
  virtual ~IListenerTransport() noexcept = default;

  /**
   * @brief Waits until an incoming connection is available.
   */
  virtual StatusOr<bool> wait_for_connection(int timeout_ms) const = 0;
  /**
   * @brief Accepts a pending connection.
   */
  virtual StatusOr<std::unique_ptr<IByteStreamTransport>> accept() = 0;
  /**
   * @brief Closes the listener handle.
   */
  virtual Status close() = 0;
  /**
   * @brief Returns the native OS handle for this listener.
   */
  virtual int native_handle() const = 0;
  /**
   * @brief Indicates whether the listener is open.
   */
  virtual bool is_open() const = 0;
  /**
   * @brief Returns the local endpoint bound by the listener.
   */
  virtual std::string local_endpoint() const = 0;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__BYTE_STREAM_TRANSPORT_HPP_
