#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "packages/upr_runtime/tests/upr_general_direct_generated.hpp"
#include "universal_protocol_runtime/compiler/schema_compiler.hpp"
#include "universal_protocol_runtime/decoder/protocol_decoder.hpp"
#include "universal_protocol_runtime/pdl/yaml_loader.hpp"

namespace upr = universal_protocol_runtime;
namespace gdc = universal_protocol_runtime::gdc;

namespace {

std::filesystem::path schema_path() {
  if (const char* runfiles_dir = std::getenv("RUNFILES_DIR")) {
    return std::filesystem::path(runfiles_dir) / "_main" / "packages" / "upr_runtime" / "tests" /
           "general_direct_codec.upr";
  }
  if (const char* test_srcdir = std::getenv("TEST_SRCDIR")) {
    return std::filesystem::path(test_srcdir) / "_main" / "packages" / "upr_runtime" / "tests" /
           "general_direct_codec.upr";
  }
  return std::filesystem::path("packages") / "upr_runtime" / "tests" / "general_direct_codec.upr";
}

upr::CompiledProtocol compile() {
  upr::StatusOr<upr::ProtocolDefinition> definition = upr::load_protocol_definition_from_file(schema_path().string());
  EXPECT_TRUE(definition.ok()) << definition.status().message();
  upr::StatusOr<upr::CompiledProtocol> compiled = upr::compile_protocol(definition.value());
  EXPECT_TRUE(compiled.ok()) << compiled.status().message();
  return std::move(compiled).value();
}

// Collection of nested structs + a trailing checksum exercises the general
// direct path. The bytes produced by the generated direct encoder must be
// accepted and interpreted identically by the dynamic decoder.
TEST(GeneralDirectCodecTest, CollectionWithChecksumRoundTrips) {
  const upr::CompiledProtocol protocol = compile();
  const upr::ProtocolDecoder decoder(protocol);

  gdc::Book::Value value;
  value.message_type = 1;
  value.count = 2;
  value.pairs.push_back({.key = 7, .value = {.a = 11, .b = 0xDEADBEEFU}});
  value.pairs.push_back({.key = 9, .value = {.a = 22, .b = 0x01020304U}});
  value.checksum = 0;  // Filled by the encoder.

  std::array<std::byte, 256> buffer{};
  std::size_t written = 0;
  ASSERT_EQ(gdc::Book::encode_value_direct(value, upr::MutableByteSpan(buffer.data(), buffer.size()), &written),
            upr::EncodeStatus::kOk);
  ASSERT_GT(written, 0U);

  const upr::ByteSpan frame(buffer.data(), written);

  // Dynamic decode must accept the direct-encoded frame (including checksum).
  upr::DecodedMessage decoded;
  ASSERT_EQ(decoder.decode_as("Book", frame, &decoded), upr::DecodeStatus::kOk);
  EXPECT_EQ(decoded.get<uint8_t>(gdc::Book::Fields::kMessageType).value(), 1U);
  EXPECT_EQ(decoded.get<uint8_t>(gdc::Book::Fields::kCount).value(), 2U);
  const auto collection = decoded.get_collection(gdc::Book::Fields::kPairs);
  ASSERT_TRUE(collection.has_value());
  ASSERT_EQ(collection->count(), 2U);
  const auto pair0 = collection->at(0);
  ASSERT_TRUE(pair0.has_value());
  EXPECT_EQ(pair0->get<uint16_t>(gdc::Pair::Fields::kKey).value(), 7U);
  const auto inner0 = pair0->get_struct(gdc::Pair::Fields::kValue);
  ASSERT_TRUE(inner0.has_value());
  EXPECT_EQ(inner0->get<uint16_t>(gdc::Inner::Fields::kA).value(), 11U);
  EXPECT_EQ(inner0->get<uint32_t>(gdc::Inner::Fields::kB).value(), 0xDEADBEEFU);

  // Direct decode must round-trip back to the original value.
  gdc::Book::Value round;
  ASSERT_EQ(gdc::Book::decode_value_direct(frame, &round), upr::DecodeStatus::kOk);
  EXPECT_EQ(round.message_type, 1U);
  EXPECT_EQ(round.count, 2U);
  ASSERT_EQ(round.pairs.size(), 2U);
  EXPECT_EQ(round.pairs[0].key, 7U);
  EXPECT_EQ(round.pairs[0].value.a, 11U);
  EXPECT_EQ(round.pairs[0].value.b, 0xDEADBEEFU);
  EXPECT_EQ(round.pairs[1].key, 9U);
  EXPECT_EQ(round.pairs[1].value.b, 0x01020304U);

  // A corrupted checksum byte must be rejected by the direct decoder.
  std::array<std::byte, 256> tampered = buffer;
  tampered[written - 1] = static_cast<std::byte>(static_cast<uint8_t>(tampered[written - 1]) ^ 0xFFU);
  gdc::Book::Value rejected;
  EXPECT_EQ(gdc::Book::decode_value_direct(upr::ByteSpan(tampered.data(), written), &rejected),
            upr::DecodeStatus::kChecksumMismatch);
}

TEST(GeneralDirectCodecTest, VariantRoundTrips) {
  const upr::CompiledProtocol protocol = compile();
  const upr::ProtocolDecoder decoder(protocol);

  gdc::Event::Value value;
  value.message_type = 2;
  value.kind = 1;
  value.detail.emplace<1>(gdc::QuoteDetail::Value{.bid = 100U, .ask = 105U});

  std::array<std::byte, 64> buffer{};
  std::size_t written = 0;
  ASSERT_EQ(gdc::Event::encode_value_direct(value, upr::MutableByteSpan(buffer.data(), buffer.size()), &written),
            upr::EncodeStatus::kOk);

  const upr::ByteSpan frame(buffer.data(), written);
  upr::DecodedMessage decoded;
  ASSERT_EQ(decoder.decode_as("Event", frame, &decoded), upr::DecodeStatus::kOk);
  const auto variant = decoded.get_variant(gdc::Event::Fields::kDetail);
  ASSERT_TRUE(variant.has_value());
  EXPECT_EQ(variant->get<uint32_t>(gdc::QuoteDetail::Fields::kBid).value(), 100U);
  EXPECT_EQ(variant->get<uint32_t>(gdc::QuoteDetail::Fields::kAsk).value(), 105U);

  gdc::Event::Value round;
  ASSERT_EQ(gdc::Event::decode_value_direct(frame, &round), upr::DecodeStatus::kOk);
  ASSERT_EQ(round.detail.index(), 1U);
  EXPECT_EQ(std::get<1>(round.detail).bid, 100U);
  EXPECT_EQ(std::get<1>(round.detail).ask, 105U);
}

TEST(GeneralDirectCodecTest, PresenceGatedFieldsRoundTrip) {
  const upr::CompiledProtocol protocol = compile();
  const upr::ProtocolDecoder decoder(protocol);

  // Present case.
  {
    gdc::Note::Value value;
    value.message_type = 3;
    value.presence = 0x01;
    value.note_len = 5;
    value.note = "hello";

    std::array<std::byte, 64> buffer{};
    std::size_t written = 0;
    ASSERT_EQ(gdc::Note::encode_value_direct(value, upr::MutableByteSpan(buffer.data(), buffer.size()), &written),
              upr::EncodeStatus::kOk);
    const upr::ByteSpan frame(buffer.data(), written);

    upr::DecodedMessage decoded;
    ASSERT_EQ(decoder.decode_as("Note", frame, &decoded), upr::DecodeStatus::kOk);
    EXPECT_EQ(decoded.get_string_view(gdc::Note::Fields::kNote).value(), "hello");

    gdc::Note::Value round;
    ASSERT_EQ(gdc::Note::decode_value_direct(frame, &round), upr::DecodeStatus::kOk);
    EXPECT_EQ(round.note_len, 5U);
    EXPECT_EQ(round.note, "hello");
  }

  // Absent case: gated fields contribute no bytes.
  {
    gdc::Note::Value value;
    value.message_type = 3;
    value.presence = 0x00;

    std::array<std::byte, 64> buffer{};
    std::size_t written = 0;
    ASSERT_EQ(gdc::Note::encode_value_direct(value, upr::MutableByteSpan(buffer.data(), buffer.size()), &written),
              upr::EncodeStatus::kOk);
    EXPECT_EQ(written, 2U);  // message_type + presence only

    const upr::ByteSpan frame(buffer.data(), written);
    upr::DecodedMessage decoded;
    ASSERT_EQ(decoder.decode_as("Note", frame, &decoded), upr::DecodeStatus::kOk);

    gdc::Note::Value round;
    ASSERT_EQ(gdc::Note::decode_value_direct(frame, &round), upr::DecodeStatus::kOk);
    EXPECT_EQ(round.presence, 0U);
  }
}

// A truncated frame must surface the failing field name and byte offset via the
// rich DecodeError out-parameter rather than just a bare status code.
TEST(GeneralDirectCodecTest, RichDecodeErrorReportsFieldAndOffset) {
  gdc::Book::Value value;
  value.message_type = 1;
  value.count = 2;
  value.pairs.push_back({.key = 7, .value = {.a = 11, .b = 0xDEADBEEFU}});
  value.pairs.push_back({.key = 9, .value = {.a = 22, .b = 0x01020304U}});

  std::array<std::byte, 256> buffer{};
  std::size_t written = 0;
  ASSERT_EQ(gdc::Book::encode_value_direct(value, upr::MutableByteSpan(buffer.data(), buffer.size()), &written),
            upr::EncodeStatus::kOk);

  // Drop the final checksum byte so the 2-byte checksum read runs short.
  const upr::ByteSpan truncated(buffer.data(), written - 1);
  gdc::Book::Value round;
  upr::DecodeError error;
  EXPECT_EQ(gdc::Book::decode_value_direct(truncated, &round, nullptr, &error), upr::DecodeStatus::kSchemaMismatch);
  EXPECT_EQ(error.status, upr::DecodeStatus::kSchemaMismatch);
  EXPECT_EQ(error.field_name, "checksum");
  EXPECT_EQ(error.byte_offset, written - 2U);  // checksum begins two bytes from the end

  // A tampered checksum reports the checksum field with kChecksumMismatch.
  std::array<std::byte, 256> tampered = buffer;
  tampered[written - 1] = static_cast<std::byte>(static_cast<uint8_t>(tampered[written - 1]) ^ 0xFFU);
  upr::DecodeError checksum_error;
  EXPECT_EQ(gdc::Book::decode_value_direct(upr::ByteSpan(tampered.data(), written), &round, nullptr, &checksum_error),
            upr::DecodeStatus::kChecksumMismatch);
  EXPECT_EQ(checksum_error.status, upr::DecodeStatus::kChecksumMismatch);
  EXPECT_EQ(checksum_error.field_name, "checksum");

  // Nested decode failures report offsets from the top-level frame, not from
  // the nested subspan handed to the child decoder.
  const std::array<std::byte, 4> nested_truncated{std::byte{0x01}, std::byte{0x01}, std::byte{0x07}, std::byte{0x00}};
  upr::DecodeError nested_error;
  EXPECT_EQ(gdc::Book::decode_value_direct(
                upr::ByteSpan(nested_truncated.data(), nested_truncated.size()), &round, nullptr, &nested_error),
            upr::DecodeStatus::kSchemaMismatch);
  EXPECT_EQ(nested_error.byte_offset, 4U);

  // A clean decode clears the error.
  upr::DecodeError ok_error;
  ASSERT_EQ(gdc::Book::decode_value_direct(upr::ByteSpan(buffer.data(), written), &round, nullptr, &ok_error),
            upr::DecodeStatus::kOk);
  EXPECT_TRUE(ok_error.ok());
}

// decode_sequence decodes packed struct records without manual bookkeeping.
TEST(GeneralDirectCodecTest, DecodeSequenceDecodesPackedRecords) {
  gdc::Pair::Value first{.key = 7, .value = {.a = 11, .b = 0xDEADBEEFU}};
  gdc::Pair::Value second{.key = 9, .value = {.a = 22, .b = 0x01020304U}};

  std::array<std::byte, 64> buffer{};
  std::size_t offset = 0;
  std::size_t consumed = 0;
  ASSERT_EQ(gdc::Pair::encode_value_direct(
                first, upr::MutableByteSpan(buffer.data() + offset, buffer.size() - offset), &consumed),
            upr::EncodeStatus::kOk);
  offset += consumed;
  ASSERT_EQ(gdc::Pair::encode_value_direct(
                second, upr::MutableByteSpan(buffer.data() + offset, buffer.size() - offset), &consumed),
            upr::EncodeStatus::kOk);
  offset += consumed;

  std::vector<gdc::Pair::Value> records;
  std::size_t record_count = 0;
  upr::DecodeError error;
  ASSERT_EQ(gdc::Pair::decode_sequence(upr::ByteSpan(buffer.data(), offset), &records, &record_count, &error),
            upr::DecodeStatus::kOk);
  EXPECT_EQ(record_count, 2U);
  ASSERT_EQ(records.size(), 2U);
  EXPECT_EQ(records[0].key, 7U);
  EXPECT_EQ(records[0].value.b, 0xDEADBEEFU);
  EXPECT_EQ(records[1].key, 9U);
  EXPECT_EQ(records[1].value.b, 0x01020304U);
  EXPECT_TRUE(error.ok());

  // A trailing partial record is reported as a schema mismatch.
  std::vector<gdc::Pair::Value> partial_records;
  upr::DecodeError partial_error;
  EXPECT_EQ(
      gdc::Pair::decode_sequence(upr::ByteSpan(buffer.data(), offset - 1), &partial_records, nullptr, &partial_error),
      upr::DecodeStatus::kSchemaMismatch);
}

}  // namespace
