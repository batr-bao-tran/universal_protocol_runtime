#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__PLUGGABLE_STREAM_ENGINE_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__PLUGGABLE_STREAM_ENGINE_HPP_

#include <memory>
#include <span>

#include "universal_protocol_runtime/adapters/byte_stream_transport.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Engine abstraction used by pluggable stream transports.
 */
class IByteStreamEngine {
 public:
  /**
   * @brief Destroys the engine interface.
   */
  virtual ~IByteStreamEngine() noexcept = default;

  /**
   * @brief Reads bytes from the engine.
   */
  virtual ReadResult read(MutableByteSpan destination) = 0;
  /**
   * @brief Writes bytes to the engine.
   */
  virtual WriteResult write(ByteSpan source) = 0;
  /**
   * @brief Writes multiple byte spans to the engine.
   */
  virtual WriteResult writev(std::span<const ByteSpan> sources) = 0;
  /**
   * @brief Closes the engine handle.
   */
  virtual Status close() = 0;
  /**
   * @brief Indicates whether the engine is open.
   */
  virtual bool is_open() const = 0;
  /**
   * @brief Returns the native OS handle for the engine.
   */
  virtual int native_handle() const = 0;
  /**
   * @brief Waits until the engine is readable.
   */
  virtual StatusOr<bool> wait_until_readable(int timeout_ms) const = 0;
  /**
   * @brief Waits until the engine is writable.
   */
  virtual StatusOr<bool> wait_until_writable(int timeout_ms) const = 0;
  /**
   * @brief Returns capability flags supported by the engine.
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
   * @brief Flushes buffered outbound data.
   */
  virtual Status flush() { return Status::ok_status(); }
  /**
   * @brief Shuts down the engine read side.
   */
  virtual Status shutdown_read() { return Status::ok_status(); }
  /**
   * @brief Shuts down the engine write side.
   */
  virtual Status shutdown_write() { return Status::ok_status(); }
  /**
   * @brief Attempts to lease a zero-copy receive buffer.
   */
  virtual StatusOr<TransportBufferLease> try_acquire_receive_buffer() {
    return not_found("Zero-copy receive is not supported by this engine.");
  }
  /**
   * @brief Releases a previously leased receive buffer.
   */
  virtual Status release_receive_buffer(const TransportBufferLease&) {
    return invalid_argument("Zero-copy receive is not supported by this engine.");
  }
};

/**
 * @brief Transport wrapper that forwards operations to a pluggable engine.
 */
class PluggableStreamTransport final : public IByteStreamTransport {
 public:
  /**
   * @brief Constructs a transport from an engine implementation.
   */
  explicit PluggableStreamTransport(std::unique_ptr<IByteStreamEngine> engine) : engine_(std::move(engine)) {}

  ~PluggableStreamTransport() noexcept override = default;

  PluggableStreamTransport(const PluggableStreamTransport&) = delete;
  PluggableStreamTransport& operator=(const PluggableStreamTransport&) = delete;
  PluggableStreamTransport(PluggableStreamTransport&& other) noexcept = default;
  PluggableStreamTransport& operator=(PluggableStreamTransport&& other) noexcept = default;

  /**
   * @brief Reads bytes from the underlying engine.
   */
  ReadResult read(MutableByteSpan destination) override;
  /**
   * @brief Writes bytes to the underlying engine.
   */
  WriteResult write(ByteSpan source) override;
  /**
   * @brief Writes multiple byte spans to the underlying engine.
   */
  WriteResult writev(std::span<const ByteSpan> sources) override;
  /**
   * @brief Flushes buffered outbound data.
   */
  Status flush() override;
  /**
   * @brief Shuts down the read side.
   */
  Status shutdown_read() override;
  /**
   * @brief Shuts down the write side.
   */
  Status shutdown_write() override;
  /**
   * @brief Closes the transport.
   */
  Status close() override;
  /**
   * @brief Indicates whether the transport is open.
   */
  bool is_open() const override;
  /**
   * @brief Returns the native OS handle.
   */
  int native_handle() const override;
  /**
   * @brief Waits until readable.
   */
  StatusOr<bool> wait_until_readable(int timeout_ms) const override;
  /**
   * @brief Waits until writable.
   */
  StatusOr<bool> wait_until_writable(int timeout_ms) const override;
  /**
   * @brief Returns transport capability flags.
   */
  TransportCapabilityMask capabilities() const override;
  /**
   * @brief Returns local endpoint text.
   */
  std::string local_endpoint() const override;
  /**
   * @brief Returns peer endpoint text.
   */
  std::string peer_endpoint() const override;
  /**
   * @brief Attempts to lease a zero-copy receive buffer.
   */
  StatusOr<TransportBufferLease> try_acquire_receive_buffer() override;
  /**
   * @brief Releases a leased receive buffer.
   */
  Status release_receive_buffer(const TransportBufferLease& lease) override;

 private:
  std::unique_ptr<IByteStreamEngine> engine_;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__PLUGGABLE_STREAM_ENGINE_HPP_
