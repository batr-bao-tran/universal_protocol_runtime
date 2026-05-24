#ifndef UNIVERSAL_PROTOCOL_RUNTIME__EXAMPLES_INCLUDE__HARDWARE_TELEMETRY_EXAMPLE_SUPPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__EXAMPLES_INCLUDE__HARDWARE_TELEMETRY_EXAMPLE_SUPPORT_HPP_
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "universal_protocol_runtime/universal_protocol_runtime.hpp"

namespace hardware_telemetry_example {

namespace upr = universal_protocol_runtime;

struct LoadedProtocol {
  upr::ProtocolDefinition definition;
  upr::CompiledProtocol compiled;
};

inline std::filesystem::path default_schema_path() {
  if (const char* runfiles_dir = std::getenv("RUNFILES_DIR")) {
    return std::filesystem::path(runfiles_dir) / "_main" / "examples" / "schema" / "hardware_telemetry.upr";
  }
  if (const char* test_srcdir = std::getenv("TEST_SRCDIR")) {
    return std::filesystem::path(test_srcdir) / "_main" / "examples" / "schema" / "hardware_telemetry.upr";
  }
  return std::filesystem::path("examples") / "schema" / "hardware_telemetry.upr";
}

inline std::filesystem::path default_workbench_path() {
  return std::filesystem::path("hardware_telemetry_workbench.html");
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

inline std::optional<std::vector<std::byte>> encode_sensor_packet(const upr::CompiledProtocol& protocol,
                                                                  uint8_t version,
                                                                  uint8_t sample_count,
                                                                  upr::ByteSpan sample_bytes,
                                                                  bool segmented,
                                                                  bool* zero_copy_payload = nullptr) {
  const upr::CompiledMessage* packet = protocol.find_message("SensorPacket");
  if (packet == nullptr) {
    return std::nullopt;
  }

  const auto version_id = packet->find_field("version");
  const auto sample_bytes_len_id = packet->find_field("sample_bytes_len");
  const auto sample_count_id = packet->find_field("sample_count");
  const auto sample_bytes_id = packet->find_field("sample_bytes");
  if (!version_id.has_value() || !sample_bytes_len_id.has_value() || !sample_count_id.has_value() ||
      !sample_bytes_id.has_value()) {
    return std::nullopt;
  }

  upr::ProtocolEncoder encoder(protocol);
  std::vector<std::byte> encoded;
  if (segmented) {
    std::array<std::byte, 64> scratch{};
    auto builder = encoder.build_segmented("SensorPacket", upr::MutableByteSpan(scratch.data(), scratch.size()));
    if (!builder.has_value()) {
      return std::nullopt;
    }
    if (builder->set_unsigned(*version_id, version) != upr::EncodeStatus::kOk ||
        builder->set_unsigned(*sample_bytes_len_id, sample_bytes.size()) != upr::EncodeStatus::kOk ||
        builder->set_unsigned(*sample_count_id, sample_count) != upr::EncodeStatus::kOk ||
        builder->attach_bytes(*sample_bytes_id, sample_bytes) != upr::EncodeStatus::kOk) {
      return std::nullopt;
    }
    size_t bytes_written = 0;
    if (builder->finalize(&bytes_written) != upr::EncodeStatus::kOk) {
      return std::nullopt;
    }
    encoded.resize(bytes_written);
    if (builder->copy_to(upr::MutableByteSpan(encoded.data(), encoded.size()), &bytes_written) !=
        upr::EncodeStatus::kOk) {
      return std::nullopt;
    }
    if (zero_copy_payload != nullptr) {
      const auto segments = builder->segments();
      *zero_copy_payload = !segments.empty() && segments.back().bytes.data() == sample_bytes.data();
    }
    return encoded;
  }

  encoded.resize(128U);
  auto builder = encoder.build("SensorPacket", upr::MutableByteSpan(encoded.data(), encoded.size()));
  if (!builder.has_value()) {
    return std::nullopt;
  }
  if (builder->set_unsigned(*version_id, version) != upr::EncodeStatus::kOk ||
      builder->set_unsigned(*sample_bytes_len_id, sample_bytes.size()) != upr::EncodeStatus::kOk ||
      builder->set_unsigned(*sample_count_id, sample_count) != upr::EncodeStatus::kOk ||
      builder->set_bytes(*sample_bytes_id, sample_bytes) != upr::EncodeStatus::kOk) {
    return std::nullopt;
  }
  size_t bytes_written = 0;
  if (builder->finalize(&bytes_written) != upr::EncodeStatus::kOk) {
    return std::nullopt;
  }
  encoded.resize(bytes_written);
  if (zero_copy_payload != nullptr) {
    *zero_copy_payload = false;
  }
  return encoded;
}

inline std::vector<std::byte> make_length_prefixed_frame(upr::ByteSpan payload) {
  std::vector<std::byte> framed;
  framed.reserve(payload.size() + 2U);
  append_u16_le(&framed, static_cast<uint16_t>(payload.size()));
  framed.insert(framed.end(), payload.begin(), payload.end());
  return framed;
}

}  // namespace hardware_telemetry_example

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__EXAMPLES__HARDWARE_TELEMETRY_EXAMPLE_SUPPORT_HPP_
