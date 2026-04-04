#include "universal_protocol_runtime/discovery/protocol_discovery.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

#include "detail/test_support.hpp"

namespace upr = universal_protocol_runtime;

namespace {

std::vector<std::byte> make_frame(std::initializer_list<uint8_t> values) {
  return upr_test_support::make_bytes(values);
}

TEST(ProtocolDiscoveryTest, InfersLengthDelimitedPayloadMessages) {
  const std::vector<std::vector<std::byte>> frames = {
      make_frame({0x42, 0x03, 'C', 'A', 'T', 0x99}),
      make_frame({0x42, 0x05, 'D', 'O', 'G', 'G', 'O', 0x01}),
      make_frame({0x42, 0x01, '!', 0xAA}),
  };

  upr::StatusOr<upr::DiscoveryReport> report = upr::discover_protocol_from_samples(frames, {.protocol_name = "pets"});

  ASSERT_TRUE(report.ok()) << report.status().message();
  ASSERT_EQ(report.value().messages.size(), 1U);
  const upr::DiscoveredMessage& message = report.value().messages.front();
  EXPECT_EQ(message.name, "Message_42");
  EXPECT_TRUE(message.variable_length);
  ASSERT_TRUE(message.inferred_length_field.has_value());
  EXPECT_EQ(message.inferred_length_field->offset, 1U);
  EXPECT_EQ(message.inferred_length_field->width_bytes, 1U);
  EXPECT_EQ(message.inferred_length_field->trailing_fixed_bytes, 1U);
  ASSERT_EQ(message.draft_message.fields.size(), 4U);
  EXPECT_EQ(message.draft_message.fields[0].name, "message_type");
  EXPECT_EQ(message.draft_message.fields[1].name, "length");
  EXPECT_EQ(message.draft_message.fields[2].name, "payload");
  EXPECT_EQ(message.draft_message.fields[2].size_from_field, "length");
  EXPECT_EQ(message.draft_message.fields[3].name, "trailer");
  EXPECT_NE(report.value().draft_fingerprint, 0U);
}

TEST(ProtocolDiscoveryTest, ClustersFramesByFirstByteAndBuildsCompileableDraft) {
  const std::vector<std::vector<std::byte>> frames = {
      make_frame({0x01, 'A', 'A', 'P', 'L', 0x10, 0x00}),
      make_frame({0x01, 'M', 'S', 'F', 'T', 0x22, 0x00}),
      make_frame({0x02, 0x03, 0x10, 0x11, 0x12}),
      make_frame({0x02, 0x02, 0x20, 0x21}),
  };

  upr::StatusOr<upr::DiscoveryReport> report =
      upr::discover_protocol_from_samples(frames, {.protocol_name = "mixed_stream"});

  ASSERT_TRUE(report.ok()) << report.status().message();
  ASSERT_EQ(report.value().messages.size(), 2U);
  EXPECT_EQ(report.value().draft_protocol.messages.size(), 2U);
  EXPECT_EQ(report.value().draft_protocol.messages[0].name, "Message_01");
  EXPECT_EQ(report.value().draft_protocol.messages[1].name, "Message_02");
  EXPECT_FALSE(report.value().messages[0].allow_trailing_bytes);
  EXPECT_TRUE(report.value().messages[1].inferred_length_field.has_value());

  upr::StatusOr<upr::CompiledProtocol> compiled = upr::compile_protocol(report.value().draft_protocol);
  ASSERT_TRUE(compiled.ok()) << compiled.status().message();
  EXPECT_EQ(compiled.value().messages().size(), 2U);
}

TEST(ProtocolDiscoveryTest, FallsBackToTrailingBytesWhenNoLengthFieldIsRecognized) {
  const std::vector<std::vector<std::byte>> frames = {
      make_frame({0x99, 0x10, 0x11}),
      make_frame({0x99, 0x10, 0x11, 0x12}),
      make_frame({0x99, 0x10, 0x11, 0x12, 0x13}),
  };

  upr::StatusOr<upr::DiscoveryReport> report =
      upr::discover_protocol_from_samples(frames, {.protocol_name = "fallback"});

  ASSERT_TRUE(report.ok()) << report.status().message();
  ASSERT_EQ(report.value().messages.size(), 1U);
  EXPECT_FALSE(report.value().messages.front().inferred_length_field.has_value());
  EXPECT_TRUE(report.value().messages.front().allow_trailing_bytes);
  EXPECT_TRUE(report.value().draft_protocol.messages.front().allow_trailing_bytes);
}

TEST(ProtocolDiscoveryTest, ValidatesInputsAndTracksDiscardedEmptyFrames) {
  EXPECT_FALSE(upr::discover_protocol_from_samples({make_frame({0x01})}, {.protocol_name = ""}).ok());
  EXPECT_FALSE(upr::discover_protocol_from_samples({make_frame({0x01})}, {.sample_frames_per_message = 0}).ok());
  EXPECT_FALSE(upr::discover_protocol_from_samples({}).ok());
  EXPECT_FALSE(upr::discover_protocol_from_samples({{}, {}}).ok());

  upr::StatusOr<upr::DiscoveryReport> report =
      upr::discover_protocol_from_samples({{}, make_frame({0x33, 0x44}), {}}, {.protocol_name = "trimmed"});

  ASSERT_TRUE(report.ok()) << report.status().message();
  EXPECT_EQ(report.value().frames_analyzed, 1U);
  EXPECT_EQ(report.value().frames_discarded, 2U);
  ASSERT_EQ(report.value().messages.size(), 1U);
  EXPECT_EQ(report.value().messages.front().name, "Message_33");
}

TEST(ProtocolDiscoveryTest, SupportsNonClusteredStreamsAndBigEndianLengthFields) {
  const std::vector<std::vector<std::byte>> frames = {
      make_frame({0x10, 0x00, 0x03, 'C', 'A', 'T'}),
      make_frame({0x20, 0x00, 0x04, 'D', 'O', 'G', 'O'}),
  };

  upr::StatusOr<upr::DiscoveryReport> report = upr::discover_protocol_from_samples(frames,
                                                                                   {
                                                                                       .protocol_name = "merged",
                                                                                       .sample_frames_per_message = 1,
                                                                                       .cluster_by_first_byte = false,
                                                                                   });

  ASSERT_TRUE(report.ok()) << report.status().message();
  ASSERT_EQ(report.value().messages.size(), 1U);
  const upr::DiscoveredMessage& message = report.value().messages.front();
  ASSERT_TRUE(message.inferred_length_field.has_value());
  EXPECT_EQ(message.inferred_length_field->offset, 1U);
  EXPECT_EQ(message.inferred_length_field->width_bytes, 2U);
  EXPECT_EQ(message.inferred_length_field->byte_order, upr::ByteOrder::kBigEndian);
  EXPECT_EQ(message.sample_frames.size(), 1U);
  ASSERT_EQ(message.draft_message.fields.size(), 3U);
  EXPECT_EQ(message.draft_message.fields[0].name, "field_0");
  EXPECT_EQ(message.draft_message.fields[0].kind, upr::FieldKind::kUnsigned);
  EXPECT_EQ(message.draft_message.fields[1].name, "length");
  EXPECT_EQ(message.draft_message.fields[2].name, "payload");
}

TEST(ProtocolDiscoveryTest, DraftsFixedBytesAsciiRunsAndSingleByteFields) {
  const std::vector<std::vector<std::byte>> frames = {
      make_frame({0x7E, 0x01, 0x10, 'A', 'B', 'C', 0x11, 0x80, 0x81}),
      make_frame({0x7E, 0x02, 0x10, 'D', 'E', 'F', 0x11, 0x82, 0x83}),
  };

  upr::StatusOr<upr::DiscoveryReport> report =
      upr::discover_protocol_from_samples(frames, {.protocol_name = "fixed_shapes"});

  ASSERT_TRUE(report.ok()) << report.status().message();
  ASSERT_EQ(report.value().messages.size(), 1U);
  const auto& fields = report.value().messages.front().draft_message.fields;
  ASSERT_EQ(fields.size(), 6U);
  EXPECT_EQ(fields[0].name, "message_type");
  EXPECT_EQ(fields[1].name, "field_0");
  EXPECT_EQ(fields[1].kind, upr::FieldKind::kUnsigned);
  EXPECT_EQ(fields[2].name, "fixed_2");
  EXPECT_TRUE(fields[2].has_expected_unsigned);
  EXPECT_EQ(fields[3].name, "text_0");
  EXPECT_EQ(fields[3].kind, upr::FieldKind::kString);
  EXPECT_EQ(fields[3].string_encoding, upr::StringEncoding::kAscii);
  EXPECT_EQ(fields[4].name, "fixed_6");
  EXPECT_TRUE(fields[4].has_expected_unsigned);
  EXPECT_EQ(fields[5].name, "bytes_0");
  EXPECT_EQ(fields[5].kind, upr::FieldKind::kBytes);
}

TEST(ProtocolDiscoveryTest, ReportsCompilationFailuresForOversizedDraftMessages) {
  std::vector<std::byte> first(131, std::byte{0x00});
  std::vector<std::byte> second(131, std::byte{0x00});
  first[0] = std::byte{0xAA};
  second[0] = std::byte{0xAA};
  for (size_t field_index = 0; field_index < 65; ++field_index) {
    const size_t variable_offset = 1U + (field_index * 2U);
    const size_t fixed_offset = variable_offset + 1U;
    first[variable_offset] = static_cast<std::byte>(field_index);
    second[variable_offset] = static_cast<std::byte>(field_index + 1U);
    first[fixed_offset] = static_cast<std::byte>(0x80U + field_index);
    second[fixed_offset] = static_cast<std::byte>(0x80U + field_index);
  }

  upr::StatusOr<upr::DiscoveryReport> report =
      upr::discover_protocol_from_samples({first, second}, {.protocol_name = "overflow"});

  ASSERT_FALSE(report.ok());
  EXPECT_NE(std::string(report.status().message()).find("did not compile cleanly"), std::string::npos);
}

}  // namespace
