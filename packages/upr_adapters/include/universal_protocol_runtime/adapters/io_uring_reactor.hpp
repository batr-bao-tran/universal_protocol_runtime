#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__IO_URING_REACTOR_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__IO_URING_REACTOR_HPP_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "universal_protocol_runtime/adapters/pluggable_stream_engine.hpp"

namespace universal_protocol_runtime {

enum class IoUringAvailability {
  kUnavailable,
  kAvailable,
};

struct IoUringOptions {
  /**
   * @brief Submission/completion queue depth.
   */
  unsigned queue_depth = 64;
  /**
   * @brief Enables SQPOLL mode when supported.
   */
  bool setup_sqpoll = false;
  /**
   * @brief SQPOLL idle timeout in milliseconds.
   */
  unsigned sq_thread_idle_ms = 0;
  /**
   * @brief Indicates whether the engine closes the provided fd.
   */
  bool own_handle = true;
  /**
   * @brief Enables send zero-copy requests when true.
   */
  bool use_send_zerocopy = false;
  /**
   * @brief Minimum payload size to use send zero-copy.
   */
  size_t send_zerocopy_threshold_bytes = 16384;
};

/**
 * @brief Runtime helpers for io_uring availability checks.
 */
class IoUringReactor final {
 public:
  /**
   * @brief Returns current io_uring availability state.
   */
  static IoUringAvailability availability();
  /**
   * @brief Returns true when io_uring is supported.
   */
  static bool is_supported();
  /**
   * @brief Returns a short availability reason string.
   */
  static std::string_view reason();
};

/**
 * @brief Byte stream engine backed by io_uring operations.
 */
class IoUringStreamEngine final : public IByteStreamEngine {
 public:
  IoUringStreamEngine() = delete;
  /**
   * @brief Destroys the engine and releases ring resources.
   */
  ~IoUringStreamEngine() noexcept override;

  IoUringStreamEngine(const IoUringStreamEngine&) = delete;
  IoUringStreamEngine& operator=(const IoUringStreamEngine&) = delete;
  IoUringStreamEngine(IoUringStreamEngine&& other) noexcept;
  IoUringStreamEngine& operator=(IoUringStreamEngine&& other) noexcept;

  /**
   * @brief Creates an io_uring engine around an existing fd.
   */
  static StatusOr<std::unique_ptr<IByteStreamEngine>> create(int fd,
                                                             std::string local_endpoint,
                                                             std::string peer_endpoint,
                                                             IoUringOptions options = {});

  /**
   * @brief Reads bytes using io_uring receive operations.
   */
  ReadResult read(MutableByteSpan destination) override;
  /**
   * @brief Writes bytes using io_uring send operations.
   */
  WriteResult write(ByteSpan source) override;
  /**
   * @brief Writes multiple byte spans using sendmsg.
   */
  WriteResult writev(std::span<const ByteSpan> sources) override;
  /**
   * @brief Closes the engine and owned resources.
   */
  Status close() override;
  /**
   * @brief Indicates whether the engine is open.
   */
  bool is_open() const override;
  /**
   * @brief Returns the native file descriptor.
   */
  int native_handle() const override;
  /**
   * @brief Waits for read readiness via io_uring poll.
   */
  StatusOr<bool> wait_until_readable(int timeout_ms) const override;
  /**
   * @brief Waits for write readiness via io_uring poll.
   */
  StatusOr<bool> wait_until_writable(int timeout_ms) const override;
  /**
   * @brief Returns capability flags exposed by this engine.
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
   * @brief Shuts down the read side of the descriptor.
   */
  Status shutdown_read() override;
  /**
   * @brief Shuts down the write side of the descriptor.
   */
  Status shutdown_write() override;

 private:
  struct Impl;

  explicit IoUringStreamEngine(std::unique_ptr<Impl> impl);
  void move_from(IoUringStreamEngine* other) noexcept;

  std::unique_ptr<Impl> impl_;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__IO_URING_REACTOR_HPP_
