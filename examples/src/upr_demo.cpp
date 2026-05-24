#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "universal_protocol_runtime/universal_protocol_runtime.hpp"

namespace {

namespace upr = universal_protocol_runtime;

constexpr size_t kOrderFrameSize = 15;

struct OrderSpec {
  std::string_view symbol;
  float price = 0.0F;
  uint32_t quantity = 0;
  uint8_t side = 0;
  uint8_t order_type = 0;
};

std::filesystem::path default_schema_path() {
  if (const char* runfiles_dir = std::getenv("RUNFILES_DIR")) {
    return std::filesystem::path(runfiles_dir) / "_main" / "examples" / "schema" / "market_data.upr";
  }
  if (const char* test_srcdir = std::getenv("TEST_SRCDIR")) {
    return std::filesystem::path(test_srcdir) / "_main" / "examples" / "schema" / "market_data.upr";
  }
  return std::filesystem::path("examples") / "schema" / "market_data.upr";
}

std::filesystem::path default_workbench_path() { return std::filesystem::path("upr_demo_workbench.html"); }

void append_uint32_le(std::vector<std::byte>* buffer, uint32_t value) {
  buffer->push_back(std::byte{static_cast<uint8_t>(value & 0xFFU)});
  buffer->push_back(std::byte{static_cast<uint8_t>((value >> 8U) & 0xFFU)});
  buffer->push_back(std::byte{static_cast<uint8_t>((value >> 16U) & 0xFFU)});
  buffer->push_back(std::byte{static_cast<uint8_t>((value >> 24U) & 0xFFU)});
}

void append_float32_le(std::vector<std::byte>* buffer, float value) {
  append_uint32_le(buffer, std::bit_cast<uint32_t>(value));
}

bool append_symbol(std::vector<std::byte>* buffer, std::string_view symbol) {
  if (symbol.size() != 4) {
    return false;
  }
  buffer->push_back(std::byte{static_cast<uint8_t>(symbol[0])});
  buffer->push_back(std::byte{static_cast<uint8_t>(symbol[1])});
  buffer->push_back(std::byte{static_cast<uint8_t>(symbol[2])});
  buffer->push_back(std::byte{static_cast<uint8_t>(symbol[3])});
  return true;
}

bool append_order_frame(std::vector<std::byte>* buffer, const OrderSpec& order) {
  buffer->push_back(std::byte{0x01});
  if (!append_symbol(buffer, order.symbol)) {
    return false;
  }
  append_float32_le(buffer, order.price);
  append_uint32_le(buffer, order.quantity);
  buffer->push_back(std::byte{order.side});
  buffer->push_back(std::byte{order.order_type});
  return true;
}

bool print_order(size_t index, const upr::DecodedMessage& message) {
  const std::optional<std::string_view> symbol = message.get_string_view("symbol");
  const std::optional<float> price = message.get<float>("price");
  const std::optional<uint32_t> quantity = message.get<uint32_t>("quantity");
  const std::optional<std::string_view> side = message.get_enum_name("side");
  const std::optional<std::string_view> order_type = message.get_enum_name("order_type");
  if (!symbol.has_value() || !price.has_value() || !quantity.has_value() || !side.has_value() ||
      !order_type.has_value()) {
    return false;
  }

  std::cout << "order[" << index << "] symbol=" << *symbol << " type=" << *order_type << " side=" << *side
            << " price=" << *price << " quantity=" << *quantity << '\n';
  return true;
}

std::vector<std::vector<std::byte>> split_fixed_frames(const std::vector<std::byte>& stream, size_t frame_size) {
  std::vector<std::vector<std::byte>> frames;
  if (frame_size == 0 || stream.size() % frame_size != 0) {
    return frames;
  }
  frames.reserve(stream.size() / frame_size);
  for (size_t offset = 0; offset < stream.size(); offset += frame_size) {
    frames.emplace_back(stream.begin() + static_cast<ptrdiff_t>(offset),
                        stream.begin() + static_cast<ptrdiff_t>(offset + frame_size));
  }
  return frames;
}

}  // namespace

