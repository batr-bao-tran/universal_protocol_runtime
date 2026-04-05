#include "universal_protocol_runtime/runtime/stream_runtime.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "detail/test_support.hpp"
#include "universal_protocol_runtime/compiler/schema_compiler.hpp"
#include "universal_protocol_runtime/decoder/protocol_decoder.hpp"
#include "universal_protocol_runtime/framing/framer.hpp"
#include "universal_protocol_runtime/framing/length_prefixed_framer.hpp"
#include "universal_protocol_runtime/transport/span_transport.hpp"

namespace upr = universal_protocol_runtime;

namespace {

upr::CompiledProtocol make_blob_protocol() {
  return upr_test_support::compile_protocol_or_throw(upr_test_support::make_protocol(
      "capture",
      {upr_test_support::make_message(
          "Blob",
          {
              upr_test_support::make_scalar_field(
                  "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 2),
              upr_test_support::make_scalar_field("length", upr::FieldKind::kUnsigned, 1),
              upr_test_support::make_dynamic_bytes_field("payload", "length"),
          })}));
}

upr::CompiledProtocol make_checksum_protocol() {
  upr::FieldDefinition crc = upr_test_support::make_scalar_field("crc", upr::FieldKind::kUnsigned, 1);
  upr_test_support::add_checksum(&crc, "xor8");
  return upr_test_support::compile_protocol_or_throw(upr_test_support::make_protocol(
      "checksums",
      {upr_test_support::make_message(
          "Checksummed",
          {
              upr_test_support::make_scalar_field(
                  "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 4),
              upr_test_support::make_scalar_field("value", upr::FieldKind::kUnsigned, 1),
              crc,
          })}));
}

class ScriptedTransport final : public upr::ITransport {
 public:
  struct Step {
    std::vector<std::byte> bytes;
    bool end_of_stream = false;
    bool would_block = false;
    upr::Status status = upr::Status::ok_status();
  };

  explicit ScriptedTransport(std::vector<Step> steps) : steps_(std::move(steps)) {}

  ~ScriptedTransport() noexcept override = default;

  upr::ReadResult read(upr::MutableByteSpan destination) override {
    if (index_ >= steps_.size()) {
      open_ = false;
      return {.end_of_stream = true};
    }
    const Step& step = steps_[index_++];
    if (!step.status.ok()) {
      open_ = false;
      return {.status = step.status};
    }
    const size_t bytes_to_copy = std::min(destination.size(), step.bytes.size());
    std::copy_n(step.bytes.begin(), bytes_to_copy, destination.begin());
    open_ = !step.end_of_stream;
    return {
        .bytes_read = bytes_to_copy,
        .end_of_stream = step.end_of_stream,
        .would_block = step.would_block,
    };
  }

  bool is_open() const override { return open_; }

 private:
  std::vector<Step> steps_;
  size_t index_ = 0;
  bool open_ = true;
};

class AlwaysNeedMoreFramer final : public upr::IFramer {
 public:
  ~AlwaysNeedMoreFramer() noexcept override = default;

  upr::FrameStatus try_frame(upr::ByteSpan, upr::FrameSlice*) const override { return upr::FrameStatus::kNeedMoreData; }
};

class AlwaysInvalidFramer final : public upr::IFramer {
 public:
  ~AlwaysInvalidFramer() noexcept override = default;

  upr::FrameStatus try_frame(upr::ByteSpan, upr::FrameSlice*) const override { return upr::FrameStatus::kInvalidFrame; }
};

std::vector<std::byte> make_length_prefixed_blob_stream(std::initializer_list<uint8_t> payload) {
  std::vector<std::byte> stream;
  const auto payload_size = static_cast<uint8_t>(payload.size() + 2U);
  upr_test_support::append_integral<uint8_t>(stream, payload_size, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<uint8_t>(stream, 2, upr::ByteOrder::kLittleEndian);
  upr_test_support::append_integral<uint8_t>(
      stream, static_cast<uint8_t>(payload.size()), upr::ByteOrder::kLittleEndian);
  for (const uint8_t value : payload) {
    stream.push_back(std::byte{value});
  }
  return stream;
}

std::vector<std::byte> make_length_prefixed_payload(std::initializer_list<uint8_t> payload) {
  std::vector<std::byte> stream;
  upr_test_support::append_integral<uint8_t>(
      stream, static_cast<uint8_t>(payload.size()), upr::ByteOrder::kLittleEndian);
  for (const uint8_t value : payload) {
    stream.push_back(std::byte{value});
  }
  return stream;
}

TEST(StreamRuntimeTest, PollsTransportAcrossWrapAroundAndDecodesMessages) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 1,
      .max_payload_size = 16,
  });
  std::vector<std::byte> transport_bytes = make_length_prefixed_blob_stream({0xAA, 0xBB});
  const std::vector<std::byte> second_frame = make_length_prefixed_blob_stream({0x11, 0x22});
  transport_bytes.insert(transport_bytes.end(), second_frame.begin(), second_frame.end());
  upr::SpanTransport transport(upr::ByteSpan(transport_bytes.data(), transport_bytes.size()), 7);
  upr::StreamRuntime<8> runtime(transport, framer, decoder);

  upr::DecodedMessage first_message;
  const upr::PollResult first_result = runtime.poll(&first_message);
  ASSERT_EQ(first_result.status, upr::PollStatus::kMessageReady);
  EXPECT_TRUE(first_result.message_ready());
  EXPECT_EQ(first_result.bytes_consumed, 5U);
  EXPECT_EQ(first_message.message_name(), "Blob");
  EXPECT_EQ(first_message.get<uint8_t>("length"), 2U);

  upr::DecodedMessage second_message;
  const upr::PollResult second_result = runtime.poll(&second_message);
  ASSERT_EQ(second_result.status, upr::PollStatus::kMessageReady);
  EXPECT_EQ(second_result.bytes_consumed, 5U);
  EXPECT_EQ(second_message.get<uint8_t>("length"), 2U);
  const auto payload = second_message.get_bytes("payload");
  ASSERT_TRUE(payload.has_value());
  EXPECT_EQ((*payload)[0], std::byte{0x11});
  EXPECT_EQ(runtime.stats().frames_decoded, 2U);
  EXPECT_GE(runtime.stats().transport_reads, 2U);
}

TEST(StreamRuntimeTest, ReturnsNeedMoreDataWhenTransportWouldBlock) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  AlwaysNeedMoreFramer framer;
  ScriptedTransport transport(std::vector<ScriptedTransport::Step>{{
      .bytes = {},
      .would_block = true,
  }});
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kNeedMoreData);
  EXPECT_FALSE(result.message_ready());
  EXPECT_EQ(result.bytes_consumed, 0U);
}

