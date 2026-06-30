#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>

#include "examples/cpp/include/hardware_telemetry_example_support.hpp"

namespace {

namespace example = hardware_telemetry_example;
namespace upr = universal_protocol_runtime;

}  // namespace

int main(int argc, char** argv) {
  const std::filesystem::path schema_path = argc > 1 ? std::filesystem::path(argv[1]) : example::default_schema_path();
  const std::filesystem::path workbench_path =
      argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("sensor_packet_encode_workbench.html");
  example::LoadedProtocol loaded;
  if (!example::load_protocol(schema_path, &loaded)) {
    return 1;
  }

  const std::array<std::byte, 8> sample_bytes = {
      std::byte{0x10},
      std::byte{0x11},
      std::byte{0x12},
      std::byte{0x13},
      std::byte{0x20},
      std::byte{0x21},
      std::byte{0x22},
      std::byte{0x23},
  };

  bool zero_copy_payload = false;
  const auto encoded = example::encode_sensor_packet(
      loaded.compiled, 2U, 2U, upr::ByteSpan(sample_bytes.data(), sample_bytes.size()), true, &zero_copy_payload);
  if (!encoded.has_value()) {
    std::cerr << "sensor packet encode example failed\n";
    return 1;
  }
  const auto encoded_contiguous = example::encode_sensor_packet(
      loaded.compiled, 2U, 2U, upr::ByteSpan(sample_bytes.data(), sample_bytes.size()), false);
  if (!encoded_contiguous.has_value()) {
    std::cerr << "sensor packet contiguous encode failed\n";
    return 1;
  }

  upr::ProtocolDecoder decoder(loaded.compiled);
  upr::DecodedMessage message;
  if (decoder.decode_as("SensorPacket", upr::ByteSpan(encoded->data(), encoded->size()), &message) !=
      upr::DecodeStatus::kOk) {
    std::cerr << "sensor packet roundtrip decode failed\n";
    return 1;
  }

  std::cout << "sensor_packet_encoded bytes=" << encoded->size()
            << " zero_copy_payload=" << (zero_copy_payload ? "yes" : "no") << " data=";
  for (const std::byte value : *encoded) {
    std::cout << ' ' << static_cast<unsigned>(std::to_integer<uint8_t>(value));
  }
  std::cout << '\n';
  std::cout << "sensor_packet_contiguous_match=" << (*encoded == *encoded_contiguous ? "yes" : "no")
            << " decoded_sample_count=" << message.get_unsigned("sample_count").value_or(0)
            << " decoded_sample_bytes_len=" << message.get_unsigned("sample_bytes_len").value_or(0) << '\n';
  if (!example::write_workbench(
          workbench_path, "Hardware Telemetry Encode Workbench", loaded, {*encoded, *encoded_contiguous})) {
    std::cerr << "Failed to write sensor encode workbench\n";
    return 1;
  }
  std::cout << "workbench=" << std::filesystem::absolute(workbench_path) << '\n';
  return 0;
}
