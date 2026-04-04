#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "detail/test_support.hpp"
#include "universal_protocol_runtime/compiler/compiled_protocol.hpp"
#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/core/types.hpp"
#include "universal_protocol_runtime/decoder/protocol_decoder.hpp"
#include "universal_protocol_runtime/framing/frame_result.hpp"
#include "universal_protocol_runtime/runtime/stream_runtime.hpp"
#include "universal_protocol_runtime/transport/span_transport.hpp"
#include "utils/status.hpp"
#include "utils/xxhash64.hpp"

namespace upr = universal_protocol_runtime;

namespace {

enum class HashEnum : uint8_t {
  KAlpha = 1,
};

template <typename T>
T sample_value();

template <>
int sample_value<int>() {
  return 42;
}

template <>
std::string sample_value<std::string>() {
  return "value";
}

template <typename T>
class StatusOrTypedTest : public ::testing::Test {
 public:
  ~StatusOrTypedTest() noexcept override = default;
};

using StatusOrTypes = ::testing::Types<int, std::string>;
TYPED_TEST_SUITE(StatusOrTypedTest, StatusOrTypes);

TYPED_TEST(StatusOrTypedTest, SupportsValueAccessAcrossReferenceCategories) {
  const TypeParam expected = sample_value<TypeParam>();
  upr::StatusOr<TypeParam> status_or(expected);
  const upr::StatusOr<TypeParam> const_status_or(expected);

  ASSERT_TRUE(status_or.ok());
  EXPECT_TRUE(status_or.status().ok());
  EXPECT_EQ(const_status_or.value(), expected);
  EXPECT_EQ(status_or.value(), expected);
  EXPECT_EQ(std::move(status_or).value(), expected);
}

TYPED_TEST(StatusOrTypedTest, PreservesErrorStatus) {
  upr::StatusOr<TypeParam> status_or(upr::decode_error("bad_payload"));

  EXPECT_FALSE(status_or.ok());
  EXPECT_EQ(status_or.status().code(), upr::StatusCode::kDecodeError);
  EXPECT_EQ(status_or.status().message(), "bad_payload");
}

TEST(CoreUtilityTest, ConvertsEnumsToStringsIncludingUnknownFallbacks) {
  EXPECT_EQ(upr::to_string(upr::ByteOrder::kLittleEndian), "little_endian");
  EXPECT_EQ(upr::to_string(upr::ByteOrder::kBigEndian), "big_endian");
  EXPECT_EQ(upr::to_string(static_cast<upr::ByteOrder>(99)), "unknown");

  EXPECT_EQ(upr::to_string(upr::FieldKind::kUnsigned), "unsigned");
  EXPECT_EQ(upr::to_string(upr::FieldKind::kSigned), "signed");
  EXPECT_EQ(upr::to_string(upr::FieldKind::kFloat32), "float32");
  EXPECT_EQ(upr::to_string(upr::FieldKind::kFloat64), "float64");
  EXPECT_EQ(upr::to_string(upr::FieldKind::kBytes), "bytes");
  EXPECT_EQ(upr::to_string(upr::FieldKind::kString), "string");
  EXPECT_EQ(upr::to_string(upr::FieldKind::kStruct), "struct");
  EXPECT_EQ(upr::to_string(upr::FieldKind::kEnum), "enum");
  EXPECT_EQ(upr::to_string(static_cast<upr::FieldKind>(99)), "unknown");

  EXPECT_EQ(upr::to_string(upr::StringEncoding::kAscii), "ascii");
  EXPECT_EQ(upr::to_string(upr::StringEncoding::kUtf8), "utf8");
  EXPECT_EQ(upr::to_string(static_cast<upr::StringEncoding>(99)), "unknown");

  EXPECT_EQ(upr::to_string(upr::FrameStatus::kReady), "ready");
  EXPECT_EQ(upr::to_string(upr::FrameStatus::kNeedMoreData), "need_more_data");
  EXPECT_EQ(upr::to_string(upr::FrameStatus::kInvalidFrame), "invalid_frame");
  EXPECT_EQ(upr::to_string(static_cast<upr::FrameStatus>(99)), "unknown");

  EXPECT_EQ(upr::to_string(upr::PollStatus::kMessageReady), "message_ready");
  EXPECT_EQ(upr::to_string(upr::PollStatus::kNeedMoreData), "need_more_data");
  EXPECT_EQ(upr::to_string(upr::PollStatus::kEndOfStream), "end_of_stream");
  EXPECT_EQ(upr::to_string(upr::PollStatus::kFrameInvalid), "frame_invalid");
  EXPECT_EQ(upr::to_string(upr::PollStatus::kDecodeError), "decode_error");
  EXPECT_EQ(upr::to_string(upr::PollStatus::kBufferExhausted), "buffer_exhausted");
  EXPECT_EQ(upr::to_string(upr::PollStatus::kTransportError), "transport_error");
  EXPECT_EQ(upr::to_string(static_cast<upr::PollStatus>(99)), "unknown");

  EXPECT_EQ(upr::to_string(upr::DecodeFailurePolicy::kStop), "stop");
  EXPECT_EQ(upr::to_string(upr::DecodeFailurePolicy::kDropAndContinue), "drop_and_continue");
  EXPECT_EQ(upr::to_string(upr::DecodeFailurePolicy::kQuarantine), "quarantine");
  EXPECT_EQ(upr::to_string(static_cast<upr::DecodeFailurePolicy>(99)), "unknown");

  EXPECT_EQ(upr::to_string(upr::DecodeStatus::kOk), "ok");
  EXPECT_EQ(upr::to_string(upr::DecodeStatus::kMessageNotFound), "message_not_found");
  EXPECT_EQ(upr::to_string(upr::DecodeStatus::kSchemaMismatch), "schema_mismatch");
  EXPECT_EQ(upr::to_string(upr::DecodeStatus::kInvalidData), "invalid_data");
  EXPECT_EQ(upr::to_string(upr::DecodeStatus::kChecksumMismatch), "checksum_mismatch");
  EXPECT_EQ(upr::to_string(upr::DecodeStatus::kFieldLimitExceeded), "field_limit_exceeded");
  EXPECT_EQ(upr::to_string(static_cast<upr::DecodeStatus>(99)), "unknown");

  EXPECT_EQ(upr::to_string(upr::StatusCode::kOk), "ok");
  EXPECT_EQ(upr::to_string(upr::StatusCode::kInvalidArgument), "invalid_argument");
  EXPECT_EQ(upr::to_string(upr::StatusCode::kNotFound), "not_found");
  EXPECT_EQ(upr::to_string(upr::StatusCode::kSchemaError), "schema_error");
  EXPECT_EQ(upr::to_string(upr::StatusCode::kDecodeError), "decode_error");
  EXPECT_EQ(upr::to_string(upr::StatusCode::kIoError), "io_error");
  EXPECT_EQ(upr::to_string(upr::StatusCode::kExhausted), "exhausted");
  EXPECT_EQ(upr::to_string(static_cast<upr::StatusCode>(99)), "unknown");
}

TEST(CoreUtilityTest, BuildsByteViewsOverMutableAndConstBuffers) {
  std::array<uint8_t, 3> values = {1, 2, 3};

  const upr::ByteSpan readable = upr::as_byte_span(std::span<const uint8_t>(values));
  upr::MutableByteSpan writable = upr::as_writable_byte_span(std::span<uint8_t>(values));

  ASSERT_EQ(readable.size(), 3U);
  EXPECT_EQ(readable[0], std::byte{0x01});
  writable[1] = std::byte{0xFE};
  EXPECT_EQ(values[1], 0xFEU);
}

TEST(CoreUtilityTest, ProducesDeterministicDistinctXxHash64Fingerprints) {
  const std::vector<std::byte> bytes = upr_test_support::make_bytes({0xAA, 0xBB, 0xCC});
  upr::XxHash64State streamed_hash;
  streamed_hash.update(std::span<const std::byte>(bytes.data(), 1));
  streamed_hash.update(std::span<const std::byte>(bytes.data() + 1, bytes.size() - 1));

  const uint64_t hash_empty = upr::xxhash64(std::span<const std::byte>());
  const uint64_t hash_a = upr::xxhash64(bytes);
  const uint64_t hash_b = upr::xxhash64("abc");
  const uint64_t hash_c = upr::xxhash64_integral(42U);
  const uint64_t hash_d = upr::xxhash64_integral(HashEnum::KAlpha);
  const uint64_t hash_true = upr::xxhash64_bool(true);
  const uint64_t hash_false = upr::xxhash64_bool(false);

  EXPECT_EQ(hash_empty, 0xEF46DB3751D8E999ULL);
  EXPECT_EQ(hash_a, streamed_hash.digest());
  EXPECT_NE(hash_a, hash_b);
  EXPECT_NE(hash_b, hash_c);
  EXPECT_NE(hash_c, hash_d);
  EXPECT_NE(hash_true, hash_false);
}

TEST(CoreUtilityTest, BuildsStatusesThroughHelperFunctions) {
  const upr::Status invalid = upr::invalid_argument("bad_arg");
  const upr::Status missing = upr::not_found("missing");
  const upr::Status schema = upr::schema_error("bad_schema");
  const upr::Status decode = upr::decode_error("bad_decode");
  const upr::Status io = upr::io_error("bad_io");
  const upr::Status exhausted = upr::exhausted("done");

  EXPECT_FALSE(invalid.ok());
  EXPECT_EQ(invalid.message(), "bad_arg");
  EXPECT_EQ(missing.code(), upr::StatusCode::kNotFound);
  EXPECT_EQ(schema.code(), upr::StatusCode::kSchemaError);
  EXPECT_EQ(decode.code(), upr::StatusCode::kDecodeError);
  EXPECT_EQ(io.code(), upr::StatusCode::kIoError);
  EXPECT_EQ(exhausted.code(), upr::StatusCode::kExhausted);
}

TEST(CoreUtilityTest, ReportsCompiledFieldSizeContributions) {
  upr::CompiledField scalar;
  scalar.kind = upr::FieldKind::kUnsigned;
  scalar.width_bytes = 4;

  upr::CompiledField fixed_bytes;
  fixed_bytes.kind = upr::FieldKind::kBytes;
  fixed_bytes.fixed_size = 12;

  upr::CompiledField fixed_string;
  fixed_string.kind = upr::FieldKind::kString;
  fixed_string.fixed_size = 7;

  upr::CompiledField nested_struct;
  nested_struct.kind = upr::FieldKind::kStruct;
  nested_struct.fixed_size = 24;

  upr::CompiledField dynamic_bytes;
  dynamic_bytes.kind = upr::FieldKind::kBytes;
  dynamic_bytes.fixed_size = 12;
  dynamic_bytes.dynamic_size = true;

  EXPECT_TRUE(scalar.is_scalar());
  EXPECT_EQ(scalar.minimum_size_contribution(), 4U);
  EXPECT_FALSE(fixed_bytes.is_scalar());
  EXPECT_EQ(fixed_bytes.minimum_size_contribution(), 12U);
  EXPECT_EQ(fixed_string.minimum_size_contribution(), 7U);
  EXPECT_EQ(nested_struct.minimum_size_contribution(), 24U);
  EXPECT_EQ(dynamic_bytes.minimum_size_contribution(), 0U);
}

TEST(CoreUtilityTest, SpanTransportHandlesChunkingEmptyInputsAndClosedReads) {
  const std::vector<std::byte> source = upr_test_support::make_bytes({0x10, 0x20, 0x30});
  upr::SpanTransport transport(upr::ByteSpan(source.data(), source.size()), 2);
  std::array<std::byte, 2> first_chunk{};
  std::array<std::byte, 2> second_chunk{};

  upr::ReadResult first = transport.read(first_chunk);
  ASSERT_EQ(first.bytes_read, 2U);
  EXPECT_FALSE(first.end_of_stream);
  EXPECT_TRUE(transport.is_open());

  upr::ReadResult second = transport.read(second_chunk);
  ASSERT_EQ(second.bytes_read, 1U);
  EXPECT_TRUE(second.end_of_stream);
  EXPECT_FALSE(transport.is_open());

  upr::ReadResult third = transport.read(second_chunk);
  EXPECT_EQ(third.bytes_read, 0U);
  EXPECT_TRUE(third.end_of_stream);

  const std::vector<std::byte> empty_source;
  upr::SpanTransport empty_transport(upr::ByteSpan(empty_source.data(), empty_source.size()));
  upr::ReadResult empty = empty_transport.read(first_chunk);
  EXPECT_EQ(empty.bytes_read, 0U);
  EXPECT_TRUE(empty.end_of_stream);
  EXPECT_FALSE(empty_transport.is_open());
}

TEST(CoreUtilityTest, CompiledProtocolLookupsReturnNullWhenAbsent) {
  const upr::CompiledProtocol protocol = upr_test_support::compile_protocol_or_throw(upr_test_support::make_protocol(
      "lookup",
      {upr_test_support::make_message(
          "Message",
          {
              upr_test_support::make_scalar_field(
                  "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 1),
              upr_test_support::make_scalar_field("value", upr::FieldKind::kUnsigned, 2),
          })}));

  const upr::CompiledMessage* message = protocol.find_message("Message");
  ASSERT_NE(message, nullptr);
  EXPECT_EQ(protocol.name(), "lookup");
  EXPECT_FALSE(protocol.find_message("Missing"));
  EXPECT_FALSE(message->find_field("missing").has_value());
}

}  // namespace