TEST(StreamRuntimeTest, ReturnsTransportErrorWhenTransportFails) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  AlwaysNeedMoreFramer framer;
  ScriptedTransport transport(std::vector<ScriptedTransport::Step>{{
      .bytes = {},
      .status = upr::io_error("transport_failed"),
  }});
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kTransportError);
}

TEST(StreamRuntimeTest, ReturnsBufferExhaustedWhenNoFrameCanBeProduced) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  AlwaysNeedMoreFramer framer;
  const std::vector<std::byte> bytes = upr_test_support::make_bytes({0x01, 0x02, 0x03, 0x04});
  upr::SpanTransport transport(upr::ByteSpan(bytes.data(), bytes.size()));
  upr::StreamRuntime<4> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kBufferExhausted);
}

TEST(StreamRuntimeTest, ReturnsFrameInvalidWhenFramerRejectsData) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  AlwaysInvalidFramer framer;
  const std::vector<std::byte> bytes = upr_test_support::make_bytes({0x01});
  upr::SpanTransport transport(upr::ByteSpan(bytes.data(), bytes.size()));
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kFrameInvalid);
  EXPECT_EQ(runtime.stats().frame_errors, 1U);
}

TEST(StreamRuntimeTest, ReturnsFrameInvalidForIncompleteFrameAtEndOfStream) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 1,
      .max_payload_size = 16,
  });
  const std::vector<std::byte> bytes = upr_test_support::make_bytes({0x04, 0x02, 0x02, 0xAA});
  upr::SpanTransport transport(upr::ByteSpan(bytes.data(), bytes.size()));
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kFrameInvalid);
  EXPECT_EQ(runtime.stats().frame_errors, 1U);
}

TEST(StreamRuntimeTest, ReturnsDecodeErrorWithExactStatusAndPolicy) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 1,
      .max_payload_size = 16,
  });
  const std::vector<std::byte> bytes = upr_test_support::make_bytes({0x04, 0x02, 0x05, 0xAA, 0xBB});
  upr::SpanTransport transport(upr::ByteSpan(bytes.data(), bytes.size()));
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kDecodeError);
  EXPECT_EQ(result.decode_status, upr::DecodeStatus::kInvalidData);
  EXPECT_EQ(result.policy, upr::DecodeFailurePolicy::kQuarantine);
  EXPECT_EQ(result.bytes_consumed, 5U);
  EXPECT_EQ(runtime.stats().decode_errors, 1U);
}

TEST(StreamRuntimeTest, ReturnsDropAndContinueForUnknownMessageTypes) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 1,
      .max_payload_size = 16,
  });
  const std::vector<std::byte> bytes = upr_test_support::make_bytes({0x04, 0x09, 0x02, 0xAA, 0xBB});
  upr::SpanTransport transport(upr::ByteSpan(bytes.data(), bytes.size()));
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kDecodeError);
  EXPECT_EQ(result.decode_status, upr::DecodeStatus::kMessageNotFound);
  EXPECT_EQ(result.policy, upr::DecodeFailurePolicy::kDropAndContinue);
  EXPECT_EQ(result.bytes_consumed, 5U);
}

