#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_TRANSPORT__SPAN_TRANSPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_TRANSPORT__SPAN_TRANSPORT_HPP_
#include <algorithm>
#include <cstddef>

#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/transport/transport.hpp"

namespace universal_protocol_runtime {

class SpanTransport final : public ITransport {
 public:
  explicit SpanTransport(ByteSpan source, size_t chunk_size = 0) : source_(source), chunk_size_(chunk_size) {}

  ~SpanTransport() noexcept override = default;

  ReadResult read(MutableByteSpan destination) override {
    if (!open_) {
      return {.end_of_stream = true};
    }
    if (offset_ >= source_.size()) {
      open_ = false;
      return {.end_of_stream = true};
    }
    const size_t max_chunk = chunk_size_ == 0 ? source_.size() : chunk_size_;
    const size_t bytes_to_copy = std::min({destination.size(), max_chunk, source_.size() - offset_});
    std::copy_n(source_.begin() + static_cast<ptrdiff_t>(offset_), bytes_to_copy, destination.begin());
    offset_ += bytes_to_copy;
    const bool eof = offset_ >= source_.size();
    if (eof) {
      open_ = false;
    }
    return {
        .bytes_read = bytes_to_copy,
        .end_of_stream = eof,
    };
  }

  bool is_open() const override { return open_; }

 private:
  ByteSpan source_;
  size_t offset_ = 0;
  size_t chunk_size_ = 0;
  bool open_ = true;
};

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_TRANSPORT__SPAN_TRANSPORT_HPP_
