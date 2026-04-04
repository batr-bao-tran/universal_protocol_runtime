#include "universal_protocol_runtime/framing/fixed_size_framer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace upr = universal_protocol_runtime;

namespace {

struct FixedSizeCase {
  std::string name;
  size_t frame_size = 0;
  size_t input_size = 0;
  upr::FrameStatus expected_status = upr::FrameStatus::kInvalidFrame;
  size_t expected_frame_size = 0;
};

class FixedSizeFramerTest : public ::testing::TestWithParam<FixedSizeCase> {
 public:
  ~FixedSizeFramerTest() noexcept override = default;
};

TEST_P(FixedSizeFramerTest, HandlesConfiguredFrameSizes) {
  const FixedSizeCase& param = GetParam();
  const std::array<std::byte, 6> bytes = {
      std::byte{0x01},
      std::byte{0x02},
      std::byte{0x03},
      std::byte{0x04},
      std::byte{0x05},
      std::byte{0x06},
  };
  upr::FixedSizeFramer framer(param.frame_size);
  upr::FrameSlice slice;

  const upr::FrameStatus status =
      framer.try_frame(upr::ByteSpan(bytes.data(), param.input_size),
                       param.expected_status == upr::FrameStatus::kReady ? &slice : nullptr);

  EXPECT_EQ(status, param.expected_status);
  if (status == upr::FrameStatus::kReady) {
    EXPECT_EQ(slice.offset, 0U);
    EXPECT_EQ(slice.size, param.expected_frame_size);
    EXPECT_EQ(slice.bytes_consumed, param.expected_frame_size);
  }
}

INSTANTIATE_TEST_SUITE_P(Coverage,
                         FixedSizeFramerTest,
                         ::testing::Values(FixedSizeCase{.name = "zero_size_is_invalid",
                                                         .frame_size = 0,
                                                         .input_size = 0,
                                                         .expected_status = upr::FrameStatus::kInvalidFrame},
                                           FixedSizeCase{.name = "needs_more_data",
                                                         .frame_size = 4,
                                                         .input_size = 2,
                                                         .expected_status = upr::FrameStatus::kNeedMoreData},
                                           FixedSizeCase{.name = "exact_frame",
                                                         .frame_size = 4,
                                                         .input_size = 4,
                                                         .expected_status = upr::FrameStatus::kReady,
                                                         .expected_frame_size = 4}),
                         [](const ::testing::TestParamInfo<FixedSizeCase>& info) { return info.param.name; });

TEST(FixedSizeFramerStandaloneTest, CanReturnReadyWithoutOutputSlice) {
  const std::array<std::byte, 4> bytes = {
      std::byte{0xAA},
      std::byte{0xBB},
      std::byte{0xCC},
      std::byte{0xDD},
  };
  upr::FixedSizeFramer framer(4);

  EXPECT_EQ(framer.try_frame(bytes, nullptr), upr::FrameStatus::kReady);
}

}  // namespace
