#include "universal_protocol_runtime/adapters/byte_stream_transport.hpp"

namespace universal_protocol_runtime {

WriteResult IByteStreamTransport::writev(std::span<const ByteSpan> sources) {
  size_t total_bytes_written = 0;
  for (const ByteSpan source : sources) {
    if (source.empty()) {
      continue;
    }
    const WriteResult result = write(source);
    if (!result.status.ok() || result.would_block) {
      return {
          .bytes_written = total_bytes_written + result.bytes_written,
          .would_block = result.would_block,
          .status = result.status,
      };
    }
    total_bytes_written += result.bytes_written;
    if (result.bytes_written != source.size()) {
      return {
          .bytes_written = total_bytes_written,
          .would_block = true,
      };
    }
  }
  return {
      .bytes_written = total_bytes_written,
  };
}

}  // namespace universal_protocol_runtime