TEST(StreamRuntimeTest, ReturnsEndOfStreamForEmptyTransport) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 1,
      .max_payload_size = 16,
  });
  const std::vector<std::byte> empty_bytes;
  upr::SpanTransport transport(upr::ByteSpan(empty_bytes.data(), empty_bytes.size()));
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kEndOfStream);
}

TEST(StreamRuntimeTest, RemainsAtEndOfStreamAfterTransportIsDrained) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 1,
      .max_payload_size = 16,
  });
  const std::vector<std::byte> empty_bytes;
  upr::SpanTransport transport(upr::ByteSpan(empty_bytes.data(), empty_bytes.size()));
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  EXPECT_EQ(runtime.poll(&message).status, upr::PollStatus::kEndOfStream);
  EXPECT_EQ(runtime.poll(&message).status, upr::PollStatus::kEndOfStream);
}

TEST(StreamRuntimeTest, AllowsPolicyOverridesPerRuntime) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 1,
      .max_payload_size = 16,
  });
  const std::vector<std::byte> bytes = upr_test_support::make_bytes({0x04, 0x09, 0x02, 0xAA, 0xBB});
  upr::SpanTransport transport(upr::ByteSpan(bytes.data(), bytes.size()));
  upr::StreamRuntime<8> runtime(transport,
                                framer,
                                decoder,
                                {
                                    .message_not_found_policy = upr::DecodeFailurePolicy::kStop,
                                });
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kDecodeError);
  EXPECT_EQ(result.decode_status, upr::DecodeStatus::kMessageNotFound);
  EXPECT_EQ(result.policy, upr::DecodeFailurePolicy::kStop);
}

TEST(StreamRuntimeTest, ReturnsWouldBlockForZeroByteReadWithoutFlags) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  AlwaysNeedMoreFramer framer;
  ScriptedTransport transport(std::vector<ScriptedTransport::Step>{{
      .bytes = {},
  }});
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kNeedMoreData);
  EXPECT_EQ(result.bytes_consumed, 0U);
}

TEST(StreamRuntimeTest, ReturnsSchemaMismatchPolicyForShortFrames) {
  const upr::CompiledProtocol protocol = make_blob_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 1,
      .max_payload_size = 16,
  });
  const std::vector<std::byte> bytes = make_length_prefixed_payload({0x02});
  upr::SpanTransport transport(upr::ByteSpan(bytes.data(), bytes.size()));
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kDecodeError);
  EXPECT_EQ(result.decode_status, upr::DecodeStatus::kSchemaMismatch);
  EXPECT_EQ(result.policy, upr::DecodeFailurePolicy::kQuarantine);
  EXPECT_EQ(result.bytes_consumed, 2U);
}

TEST(StreamRuntimeTest, ReturnsChecksumMismatchPolicyWhenChecksumVerificationFails) {
  const upr::CompiledProtocol protocol = make_checksum_protocol();
  upr::ProtocolDecoder decoder(protocol);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 1,
      .max_payload_size = 16,
  });
  const std::vector<std::byte> bytes = make_length_prefixed_payload({0x04, 0xAA, 0x00});
  upr::SpanTransport transport(upr::ByteSpan(bytes.data(), bytes.size()));
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kDecodeError);
  EXPECT_EQ(result.decode_status, upr::DecodeStatus::kChecksumMismatch);
  EXPECT_EQ(result.policy, upr::DecodeFailurePolicy::kQuarantine);
  EXPECT_EQ(result.bytes_consumed, 4U);
}

TEST(StreamRuntimeTest, ReturnsFieldLimitPolicyForMalformedCompiledProtocols) {
  std::vector<upr::CompiledField> fields;
  for (size_t index = 0; index <= upr::kMaxFieldsPerMessage; ++index) {
    upr::CompiledField field;
    field.id = static_cast<upr::FieldId>(index);
    field.name = "field_" + std::to_string(index);
    field.kind = upr::FieldKind::kUnsigned;
    field.width_bytes = 1;
    fields.push_back(std::move(field));
  }
  upr::CompiledMessage message_schema("Oversized", std::move(fields), {}, {}, 0, false);
  upr::CompiledProtocol protocol("manual", 5, {}, {message_schema});
  upr::ProtocolDecoder decoder(protocol);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 1,
      .max_payload_size = 16,
  });
  const std::vector<std::byte> bytes = make_length_prefixed_payload({});
  upr::SpanTransport transport(upr::ByteSpan(bytes.data(), bytes.size()));
  upr::StreamRuntime<8> runtime(transport, framer, decoder);
  upr::DecodedMessage message;

  const upr::PollResult result = runtime.poll(&message);
  EXPECT_EQ(result.status, upr::PollStatus::kDecodeError);
  EXPECT_EQ(result.decode_status, upr::DecodeStatus::kFieldLimitExceeded);
  EXPECT_EQ(result.policy, upr::DecodeFailurePolicy::kStop);
  EXPECT_EQ(result.bytes_consumed, 1U);
}

}  // namespace
