#include "universal_protocol_runtime/compiler/checksum_registry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "detail/test_support.hpp"

namespace upr = universal_protocol_runtime;

namespace {

constexpr uint64_t custom_folded_sum(upr::ByteSpan bytes) noexcept {
  uint64_t value = 0;
  for (const std::byte byte : bytes) {
    value = (value * 131U) + std::to_integer<uint8_t>(byte);
  }
  return value & 0xFFFFU;
}

upr::ByteSpan checksum_input_span(const std::vector<std::byte>& input) {
  return upr::ByteSpan(input.data(), input.size());
}

struct BuiltinChecksumCase {
  std::string lookup_name;
  std::string canonical_name;
  uint8_t result_width_bytes = 0;
  uint64_t expected_value = 0;
};

class BuiltinChecksumRegistryTest : public ::testing::TestWithParam<BuiltinChecksumCase> {
 public:
  ~BuiltinChecksumRegistryTest() noexcept override = default;
};

TEST_P(BuiltinChecksumRegistryTest, ResolvesBuiltinAlgorithmsAndComputesReferenceVectors) {
  const BuiltinChecksumCase& param = GetParam();
  const std::vector<std::byte> input =
      upr_test_support::make_bytes({0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39});

  const upr::StatusOr<upr::ChecksumAlgorithmSpec> spec = upr::find_checksum_algorithm(param.lookup_name);

  ASSERT_TRUE(spec.ok()) << spec.status().message();
  EXPECT_EQ(spec.value().name, param.canonical_name);
  EXPECT_EQ(spec.value().result_width_bytes, param.result_width_bytes);
  ASSERT_NE(spec.value().function, nullptr);
  EXPECT_EQ(spec.value().function(checksum_input_span(input)), param.expected_value);
}

INSTANTIATE_TEST_SUITE_P(Coverage,
                         BuiltinChecksumRegistryTest,
                         ::testing::Values(
                             BuiltinChecksumCase{
                                 .lookup_name = "CRC16_CCITT",
                                 .canonical_name = "crc16_ccitt",
                                 .result_width_bytes = 2,
                                 .expected_value = 0x906EU,
                             },
                             BuiltinChecksumCase{
                                 .lookup_name = "crc32",
                                 .canonical_name = "crc32",
                                 .result_width_bytes = 4,
                                 .expected_value = 0xCBF43926U,
                             },
                             BuiltinChecksumCase{
                                 .lookup_name = "CrC32C",
                                 .canonical_name = "crc32c",
                                 .result_width_bytes = 4,
                                 .expected_value = 0xE3069283U,
                             },
                             BuiltinChecksumCase{
                                 .lookup_name = "XOR8",
                                 .canonical_name = "xor8",
                                 .result_width_bytes = 1,
                                 .expected_value = 0x31U,
                             },
                             BuiltinChecksumCase{
                                 .lookup_name = "sum16",
                                 .canonical_name = "sum16",
                                 .result_width_bytes = 2,
                                 .expected_value = 0x01DDU,
                             }),
                         [](const ::testing::TestParamInfo<BuiltinChecksumCase>& info) {
                           return info.param.canonical_name;
                         });

TEST(ChecksumRegistryTest, RegistersVendorAlgorithmsWithCaseInsensitiveLookupAndDuplicateProtection) {
  static constexpr std::string_view kAlgorithmName = "Vendor_Folded_20260404";
  const upr::Status registered = upr::register_checksum_algorithm({
      .name = std::string(kAlgorithmName),
      .result_width_bytes = 2,
      .function = &custom_folded_sum,
  });
  ASSERT_TRUE(registered.ok()) << registered.message();

  const auto spec = upr::find_checksum_algorithm("vendor_folded_20260404");
  ASSERT_TRUE(spec.ok()) << spec.status().message();
  EXPECT_EQ(spec.value().name, "vendor_folded_20260404");
  EXPECT_EQ(spec.value().result_width_bytes, 2U);
  ASSERT_NE(spec.value().function, nullptr);

  const std::vector<std::byte> input = upr_test_support::make_bytes({0x10, 0x20, 0x30});
  EXPECT_EQ(spec.value().function(checksum_input_span(input)), custom_folded_sum(checksum_input_span(input)));

  const upr::Status duplicate = upr::register_checksum_algorithm({
      .name = "VENDOR_FOLDED_20260404",
      .result_width_bytes = 2,
      .function = &custom_folded_sum,
  });
  EXPECT_FALSE(duplicate.ok());
  EXPECT_EQ(duplicate.code(), upr::StatusCode::kInvalidArgument);
  EXPECT_NE(std::string(duplicate.message()).find("Duplicate checksum algorithm"), std::string::npos);
}

struct InvalidChecksumRegistrationCase {
  std::string name;
  upr::ChecksumAlgorithmSpec spec;
  std::string expected_message_substring;
};

class InvalidChecksumRegistrationTest : public ::testing::TestWithParam<InvalidChecksumRegistrationCase> {
 public:
  ~InvalidChecksumRegistrationTest() noexcept override = default;
};

TEST_P(InvalidChecksumRegistrationTest, RejectsInvalidRegistrations) {
  const InvalidChecksumRegistrationCase& param = GetParam();

  const upr::Status status = upr::register_checksum_algorithm(param.spec);

  ASSERT_FALSE(status.ok());
  EXPECT_EQ(status.code(), upr::StatusCode::kInvalidArgument);
  EXPECT_NE(std::string(status.message()).find(param.expected_message_substring), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(Coverage,
                         InvalidChecksumRegistrationTest,
                         ::testing::Values(
                             InvalidChecksumRegistrationCase{
                                 .name = "empty_name",
                                 .spec =
                                     {
                                         .name = "",
                                         .result_width_bytes = 1,
                                         .function = &custom_folded_sum,
                                     },
                                 .expected_message_substring = "must not be empty",
                             },
                             InvalidChecksumRegistrationCase{
                                 .name = "null_function",
                                 .spec =
                                     {
                                         .name = "null_function_20260404",
                                         .result_width_bytes = 1,
                                         .function = nullptr,
                                     },
                                 .expected_message_substring = "must not be null",
                             },
                             InvalidChecksumRegistrationCase{
                                 .name = "zero_width",
                                 .spec =
                                     {
                                         .name = "zero_width_20260404",
                                         .result_width_bytes = 0,
                                         .function = &custom_folded_sum,
                                     },
                                 .expected_message_substring = "between 1 and 8 bytes",
                             },
                             InvalidChecksumRegistrationCase{
                                 .name = "too_wide",
                                 .spec =
                                     {
                                         .name = "too_wide_20260404",
                                         .result_width_bytes = 9,
                                         .function = &custom_folded_sum,
                                     },
                                 .expected_message_substring = "between 1 and 8 bytes",
                             }),
                         [](const ::testing::TestParamInfo<InvalidChecksumRegistrationCase>& info) {
                           return info.param.name;
                         });

TEST(ChecksumRegistryTest, RejectsEmptyAndUnknownLookups) {
  const auto empty_name = upr::find_checksum_algorithm("");
  ASSERT_FALSE(empty_name.ok());
  EXPECT_EQ(empty_name.status().code(), upr::StatusCode::kInvalidArgument);

  const auto unknown_name = upr::find_checksum_algorithm("missing_algorithm_20260404");
  ASSERT_FALSE(unknown_name.ok());
  EXPECT_EQ(unknown_name.status().code(), upr::StatusCode::kNotFound);
  EXPECT_NE(std::string(unknown_name.status().message()).find("Unknown checksum algorithm"), std::string::npos);
}

}  // namespace
