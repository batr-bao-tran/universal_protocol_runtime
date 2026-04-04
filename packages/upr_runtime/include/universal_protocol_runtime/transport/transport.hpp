#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_TRANSPORT__TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_TRANSPORT__TRANSPORT_HPP_
#include <cstddef>

#include "universal_protocol_runtime/core/byte_view.hpp"
#include "utils/status.hpp"

namespace universal_protocol_runtime {

struct ReadResult {
  size_t bytes_read = 0;
  bool end_of_stream = false;
  bool would_block = false;
  Status status = Status::ok_status();
};

class ITransport {
 public:
  virtual ~ITransport() noexcept = default;

  // Reads into the supplied buffer and reports whether progress was made,
  // whether the source would block, or whether the stream reached EOF.
  // A non-blocking transport should set would_block instead of returning
  // repeated zero-byte reads that are expected to become readable later.
  virtual ReadResult read(MutableByteSpan destination) = 0;

  virtual bool is_open() const = 0;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_TRANSPORT__TRANSPORT_HPP_
