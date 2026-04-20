#include "universal_protocol_runtime/adapters/pluggable_stream_engine.hpp"

namespace universal_protocol_runtime {

ReadResult PluggableStreamTransport::read(MutableByteSpan destination) { return engine_->read(destination); }

WriteResult PluggableStreamTransport::write(ByteSpan source) { return engine_->write(source); }

WriteResult PluggableStreamTransport::writev(std::span<const ByteSpan> sources) { return engine_->writev(sources); }

Status PluggableStreamTransport::flush() { return engine_->flush(); }

Status PluggableStreamTransport::shutdown_read() { return engine_->shutdown_read(); }

Status PluggableStreamTransport::shutdown_write() { return engine_->shutdown_write(); }

Status PluggableStreamTransport::close() { return engine_->close(); }

bool PluggableStreamTransport::is_open() const { return engine_->is_open(); }

int PluggableStreamTransport::native_handle() const { return engine_->native_handle(); }

StatusOr<bool> PluggableStreamTransport::wait_until_readable(int timeout_ms) const {
  return engine_->wait_until_readable(timeout_ms);
}

StatusOr<bool> PluggableStreamTransport::wait_until_writable(int timeout_ms) const {
  return engine_->wait_until_writable(timeout_ms);
}

TransportCapabilityMask PluggableStreamTransport::capabilities() const { return engine_->capabilities(); }

std::string PluggableStreamTransport::local_endpoint() const { return engine_->local_endpoint(); }

std::string PluggableStreamTransport::peer_endpoint() const { return engine_->peer_endpoint(); }

StatusOr<TransportBufferLease> PluggableStreamTransport::try_acquire_receive_buffer() {
  return engine_->try_acquire_receive_buffer();
}

Status PluggableStreamTransport::release_receive_buffer(const TransportBufferLease& lease) {
  return engine_->release_receive_buffer(lease);
}

}  // namespace universal_protocol_runtime
