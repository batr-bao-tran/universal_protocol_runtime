#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_TRANSPORT__TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_TRANSPORT__TRANSPORT_HPP_
#include <cstddef>

#include "universal_protocol_runtime/core/byte_view.hpp"
#include "utils/status.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Result of one transport read attempt.
 */
struct ReadResult {
  size_t bytes_read = 0;
  bool end_of_stream = false;
  bool would_block = false;
  Status status = Status::ok_status();
};

/**
 * @brief Abstract byte transport used by stream runtimes and adapters.
 */
class ITransport {
 public:
  /**
   * @brief Destroys the transport interface.
   * @return No return value.
   */
  virtual ~ITransport() noexcept = default;

  /**
   * @brief Reads bytes into the supplied buffer.
   * @param destination Writable destination span.
   * @return Read result describing progress, blocking, EOF, or error state.
   */
  virtual ReadResult read(MutableByteSpan destination) = 0;

  /**
   * @brief Reports whether the transport is still open.
   * @return `true` when the transport can still be used.
   */
  virtual bool is_open() const = 0;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_TRANSPORT__TRANSPORT_HPP_
