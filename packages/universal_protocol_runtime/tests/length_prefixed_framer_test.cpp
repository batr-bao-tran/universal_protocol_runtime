#include "universal_protocol_runtime/framing/length_prefixed_framer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "detail/test_support.hpp"

namespace upr = universal_protocol_runtime;

namespace {

struct LengthPrefixedCase {
  std::string name;
  upr::LengthPrefixedFramerOptions options;
  std::vector<std::byte> bytes;
  upr::FrameStatus expected_status = upr::FrameStatus::kInvalidFrame;
  size_t expected_offset = 0;
  size_t expected_size = 0;
  size_t expected_consumed = 0;
};

class LengthPrefixedFramerTest : public ::testing::TestWithParam<LengthPrefixedCase> {
 public:
  ~LengthPrefixedFramerTest() noexcept override = default;
};

TEST_P(LengthPrefixedFramerTest, HandlesDifferentPrefixFormats) {
  const LengthPrefixedCase& param = GetParam();
  upr::LengthPrefixedFramer framer(param.options);
  upr::FrameSlice slice;

  const upr::FrameStatus status = framer.try_frame(upr::ByteSpan(param.bytes.data(), param.bytes.size()), &slice);

  EXPECT_EQ(status, param.expected_status);
  if (status == upr::FrameStatus::kReady) {
    EXPECT_EQ(slice.offset, param.expected_offset);
    EXPECT_EQ(slice.size, param.expected_size);
    EXPECT_EQ(slice.bytes_consumed, param.expected_consumed);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Coverage,
    LengthPrefixedFramerTest,
    ::testing::Values(
        LengthPrefixedCase{
            .name = "little_endian_payload_only",
            .options = {.prefix_width_bytes = 1,
                        .byte_order = upr::ByteOrder::kLittleEndian,
                        .include_prefix_in_payload = false,
                        .max_payload_size = 8},
            .bytes = upr_test_support::make_bytes({0x03, 0xAA, 0xBB, 0xCC, 0xDD}),
            .expected_status = upr::FrameStatus::kReady,
            .expected_offset = 1,
            .expected_size = 3,
            .expected_consumed = 4,
        },
        LengthPrefixedCase{
            .name = "include_prefix_in_payload",
            .options = {.prefix_width_bytes = 1, .include_prefix_in_payload = true, .max_payload_size = 8},
            .bytes = upr_test_support::make_bytes({0x02, 0x10, 0x20, 0x30}),
            .expected_status = upr::FrameStatus::kReady,
            .expected_offset = 0,
            .expected_size = 3,
            .expected_consumed = 3,
        },
        LengthPrefixedCase{
            .name = "big_endian_prefix",
            .options = {.prefix_width_bytes = 2, .byte_order = upr::ByteOrder::kBigEndian, .max_payload_size = 16},
            .bytes = upr_test_support::make_bytes({0x00, 0x03, 0x11, 0x22, 0x33}),
            .expected_status = upr::FrameStatus::kReady,
            .expected_offset = 2,
            .expected_size = 3,
            .expected_consumed = 5,
        },
        LengthPrefixedCase{
            .name = "invalid_byte_order",
            .options = {.prefix_width_bytes = 1,
                        .byte_order = static_cast<upr::ByteOrder>(99),
                        .include_prefix_in_payload = false,
                        .max_payload_size = 16},
            .bytes = upr_test_support::make_bytes({0x01, 0xAA}),
            .expected_status = upr::FrameStatus::kInvalidFrame,
        },
        LengthPrefixedCase{
            .name = "invalid_prefix_width",
            .options = {.prefix_width_bytes = 3, .max_payload_size = 16},
            .bytes = upr_test_support::make_bytes({0x03, 0xAA, 0xBB, 0xCC}),
            .expected_status = upr::FrameStatus::kInvalidFrame,
        },
        LengthPrefixedCase{
            .name = "short_prefix",
            .options = {.prefix_width_bytes = 2, .max_payload_size = 16},
            .bytes = upr_test_support::make_bytes({0x03}),
            .expected_status = upr::FrameStatus::kNeedMoreData,
        },
        LengthPrefixedCase{
            .name = "short_payload",
            .options = {.prefix_width_bytes = 1, .max_payload_size = 16},
            .bytes = upr_test_support::make_bytes({0x04, 0xAA, 0xBB}),
            .expected_status = upr::FrameStatus::kNeedMoreData,
        },
        LengthPrefixedCase{
            .name = "oversized_payload",
            .options = {.prefix_width_bytes = 1, .max_payload_size = 2},
            .bytes = upr_test_support::make_bytes({0x03, 0xAA, 0xBB, 0xCC}),
            .expected_status = upr::FrameStatus::kInvalidFrame,
        }),
    [](const ::testing::TestParamInfo<LengthPrefixedCase>& info) { return info.param.name; });

}  // namespace
