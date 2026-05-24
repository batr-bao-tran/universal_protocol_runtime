#ifndef UNIVERSAL_PROTOCOL_RUNTIME__EXAMPLES_INCLUDE__ADVANCED_MARKET_DATA_EXAMPLE_SUPPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__EXAMPLES_INCLUDE__ADVANCED_MARKET_DATA_EXAMPLE_SUPPORT_HPP_
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "universal_protocol_runtime/universal_protocol_runtime.hpp"

namespace advanced_market_data_example {

namespace upr = universal_protocol_runtime;

struct LoadedProtocol {
  upr::ProtocolDefinition definition;
  upr::CompiledProtocol compiled;
};

inline std::filesystem::path default_schema_path() {
  if (const char* runfiles_dir = std::getenv("RUNFILES_DIR")) {
    return std::filesystem::path(runfiles_dir) / "_main" / "examples" / "schema" / "advanced_market_data.upr";
  }
  if (const char* test_srcdir = std::getenv("TEST_SRCDIR")) {
    return std::filesystem::path(test_srcdir) / "_main" / "examples" / "schema" / "advanced_market_data.upr";
  }
  return std::filesystem::path("examples") / "schema" / "advanced_market_data.upr";
}

inline std::filesystem::path default_workbench_path() {
  return std::filesystem::path("advanced_market_data_workbench.html");
}

inline bool load_protocol(const std::filesystem::path& schema_path, LoadedProtocol* loaded) {
  const auto definition = upr::load_protocol_definition_from_file(schema_path.string());
  if (!definition.ok()) {
    std::cerr << definition.status().message() << '\n';
    return false;
  }
  const auto compiled_protocol = upr::compile_protocol(definition.value());
  if (!compiled_protocol.ok()) {
    std::cerr << compiled_protocol.status().message() << '\n';
    return false;
  }
  loaded->definition = definition.value();
  loaded->compiled = compiled_protocol.value();
  return true;
}

inline bool write_workbench(const std::filesystem::path& path,
                            std::string title,
                            const LoadedProtocol& loaded,
                            const std::vector<std::vector<std::byte>>& sample_frames) {
  upr::WorkbenchPageInput input;
  input.title = std::move(title);
  input.definition = &loaded.definition;
  input.compiled_protocol = &loaded.compiled;
  input.sample_frames.reserve(sample_frames.size());
  for (size_t index = 0; index < sample_frames.size(); ++index) {
    input.sample_frames.push_back({
        .label = "frame_" + std::to_string(index),
        .bytes = sample_frames[index],
    });
  }
  if (!sample_frames.empty()) {
    auto discovery =
        upr::discover_protocol_from_samples(sample_frames, {.protocol_name = loaded.definition.name + "_discovered"});
    if (discovery.ok()) {
      input.discovery_report = &discovery.value();
      const upr::Status status = upr::write_workbench_html_file(path.string(), input);
      return status.ok();
    }
  }
  return upr::write_workbench_html_file(path.string(), input).ok();
}

inline void append_u16_le(std::vector<std::byte>* bytes, uint16_t value) {
  bytes->push_back(std::byte{static_cast<uint8_t>(value & 0xFFU)});
  bytes->push_back(std::byte{static_cast<uint8_t>((value >> 8U) & 0xFFU)});
}

}  // namespace advanced_market_data_example

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__EXAMPLES__ADVANCED_MARKET_DATA_EXAMPLE_SUPPORT_HPP_
