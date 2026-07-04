#include "universal_protocol_runtime/workbench/html_workbench.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "detail/test_support.hpp"
#include "universal_protocol_runtime/discovery/protocol_discovery.hpp"

namespace upr = universal_protocol_runtime;

namespace {

upr::ProtocolDefinition make_protocol_definition() {
  return upr_test_support::make_protocol(
      "console_demo",
      {
          upr_test_support::make_message(
              "Order",
              {
                  upr_test_support::make_scalar_field(
                      "message_type", upr::FieldKind::kUnsigned, 1, upr::ByteOrder::kLittleEndian, true, 1),
                  upr_test_support::make_string_field("symbol", 4),
                  upr_test_support::make_scalar_field("qty", upr::FieldKind::kUnsigned, 2),
              }),
      });
}

std::vector<std::vector<std::byte>> make_samples() {
  return {
      upr_test_support::make_bytes({0x01, 'A', 'A', 'P', 'L', 0x10, 0x00}),
      upr_test_support::make_bytes({0x01, 'M', 'S', 'F', 'T', 0x12, 0x00}),
  };
}

TEST(HtmlWorkbenchTest, RendersDefinitionCompiledProtocolDiscoveryAndSamples) {
  const upr::ProtocolDefinition definition = make_protocol_definition();
  upr::StatusOr<upr::CompiledProtocol> compiled = upr::compile_protocol(definition);
  ASSERT_TRUE(compiled.ok()) << compiled.status().message();

  const std::vector<std::vector<std::byte>> samples = make_samples();
  upr::StatusOr<upr::DiscoveryReport> report =
      upr::discover_protocol_from_samples(samples, {.protocol_name = "demo_discovery"});
  ASSERT_TRUE(report.ok()) << report.status().message();

  upr::WorkbenchPageInput input;
  input.title = "Demo Workbench";
  input.definition = &definition;
  input.compiled_protocol = &compiled.value();
  input.discovery_report = &report.value();
  input.sample_frames = {
      {.label = "frame_0", .bytes = samples[0]},
      {.label = "frame_1", .bytes = samples[1]},
  };

  upr::StatusOr<std::string> html = upr::render_workbench_html(input);

  ASSERT_TRUE(html.ok()) << html.status().message();
  EXPECT_NE(html.value().find("<title>Demo Workbench</title>"), std::string::npos);
  EXPECT_NE(html.value().find("Authoring Definition"), std::string::npos);
  EXPECT_NE(html.value().find("Compiled Protocol"), std::string::npos);
  EXPECT_NE(html.value().find("Discovery Report"), std::string::npos);
  EXPECT_NE(html.value().find("Sample Frames"), std::string::npos);
  EXPECT_NE(html.value().find("console_demo"), std::string::npos);
  EXPECT_NE(html.value().find("Order"), std::string::npos);
  EXPECT_NE(html.value().find("Message_01"), std::string::npos);
  EXPECT_NE(html.value().find("41 41 50 4C"), std::string::npos);
}

TEST(HtmlWorkbenchTest, RejectsEmptyWorkbenchModels) {
  upr::StatusOr<std::string> html = upr::render_workbench_html({});
  EXPECT_FALSE(html.ok());
}

TEST(HtmlWorkbenchTest, RendersEscapedMarkupAndCompiledOnlySummaries) {
  const upr::ProtocolDefinition definition = upr_test_support::make_protocol(
      "proto<&>\"'",
      {
          upr_test_support::make_message("Raw<&>\"'",
                                         {
                                             upr_test_support::make_struct_field("body<&>\"'", "Order<Body>"),
                                             upr_test_support::make_string_field("note", 4),
                                         }),
      },
      {
          upr_test_support::make_struct("Order<Body>",
                                        {
                                            upr_test_support::make_scalar_field("value", upr::FieldKind::kUnsigned, 1),
                                        }),
      });

  upr::StatusOr<upr::CompiledProtocol> compiled = upr::compile_protocol(definition);
  ASSERT_TRUE(compiled.ok()) << compiled.status().message();

  upr::WorkbenchPageInput input;
  input.title = "Title <&>\"'";
  input.definition = &definition;
  input.compiled_protocol = &compiled.value();
  input.sample_frames = {
      {.label = "Empty<&>\"'", .bytes = {}},
      {.label = "Binary", .bytes = upr_test_support::make_bytes({'<', '&', 0x01})},
  };

  upr::StatusOr<std::string> html = upr::render_workbench_html(input);

  ASSERT_TRUE(html.ok()) << html.status().message();
  EXPECT_NE(html.value().find("&lt;&amp;&gt;&quot;&#39;"), std::string::npos);
  EXPECT_NE(html.value().find("ref=Order&lt;Body&gt;"), std::string::npos);
  EXPECT_NE(html.value().find("dispatch_prefix="), std::string::npos);
  EXPECT_NE(html.value().find("3C 26 01"), std::string::npos);
  EXPECT_NE(html.value().find("Binary"), std::string::npos);
}

TEST(HtmlWorkbenchTest, WritesWorkbenchFilesAndReportsIoFailures) {
  upr::WorkbenchPageInput input;
  input.title = "Write Test";
  input.sample_frames = {
      {.label = "frame", .bytes = upr_test_support::make_bytes({0x01, 0x02})},
  };

  std::array<char, sizeof("/tmp/upr_workbench_XXXXXX")> output_template{};
  std::memcpy(output_template.data(), "/tmp/upr_workbench_XXXXXX", output_template.size());
  const int output_fd = ::mkstemp(output_template.data());
  ASSERT_GE(output_fd, 0);
  ASSERT_EQ(::close(output_fd), 0);

  ASSERT_TRUE(upr::write_workbench_html_file(output_template.data(), input).ok());

  std::ifstream written(output_template.data(), std::ios::binary);
  ASSERT_TRUE(written.good());
  std::string contents((std::istreambuf_iterator<char>(written)), std::istreambuf_iterator<char>());
  EXPECT_NE(contents.find("Write Test"), std::string::npos);
  ASSERT_EQ(::unlink(output_template.data()), 0);

  EXPECT_FALSE(upr::write_workbench_html_file("/tmp/upr_missing_dir/workbench.html", input).ok());

  if (::access("/dev/full", W_OK) == 0) {
    EXPECT_FALSE(upr::write_workbench_html_file("/dev/full", input).ok());
  }
}

}  // namespace
