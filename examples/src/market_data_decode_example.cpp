#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

#include "examples/include/advanced_market_data_example_support.hpp"

namespace {

namespace example = advanced_market_data_example;
namespace upr = universal_protocol_runtime;

bool decode_snapshot(const upr::ProtocolDecoder& decoder) {
  const std::vector<std::byte> frame = {
      std::byte{0x01},
      std::byte{0x02},
      std::byte{0x65},
      std::byte{0x00},
      std::byte{0x07},
      std::byte{0x00},
      std::byte{0x66},
      std::byte{0x00},
      std::byte{0x08},
      std::byte{0x00},
  };
  upr::DecodedMessage message;
  if (decoder.decode_as("Snapshot", upr::ByteSpan(frame.data(), frame.size()), &message) != upr::DecodeStatus::kOk) {
    return false;
  }
  const auto levels = message.get_collection("levels");
  if (!levels.has_value() || levels->count() != 2U) {
    return false;
  }
  const auto first = levels->at(0);
  const auto second = levels->at(1);
  if (!first.has_value() || !second.has_value()) {
    return false;
  }
  std::cout << "snapshot level_count=" << levels->count() << " first_price=" << first->get_unsigned("price").value_or(0)
            << " second_qty=" << second->get_unsigned("qty").value_or(0) << '\n';
  return true;
}

bool decode_event(const upr::ProtocolDecoder& decoder) {
  std::vector<std::byte> frame = {std::byte{0x02}, std::byte{0x01}};
  example::append_u16_le(&frame, 99U);
  example::append_u16_le(&frame, 103U);

  upr::DecodedMessage message;
  if (decoder.decode_as("Event", upr::ByteSpan(frame.data(), frame.size()), &message) != upr::DecodeStatus::kOk) {
    return false;
  }
  const auto detail = message.get_variant("detail");
  if (!detail.has_value()) {
    return false;
  }
  std::cout << "event kind=" << message.get_unsigned("kind").value_or(0)
            << " best_bid=" << detail->get_unsigned("best_bid").value_or(0) << '\n';
  return true;
}

bool decode_trade_event(const upr::ProtocolDecoder& decoder) {
  std::vector<std::byte> frame = {std::byte{0x02}, std::byte{0x02}};
  frame.insert(frame.end(),
               {std::byte{0x78},
                std::byte{0x56},
                std::byte{0x34},
                std::byte{0x12},
                std::byte{0x00},
                std::byte{0x00},
                std::byte{0x00},
                std::byte{0x00}});

  upr::DecodedMessage message;
  if (decoder.decode_as("Event", upr::ByteSpan(frame.data(), frame.size()), &message) != upr::DecodeStatus::kOk) {
    return false;
  }
  const auto detail = message.get_variant("detail");
  if (!detail.has_value()) {
    return false;
  }
  std::cout << "trade_event kind=" << message.get_unsigned("kind").value_or(0)
            << " trade_id=" << detail->get_unsigned("trade_id").value_or(0) << '\n';
  return true;
}

bool decode_quote(const upr::ProtocolDecoder& decoder) {
  const std::vector<std::byte> frame = {
      std::byte{0x03},
      std::byte{0x01},
      std::byte{0x02},
      std::byte{'O'},
      std::byte{'K'},
  };
  upr::DecodedMessage message;
  if (decoder.decode_as("Quote", upr::ByteSpan(frame.data(), frame.size()), &message) != upr::DecodeStatus::kOk) {
    return false;
  }
  const auto note = message.get_string_view("note");
  if (!note.has_value()) {
    return false;
  }
  std::cout << "quote note=" << *note << '\n';
  return true;
}

bool decode_quote_without_note(const upr::ProtocolDecoder& decoder) {
  const std::vector<std::byte> frame = {
      std::byte{0x03},
      std::byte{0x00},
  };
  upr::DecodedMessage message;
  if (decoder.decode_as("Quote", upr::ByteSpan(frame.data(), frame.size()), &message) != upr::DecodeStatus::kOk) {
    return false;
  }
  std::cout << "quote_without_note note_present=" << (message.get_string_view("note").has_value() ? "yes" : "no")
            << '\n';
  return true;
}

bool show_partial_decode(const upr::CompiledProtocol& protocol, const upr::ProtocolDecoder& decoder) {
  const std::vector<std::byte> frame = {
      std::byte{0x01},
      std::byte{0x02},
      std::byte{0x65},
      std::byte{0x00},
      std::byte{0x07},
      std::byte{0x00},
      std::byte{0x66},
      std::byte{0x00},
      std::byte{0x08},
      std::byte{0x00},
  };

  const upr::CompiledMessage* snapshot = protocol.find_message("Snapshot");
  if (snapshot == nullptr) {
    return false;
  }
  const auto level_count_id = snapshot->find_field("level_count");
  const auto levels_id = snapshot->find_field("levels");
  if (!level_count_id.has_value() || !levels_id.has_value()) {
    return false;
  }

  upr::DecodeFieldMask mask{};
  mask.selected_fields[*level_count_id] = true;
  mask.selected_fields[*levels_id] = true;

  upr::DecodedMessage message;
  if (decoder.decode_as("Snapshot", upr::ByteSpan(frame.data(), frame.size()), &message, mask) !=
      upr::DecodeStatus::kOk) {
    return false;
  }
  const auto levels = message.get_collection("levels");
  if (!levels.has_value()) {
    return false;
  }
  std::cout << "partial_decode level_count=" << message.get_unsigned("level_count").value_or(0)
            << " levels_selected=" << (levels.has_value() ? "yes" : "no")
            << " message_type_selected=" << (message.get_unsigned("message_type").has_value() ? "yes" : "no") << '\n';
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const std::filesystem::path schema_path = argc > 1 ? std::filesystem::path(argv[1]) : example::default_schema_path();
  const std::filesystem::path workbench_path =
      argc > 2 ? std::filesystem::path(argv[2]) : example::default_workbench_path();
  example::LoadedProtocol loaded;
  if (!example::load_protocol(schema_path, &loaded)) {
    return 1;
  }

  upr::ProtocolDecoder decoder(loaded.compiled);
  if (!decode_snapshot(decoder) || !decode_event(decoder) || !decode_trade_event(decoder) || !decode_quote(decoder) ||
      !decode_quote_without_note(decoder) || !show_partial_decode(loaded.compiled, decoder)) {
    std::cerr << "market data decode example failed\n";
    return 1;
  }
  const std::vector<std::vector<std::byte>> workbench_frames = {
      {std::byte{0x01},
       std::byte{0x02},
       std::byte{0x65},
       std::byte{0x00},
       std::byte{0x07},
       std::byte{0x00},
       std::byte{0x66},
       std::byte{0x00},
       std::byte{0x08},
       std::byte{0x00}},
      {std::byte{0x02}, std::byte{0x01}, std::byte{0x63}, std::byte{0x00}, std::byte{0x67}, std::byte{0x00}},
      {std::byte{0x03}, std::byte{0x01}, std::byte{0x02}, std::byte{'O'}, std::byte{'K'}},
  };
  if (!example::write_workbench(workbench_path, "Advanced Market Data Workbench", loaded, workbench_frames)) {
    std::cerr << "Failed to write market data workbench\n";
    return 1;
  }
  std::cout << "workbench=" << std::filesystem::absolute(workbench_path) << '\n';
  return 0;
}
