#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

#include "examples/include/hardware_telemetry_example_support.hpp"

namespace {

namespace example = hardware_telemetry_example;
namespace upr = universal_protocol_runtime;

}  // namespace

int main(int argc, char** argv) {
  const std::filesystem::path schema_path = argc > 1 ? std::filesystem::path(argv[1]) : example::default_schema_path();
  const std::filesystem::path workbench_path =
      argc > 2 ? std::filesystem::path(argv[2]) : example::default_workbench_path();
  example::LoadedProtocol loaded;
  if (!example::load_protocol(schema_path, &loaded)) {
    return 1;
  }

  const std::array<std::byte, 8> first_samples = {
      std::byte{0x10},
      std::byte{0x11},
      std::byte{0x12},
      std::byte{0x13},
      std::byte{0x20},
      std::byte{0x21},
      std::byte{0x22},
      std::byte{0x23},
  };
  const std::array<std::byte, 4> second_samples = {
      std::byte{0x30},
      std::byte{0x31},
      std::byte{0x32},
      std::byte{0x33},
  };

  const auto first_packet = example::encode_sensor_packet(
      loaded.compiled, 2U, 2U, upr::ByteSpan(first_samples.data(), first_samples.size()), false);
  const auto second_packet = example::encode_sensor_packet(
      loaded.compiled, 2U, 1U, upr::ByteSpan(second_samples.data(), second_samples.size()), false);
  if (!first_packet.has_value() || !second_packet.has_value()) {
    std::cerr << "hardware byte stream example failed to encode packets\n";
    return 1;
  }

  std::vector<std::byte> stream =
      example::make_length_prefixed_frame(upr::ByteSpan(first_packet->data(), first_packet->size()));
  const auto second_frame =
      example::make_length_prefixed_frame(upr::ByteSpan(second_packet->data(), second_packet->size()));
  stream.insert(stream.end(), second_frame.begin(), second_frame.end());

  upr::ProtocolDecoder decoder(loaded.compiled);
  upr::LengthPrefixedFramer framer({
      .prefix_width_bytes = 2,
      .byte_order = upr::ByteOrder::kLittleEndian,
      .max_payload_size = 64,
  });
  upr::SpanTransport transport(upr::ByteSpan(stream.data(), stream.size()), 5);
  upr::StreamRuntime<128> runtime(transport, framer, decoder);

  size_t decoded_packets = 0;
  while (true) {
    upr::DecodedMessage message;
    const upr::PollResult result = runtime.poll(&message);
    if (result.status == upr::PollStatus::kMessageReady) {
      const auto sample_count = message.get_unsigned("sample_count");
      const auto sample_bytes_len = message.get_unsigned("sample_bytes_len");
      if (message.message_name() != "SensorPacket" || !sample_count.has_value() || !sample_bytes_len.has_value()) {
        std::cerr << "Unexpected hardware stream message\n";
        return 1;
      }
      std::cout << "hardware_stream packet=" << decoded_packets << " sample_count=" << *sample_count
                << " sample_bytes_len=" << *sample_bytes_len << '\n';
      ++decoded_packets;
      continue;
    }
    if (result.status == upr::PollStatus::kNeedMoreData) {
      continue;
    }
    if (result.status == upr::PollStatus::kEndOfStream) {
      break;
    }
    std::cerr << "Hardware stream failed with status=" << upr::to_string(result.status)
              << " decode_status=" << upr::to_string(result.decode_status) << '\n';
    return 1;
  }

  const upr::RuntimeStats& stats = runtime.stats();
  std::cout << "hardware_stream_stats frames_decoded=" << stats.frames_decoded
            << " transport_reads=" << stats.transport_reads << " bytes_read=" << stats.bytes_read << '\n';
  const std::vector<std::vector<std::byte>> workbench_frames = {
      *first_packet,
      *second_packet,
      stream,
  };
  if (!example::write_workbench(workbench_path, "Hardware Telemetry Stream Workbench", loaded, workbench_frames)) {
    std::cerr << "Failed to write hardware stream workbench\n";
    return 1;
  }
  std::cout << "workbench=" << std::filesystem::absolute(workbench_path) << '\n';
  return decoded_packets == 2U ? 0 : 1;
}