int main(int argc, char** argv) {
  const std::filesystem::path schema_path = argc > 1 ? std::filesystem::path(argv[1]) : default_schema_path();
  const std::filesystem::path workbench_path = argc > 2 ? std::filesystem::path(argv[2]) : default_workbench_path();

  auto definition = upr::load_protocol_definition_from_file(schema_path.string());
  if (!definition.ok()) {
    std::cerr << definition.status().message() << '\n';
    return 1;
  }

  auto compiled = upr::compile_protocol(definition.value());
  if (!compiled.ok()) {
    std::cerr << compiled.status().message() << '\n';
    return 1;
  }

  const std::array<OrderSpec, 3> orders = {{
      {
          .symbol = "AAPL",
          .price = 42.25F,
          .quantity = 100,
          .side = 1,
          .order_type = 1,
      },
      {
          .symbol = "MSFT",
          .price = 42.50F,
          .quantity = 25,
          .side = 2,
          .order_type = 2,
      },
      {
          .symbol = "NVDA",
          .price = 43.00F,
          .quantity = 10,
          .side = 1,
          .order_type = 3,
      },
  }};

  std::vector<std::byte> stream;
  stream.reserve(orders.size() * kOrderFrameSize);
  for (const OrderSpec& order : orders) {
    if (!append_order_frame(&stream, order)) {
      std::cerr << "Invalid order symbol width in demo payload\n";
      return 1;
    }
  }

  upr::ProtocolDecoder decoder(compiled.value());
  upr::FixedSizeFramer framer(kOrderFrameSize);
  upr::SpanTransport transport(upr::ByteSpan(stream.data(), stream.size()), 9);
  upr::StreamRuntime<64> runtime(transport, framer, decoder);

  std::cout << "decoded_orders=" << orders.size() << '\n';

  size_t decoded = 0;
  while (true) {
    upr::DecodedMessage message;
    const upr::PollResult result = runtime.poll(&message);
    if (result.status == upr::PollStatus::kMessageReady) {
      if (!print_order(decoded, message)) {
        std::cerr << "Decoded message is missing expected fields\n";
        return 1;
      }
      ++decoded;
      continue;
    }
    if (result.status == upr::PollStatus::kNeedMoreData) {
      continue;
    }
    if (result.status == upr::PollStatus::kEndOfStream) {
      break;
    }

    std::cerr << "Stream failed with status=" << upr::to_string(result.status)
              << " decode_status=" << upr::to_string(result.decode_status)
              << " bytes_consumed=" << result.bytes_consumed << '\n';
    return 1;
  }

  if (decoded != orders.size()) {
    std::cerr << "Decoded " << decoded << " orders from a " << orders.size() << "-order demo stream\n";
    return 1;
  }

  const upr::RuntimeStats& stats = runtime.stats();
  std::cout << "frames_seen=" << stats.frames_seen << " frames_decoded=" << stats.frames_decoded
            << " transport_reads=" << stats.transport_reads << " bytes_read=" << stats.bytes_read << '\n';

  const std::vector<std::vector<std::byte>> sample_frames = split_fixed_frames(stream, kOrderFrameSize);
  upr::StatusOr<upr::DiscoveryReport> discovery =
      upr::discover_protocol_from_samples(sample_frames, {.protocol_name = "market_data_discovered"});
  if (!discovery.ok()) {
    std::cerr << discovery.status().message() << '\n';
    return 1;
  }

  upr::WorkbenchPageInput workbench;
  workbench.title = "UPR Demo Workbench";
  workbench.definition = &definition.value();
  workbench.compiled_protocol = &compiled.value();
  workbench.discovery_report = &discovery.value();
  workbench.sample_frames.reserve(sample_frames.size());
  for (size_t index = 0; index < sample_frames.size(); ++index) {
    workbench.sample_frames.push_back({
        .label = "frame_" + std::to_string(index),
        .bytes = sample_frames[index],
    });
  }

  const upr::Status workbench_status = upr::write_workbench_html_file(workbench_path.string(), workbench);
  if (!workbench_status.ok()) {
    std::cerr << workbench_status.message() << '\n';
    return 1;
  }
  std::cout << "workbench=" << std::filesystem::absolute(workbench_path) << '\n';

  return 0;
}
