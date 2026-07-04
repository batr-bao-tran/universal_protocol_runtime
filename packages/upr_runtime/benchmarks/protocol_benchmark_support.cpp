#include "protocol_benchmark_support.hpp"

#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/verifier.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "packages/upr_runtime/benchmark_blob_envelope_generated.h"
#include "packages/upr_runtime/benchmark_market_data_generated.h"
#include "packages/upr_runtime/benchmarks/benchmark_messages.pb.h"
#include "universal_protocol_runtime/compiler/schema_compiler.hpp"
#include "universal_protocol_runtime/core/byte_view.hpp"
#include "universal_protocol_runtime/core/unreachable.hpp"
#include "universal_protocol_runtime/decoder/decoded_message.hpp"
#include "universal_protocol_runtime/decoder/protocol_decoder.hpp"
#include "universal_protocol_runtime/pdl/protocol_definition.hpp"

namespace flat = upr_benchmarks;
namespace proto = upr_benchmarks_proto;

namespace universal_protocol_runtime::benchmarks {
namespace {

constexpr uint8_t kBlobMessageType = 1U;
constexpr uint8_t kMarketDataMessageType = 2U;
constexpr uint8_t kBlobLengthWidthBytes = sizeof(uint16_t);
constexpr uint8_t kMarketChecksumWidthBytes = sizeof(uint16_t);
constexpr uint8_t kOuterPrefixWidthBytes = sizeof(uint16_t);
constexpr size_t kBlobSmallPayloadBytes = 32U;
constexpr size_t kBlobLargePayloadBytes = 2048U;
constexpr size_t kMarketSymbolBytes = 8U;
constexpr size_t kBlobSmallMessagesPerSeed = 4096U;
constexpr size_t kBlobLargeMessagesPerSeed = 256U;
constexpr size_t kMarketMessagesPerSeed = 4096U;
constexpr std::array<uint64_t, 16> kDatasetSeeds = {
    11U,
    23U,
    47U,
    89U,
    131U,
    197U,
    263U,
    353U,
    431U,
    509U,
    601U,
    683U,
    761U,
    823U,
    887U,
    953U,
};
constexpr std::array<ProtocolKind, 4> kProtocols = {
    ProtocolKind::KUpr,
    ProtocolKind::KPackedBinary,
    ProtocolKind::KProtobuf,
    ProtocolKind::KFlatbuffers,
};
constexpr std::array<ScenarioKind, 3> kScenarios = {
    ScenarioKind::KBlobSmall,
    ScenarioKind::KBlobLarge,
    ScenarioKind::KMarketData,
};
constexpr std::array<BenchmarkCase, kProtocols.size() * kScenarios.size()> kBenchmarkCases = {
    BenchmarkCase{.protocol = ProtocolKind::KUpr, .scenario = ScenarioKind::KBlobSmall},
    BenchmarkCase{.protocol = ProtocolKind::KPackedBinary, .scenario = ScenarioKind::KBlobSmall},
    BenchmarkCase{.protocol = ProtocolKind::KProtobuf, .scenario = ScenarioKind::KBlobSmall},
    BenchmarkCase{.protocol = ProtocolKind::KFlatbuffers, .scenario = ScenarioKind::KBlobSmall},
    BenchmarkCase{.protocol = ProtocolKind::KUpr, .scenario = ScenarioKind::KBlobLarge},
    BenchmarkCase{.protocol = ProtocolKind::KPackedBinary, .scenario = ScenarioKind::KBlobLarge},
    BenchmarkCase{.protocol = ProtocolKind::KProtobuf, .scenario = ScenarioKind::KBlobLarge},
    BenchmarkCase{.protocol = ProtocolKind::KFlatbuffers, .scenario = ScenarioKind::KBlobLarge},
    BenchmarkCase{.protocol = ProtocolKind::KUpr, .scenario = ScenarioKind::KMarketData},
    BenchmarkCase{.protocol = ProtocolKind::KPackedBinary, .scenario = ScenarioKind::KMarketData},
    BenchmarkCase{.protocol = ProtocolKind::KProtobuf, .scenario = ScenarioKind::KMarketData},
    BenchmarkCase{.protocol = ProtocolKind::KFlatbuffers, .scenario = ScenarioKind::KMarketData},
};

struct BlobEnvelopeData {
  std::vector<std::byte> payload;
  uint8_t checksum = 0;
};

struct MarketDataData {
  uint32_t instrument_id = 0;
  uint32_t sequence = 0;
  uint64_t exchange_time_ns = 0;
  uint64_t receive_time_ns = 0;
  double last_price = 0.0;
  uint32_t last_qty = 0;
  uint32_t bid_qty = 0;
  uint32_t ask_qty = 0;
  uint32_t bid_price_micros = 0;
  uint32_t ask_price_micros = 0;
  std::array<char, kMarketSymbolBytes> symbol{};
  uint8_t flags = 0;
  uint16_t checksum = 0;
};

struct CorpusBundle {
  std::vector<std::byte> stream_bytes;
  size_t message_count = 0;
  size_t total_frame_bytes = 0;
};

class SplitMix64 {
 public:
  explicit SplitMix64(uint64_t seed) : state_(seed) {}

  uint64_t next() {
    uint64_t value = (state_ += 0x9E3779B97F4A7C15ULL);
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
  }

  uint32_t next_u32() { return static_cast<uint32_t>(next() & 0xFFFFFFFFU); }

  uint32_t next_u32(uint32_t bound) { return bound == 0U ? 0U : static_cast<uint32_t>(next() % bound); }

  char next_symbol_char() { return static_cast<char>('A' + next_u32(26U)); }

 private:
  uint64_t state_ = 0;
};

template <typename Integer>
void append_little_endian(std::vector<std::byte>* out, Integer value) {
  if constexpr (std::is_floating_point_v<Integer>) {
    using Raw = std::conditional_t<sizeof(Integer) == sizeof(uint32_t), uint32_t, uint64_t>;
    const Raw normalized = std::bit_cast<Raw>(value);
    for (size_t index = 0; index < sizeof(Integer); ++index) {
      out->push_back(static_cast<std::byte>((normalized >> (index * 8U)) & 0xFFU));
    }
  } else {
    using Unsigned = std::make_unsigned_t<Integer>;
    const auto normalized = static_cast<Unsigned>(value);
    for (size_t index = 0; index < sizeof(Integer); ++index) {
      out->push_back(static_cast<std::byte>((normalized >> (index * 8U)) & 0xFFU));
    }
  }
}

uint16_t read_outer_length(ByteSpan bytes) {
  return static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[0])) |
         static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[1])) << 8U;
}

void append_outer_prefix(std::vector<std::byte>* out, size_t frame_bytes) {
  append_little_endian<uint16_t>(out, static_cast<uint16_t>(frame_bytes));
}

void append_length_prefixed_frame(std::vector<std::byte>* stream, const std::vector<std::byte>& frame) {
  append_outer_prefix(stream, frame.size());
  stream->insert(stream->end(), frame.begin(), frame.end());
}

uint8_t xor8(ByteSpan bytes) {
  uint8_t value = 0;
  for (const std::byte byte : bytes) {
    value ^= std::to_integer<uint8_t>(byte);
  }
  return value;
}

uint8_t blob_checksum(ByteSpan payload) {
  std::vector<std::byte> checksum_bytes;
  checksum_bytes.reserve(1U + kBlobLengthWidthBytes + payload.size());
  checksum_bytes.push_back(static_cast<std::byte>(kBlobMessageType));
  append_little_endian<uint16_t>(&checksum_bytes, static_cast<uint16_t>(payload.size()));
  checksum_bytes.insert(checksum_bytes.end(), payload.begin(), payload.end());
  return xor8(ByteSpan(checksum_bytes.data(), checksum_bytes.size()));
}

std::vector<std::byte> market_prefix_bytes(const MarketDataData& data) {
  std::vector<std::byte> bytes;
  bytes.reserve(1U + (sizeof(uint32_t) * 7U) + (sizeof(uint64_t) * 2U) + sizeof(double) + kMarketSymbolBytes);
  bytes.push_back(static_cast<std::byte>(kMarketDataMessageType));
  append_little_endian<uint32_t>(&bytes, data.instrument_id);
  append_little_endian<uint32_t>(&bytes, data.sequence);
  append_little_endian<uint64_t>(&bytes, data.exchange_time_ns);
  append_little_endian<uint64_t>(&bytes, data.receive_time_ns);
  append_little_endian<double>(&bytes, data.last_price);
  append_little_endian<uint32_t>(&bytes, data.last_qty);
  append_little_endian<uint32_t>(&bytes, data.bid_qty);
  append_little_endian<uint32_t>(&bytes, data.ask_qty);
  append_little_endian<uint32_t>(&bytes, data.bid_price_micros);
  append_little_endian<uint32_t>(&bytes, data.ask_price_micros);
  for (const char character : data.symbol) {
    bytes.push_back(static_cast<std::byte>(static_cast<uint8_t>(character)));
  }
  bytes.push_back(static_cast<std::byte>(data.flags));
  return bytes;
}

uint16_t sum16(ByteSpan bytes) {
  uint32_t sum = 0;
  for (const std::byte byte : bytes) {
    sum += std::to_integer<uint8_t>(byte);
  }
  return static_cast<uint16_t>(sum & 0xFFFFU);
}

uint16_t market_checksum(const MarketDataData& data) {
  const std::vector<std::byte> prefix = market_prefix_bytes(data);
  return sum16(ByteSpan(prefix.data(), prefix.size()));
}

std::string symbol_string(const std::array<char, kMarketSymbolBytes>& symbol) {
  return std::string(symbol.begin(), symbol.end());
}

std::string_view scenario_label(ScenarioKind scenario) {
  switch (scenario) {
    case ScenarioKind::KBlobSmall:
      return "blob_small";
    case ScenarioKind::KBlobLarge:
      return "blob_large";
    case ScenarioKind::KMarketData:
      return "market_data";
  }
  unreachable();
}

BlobEnvelopeData make_blob_message(uint64_t seed, size_t ordinal, size_t payload_bytes) {
  SplitMix64 rng(seed ^ (static_cast<uint64_t>(ordinal) * 0x9E3779B97F4A7C15ULL));
  BlobEnvelopeData message;
  message.payload.resize(payload_bytes);
  for (std::byte& byte : message.payload) {
    byte = static_cast<std::byte>(rng.next_u32(256U));
  }
  message.checksum = blob_checksum(ByteSpan(message.payload.data(), message.payload.size()));
  return message;
}

MarketDataData make_market_data_message(uint64_t seed, size_t ordinal) {
  SplitMix64 rng(seed ^ (static_cast<uint64_t>(ordinal) * 0xD6E8FEB86659FD93ULL));
  MarketDataData message;
  message.instrument_id = 100000U + rng.next_u32(500000U);
  message.sequence = static_cast<uint32_t>(ordinal + 1U);
  message.exchange_time_ns = 1700000000000000000ULL + rng.next();
  message.receive_time_ns = message.exchange_time_ns + static_cast<uint64_t>(rng.next_u32(5000U));
  message.last_price = static_cast<double>(950000U + rng.next_u32(200000U)) / 10000.0;
  message.last_qty = 1U + rng.next_u32(10000U);
  message.bid_qty = 1U + rng.next_u32(10000U);
  message.ask_qty = 1U + rng.next_u32(10000U);
  message.bid_price_micros = static_cast<uint32_t>(message.last_price * 1000000.0) - rng.next_u32(1500U);
  message.ask_price_micros = static_cast<uint32_t>(message.last_price * 1000000.0) + rng.next_u32(1500U);
  for (char& character : message.symbol) {
    character = rng.next_symbol_char();
  }
  message.flags = static_cast<uint8_t>(rng.next_u32(256U));
  message.checksum = market_checksum(message);
  return message;
}

std::vector<std::byte> encode_binary_blob_frame(const BlobEnvelopeData& message) {
  std::vector<std::byte> frame;
  frame.reserve(1U + kBlobLengthWidthBytes + message.payload.size() + 1U);
  frame.push_back(static_cast<std::byte>(kBlobMessageType));
  append_little_endian<uint16_t>(&frame, static_cast<uint16_t>(message.payload.size()));
  frame.insert(frame.end(), message.payload.begin(), message.payload.end());
  frame.push_back(static_cast<std::byte>(message.checksum));
  return frame;
}

std::vector<std::byte> encode_binary_market_data_frame(const MarketDataData& message) {
  std::vector<std::byte> frame = market_prefix_bytes(message);
  append_little_endian<uint16_t>(&frame, message.checksum);
  return frame;
}

std::vector<std::byte> encode_protobuf_blob_frame(const BlobEnvelopeData& message) {
  proto::BlobEnvelope encoded;
  encoded.set_message_type(kBlobMessageType);
  encoded.set_payload(reinterpret_cast<const char*>(message.payload.data()), static_cast<int>(message.payload.size()));
  encoded.set_checksum(message.checksum);

  std::vector<std::byte> frame(encoded.ByteSizeLong());
  if (!encoded.SerializeToArray(frame.data(), static_cast<int>(frame.size()))) {
    std::cerr << "Failed to serialize protobuf BlobEnvelope.\n";
    std::abort();
  }
  return frame;
}

std::vector<std::byte> encode_protobuf_market_data_frame(const MarketDataData& message) {
  proto::MarketData encoded;
  encoded.set_message_type(kMarketDataMessageType);
  encoded.set_instrument_id(message.instrument_id);
  encoded.set_sequence(message.sequence);
  encoded.set_exchange_time_ns(message.exchange_time_ns);
  encoded.set_receive_time_ns(message.receive_time_ns);
  encoded.set_last_price(message.last_price);
  encoded.set_last_qty(message.last_qty);
  encoded.set_bid_qty(message.bid_qty);
  encoded.set_ask_qty(message.ask_qty);
  encoded.set_bid_price_micros(message.bid_price_micros);
  encoded.set_ask_price_micros(message.ask_price_micros);
  encoded.set_symbol(symbol_string(message.symbol));
  encoded.set_flags(message.flags);
  encoded.set_checksum(message.checksum);

  std::vector<std::byte> frame(encoded.ByteSizeLong());
  if (!encoded.SerializeToArray(frame.data(), static_cast<int>(frame.size()))) {
    std::cerr << "Failed to serialize protobuf MarketData.\n";
    std::abort();
  }
  return frame;
}

std::vector<std::byte> encode_flatbuffers_blob_frame(const BlobEnvelopeData& message) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<uint8_t> payload_bytes(message.payload.size());
  for (size_t index = 0; index < message.payload.size(); ++index) {
    payload_bytes[index] = std::to_integer<uint8_t>(message.payload[index]);
  }

  const auto payload = builder.CreateVector(payload_bytes);
  const auto root = flat::CreateBlobEnvelope(builder, kBlobMessageType, payload, message.checksum);
  builder.Finish(root);

  std::vector<std::byte> frame(builder.GetSize());
  std::memcpy(frame.data(), builder.GetBufferPointer(), builder.GetSize());
  return frame;
}

std::vector<std::byte> encode_flatbuffers_market_data_frame(const MarketDataData& message) {
  flatbuffers::FlatBufferBuilder builder;
  const auto symbol = builder.CreateString(symbol_string(message.symbol));
  const auto root = flat::CreateMarketData(builder,
                                           kMarketDataMessageType,
                                           message.instrument_id,
                                           message.sequence,
                                           message.exchange_time_ns,
                                           message.receive_time_ns,
                                           message.last_price,
                                           message.last_qty,
                                           message.bid_qty,
                                           message.ask_qty,
                                           message.bid_price_micros,
                                           message.ask_price_micros,
                                           symbol,
                                           message.flags,
                                           message.checksum);
  builder.Finish(root);

  std::vector<std::byte> frame(builder.GetSize());
  std::memcpy(frame.data(), builder.GetBufferPointer(), builder.GetSize());
  return frame;
}

uint64_t fold_bytes(ByteSpan bytes, uint64_t seed) {
  uint64_t folded = seed;
  for (const std::byte byte : bytes) {
    folded = (folded * 131U) ^ static_cast<uint64_t>(std::to_integer<uint8_t>(byte));
  }
  return folded;
}

uint64_t fold_market_data(const MarketDataData& message) {
  uint64_t folded = 0;
  folded ^= static_cast<uint64_t>(message.instrument_id);
  folded = (folded * 131U) ^ static_cast<uint64_t>(message.sequence);
  folded = (folded * 131U) ^ message.exchange_time_ns;
  folded = (folded * 131U) ^ message.receive_time_ns;
  folded = (folded * 131U) ^ std::bit_cast<uint64_t>(message.last_price);
  folded = (folded * 131U) ^ static_cast<uint64_t>(message.last_qty);
  folded = (folded * 131U) ^ static_cast<uint64_t>(message.bid_qty);
  folded = (folded * 131U) ^ static_cast<uint64_t>(message.ask_qty);
  folded = (folded * 131U) ^ static_cast<uint64_t>(message.bid_price_micros);
  folded = (folded * 131U) ^ static_cast<uint64_t>(message.ask_price_micros);
  folded = fold_bytes(as_bytes(std::span<const char, kMarketSymbolBytes>(message.symbol.data(), message.symbol.size())),
                      folded);
  folded = (folded * 131U) ^ static_cast<uint64_t>(message.flags);
  folded = (folded * 131U) ^ static_cast<uint64_t>(message.checksum);
  return folded;
}

uint64_t decode_binary_blob_frame(ByteSpan frame) {
  if (frame.size() < 1U + kBlobLengthWidthBytes + 1U) {
    std::cerr << "Invalid packed-binary blob frame.\n";
    std::abort();
  }
  const size_t payload_size = static_cast<size_t>(std::to_integer<uint8_t>(frame[1])) |
                              (static_cast<size_t>(std::to_integer<uint8_t>(frame[2])) << 8U);
  if (frame.size() != 1U + kBlobLengthWidthBytes + payload_size + 1U) {
    std::cerr << "Packed-binary blob frame length mismatch.\n";
    std::abort();
  }

  const ByteSpan payload(frame.data() + 1U + kBlobLengthWidthBytes, payload_size);
  const auto actual_checksum = std::to_integer<uint8_t>(frame.back());
  if (actual_checksum != blob_checksum(payload)) {
    std::cerr << "Packed-binary blob checksum mismatch.\n";
    std::abort();
  }
  return fold_bytes(payload, actual_checksum);
}

uint64_t decode_binary_market_data_frame(ByteSpan frame) {
  constexpr size_t kFrameBytes = 1U + (sizeof(uint32_t) * 7U) + (sizeof(uint64_t) * 2U) + sizeof(double) +
                                 kMarketSymbolBytes + 1U + kMarketChecksumWidthBytes;
  if (frame.size() != kFrameBytes) {
    std::cerr << "Invalid packed-binary market-data frame length.\n";
    std::abort();
  }

  MarketDataData decoded;
  size_t offset = 1U;
  auto read_u32 = [&](uint32_t* out) {
    *out = static_cast<uint32_t>(std::to_integer<uint8_t>(frame[offset])) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(frame[offset + 1U])) << 8U) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(frame[offset + 2U])) << 16U) |
           (static_cast<uint32_t>(std::to_integer<uint8_t>(frame[offset + 3U])) << 24U);
    offset += sizeof(uint32_t);
  };
  auto read_u64 = [&](uint64_t* out) {
    *out = static_cast<uint64_t>(std::to_integer<uint8_t>(frame[offset])) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(frame[offset + 1U])) << 8U) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(frame[offset + 2U])) << 16U) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(frame[offset + 3U])) << 24U) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(frame[offset + 4U])) << 32U) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(frame[offset + 5U])) << 40U) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(frame[offset + 6U])) << 48U) |
           (static_cast<uint64_t>(std::to_integer<uint8_t>(frame[offset + 7U])) << 56U);
    offset += sizeof(uint64_t);
  };

  read_u32(&decoded.instrument_id);
  read_u32(&decoded.sequence);
  read_u64(&decoded.exchange_time_ns);
  read_u64(&decoded.receive_time_ns);
  uint64_t raw_price = 0;
  read_u64(&raw_price);
  decoded.last_price = std::bit_cast<double>(raw_price);
  read_u32(&decoded.last_qty);
  read_u32(&decoded.bid_qty);
  read_u32(&decoded.ask_qty);
  read_u32(&decoded.bid_price_micros);
  read_u32(&decoded.ask_price_micros);
  for (char& character : decoded.symbol) {
    character = static_cast<char>(std::to_integer<uint8_t>(frame[offset++]));
  }
  decoded.flags = std::to_integer<uint8_t>(frame[offset++]);
  decoded.checksum = static_cast<uint16_t>(std::to_integer<uint8_t>(frame[offset])) |
                     (static_cast<uint16_t>(std::to_integer<uint8_t>(frame[offset + 1U])) << 8U);

  if (decoded.checksum != market_checksum(decoded)) {
    std::cerr << "Packed-binary market-data checksum mismatch.\n";
    std::abort();
  }
  return fold_market_data(decoded);
}

uint64_t decode_protobuf_blob_frame(ByteSpan frame) {
  proto::BlobEnvelope decoded;
  if (!decoded.ParseFromArray(frame.data(), static_cast<int>(frame.size()))) {
    std::cerr << "Failed to parse protobuf BlobEnvelope.\n";
    std::abort();
  }

  const std::string& payload = decoded.payload();
  const ByteSpan payload_bytes(reinterpret_cast<const std::byte*>(payload.data()), payload.size());
  if (decoded.message_type() != kBlobMessageType || decoded.checksum() != blob_checksum(payload_bytes)) {
    std::cerr << "Protobuf blob validation failed.\n";
    std::abort();
  }
  return fold_bytes(payload_bytes, decoded.checksum());
}

uint64_t decode_protobuf_market_data_frame(ByteSpan frame) {
  proto::MarketData decoded;
  if (!decoded.ParseFromArray(frame.data(), static_cast<int>(frame.size()))) {
    std::cerr << "Failed to parse protobuf MarketData.\n";
    std::abort();
  }

  MarketDataData message;
  message.instrument_id = decoded.instrument_id();
  message.sequence = decoded.sequence();
  message.exchange_time_ns = decoded.exchange_time_ns();
  message.receive_time_ns = decoded.receive_time_ns();
  message.last_price = decoded.last_price();
  message.last_qty = decoded.last_qty();
  message.bid_qty = decoded.bid_qty();
  message.ask_qty = decoded.ask_qty();
  message.bid_price_micros = decoded.bid_price_micros();
  message.ask_price_micros = decoded.ask_price_micros();
  if (decoded.symbol().size() != kMarketSymbolBytes) {
    std::cerr << "Protobuf market-data symbol width mismatch.\n";
    std::abort();
  }
  std::memcpy(message.symbol.data(), decoded.symbol().data(), kMarketSymbolBytes);
  message.flags = static_cast<uint8_t>(decoded.flags());
  message.checksum = static_cast<uint16_t>(decoded.checksum());

  if (decoded.message_type() != kMarketDataMessageType || message.checksum != market_checksum(message)) {
    std::cerr << "Protobuf market-data validation failed.\n";
    std::abort();
  }
  return fold_market_data(message);
}

uint64_t decode_flatbuffers_blob_frame(ByteSpan frame) {
  flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
  if (!flat::VerifyBlobEnvelopeBuffer(verifier)) {
    std::cerr << "FlatBuffers blob verification failed.\n";
    std::abort();
  }
  const flat::BlobEnvelope* decoded = flat::GetBlobEnvelope(reinterpret_cast<const uint8_t*>(frame.data()));
  if (decoded->message_type() != kBlobMessageType || decoded->payload() == nullptr) {
    std::cerr << "FlatBuffers blob contents invalid.\n";
    std::abort();
  }

  const auto* payload = decoded->payload();
  const ByteSpan payload_bytes(reinterpret_cast<const std::byte*>(payload->Data()), payload->size());
  if (decoded->checksum() != blob_checksum(payload_bytes)) {
    std::cerr << "FlatBuffers blob checksum mismatch.\n";
    std::abort();
  }
  return fold_bytes(payload_bytes, decoded->checksum());
}

uint64_t decode_flatbuffers_market_data_frame(ByteSpan frame) {
  flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
  if (!flat::VerifyMarketDataBuffer(verifier)) {
    std::cerr << "FlatBuffers market-data verification failed.\n";
    std::abort();
  }
  const flat::MarketData* decoded = flat::GetMarketData(reinterpret_cast<const uint8_t*>(frame.data()));
  if (decoded->message_type() != kMarketDataMessageType || decoded->symbol() == nullptr) {
    std::cerr << "FlatBuffers market-data contents invalid.\n";
    std::abort();
  }
  if (decoded->symbol()->size() != kMarketSymbolBytes) {
    std::cerr << "FlatBuffers market-data symbol width mismatch.\n";
    std::abort();
  }

  MarketDataData message;
  message.instrument_id = decoded->instrument_id();
  message.sequence = decoded->sequence();
  message.exchange_time_ns = decoded->exchange_time_ns();
  message.receive_time_ns = decoded->receive_time_ns();
  message.last_price = decoded->last_price();
  message.last_qty = decoded->last_qty();
  message.bid_qty = decoded->bid_qty();
  message.ask_qty = decoded->ask_qty();
  message.bid_price_micros = decoded->bid_price_micros();
  message.ask_price_micros = decoded->ask_price_micros();
  std::memcpy(message.symbol.data(), decoded->symbol()->c_str(), kMarketSymbolBytes);
  message.flags = decoded->flags();
  message.checksum = decoded->checksum();

  if (message.checksum != market_checksum(message)) {
    std::cerr << "FlatBuffers market-data checksum mismatch.\n";
    std::abort();
  }
  return fold_market_data(message);
}

uint64_t decode_upr_blob_frame(const ProtocolDecoder& decoder, ByteSpan frame) {
  DecodedMessage decoded;
  if (decoder.decode_any(frame, &decoded) != DecodeStatus::kOk) {
    std::cerr << "UPR blob decode failed.\n";
    std::abort();
  }

  const auto payload = decoded.get_bytes("payload");
  if (!payload.has_value()) {
    std::cerr << "UPR blob payload missing.\n";
    std::abort();
  }
  return fold_bytes(*payload, decoded.get<uint8_t>("checksum").value_or(0U));
}

uint64_t decode_upr_market_data_frame(const ProtocolDecoder& decoder, ByteSpan frame) {
  DecodedMessage decoded;
  if (decoder.decode_any(frame, &decoded) != DecodeStatus::kOk) {
    std::cerr << "UPR market-data decode failed.\n";
    std::abort();
  }

  MarketDataData message;
  message.instrument_id = decoded.get<uint32_t>("instrument_id").value_or(0U);
  message.sequence = decoded.get<uint32_t>("sequence").value_or(0U);
  message.exchange_time_ns = decoded.get<uint64_t>("exchange_time_ns").value_or(0U);
  message.receive_time_ns = decoded.get<uint64_t>("receive_time_ns").value_or(0U);
  message.last_price = decoded.get<double>("last_price").value_or(0.0);
  message.last_qty = decoded.get<uint32_t>("last_qty").value_or(0U);
  message.bid_qty = decoded.get<uint32_t>("bid_qty").value_or(0U);
  message.ask_qty = decoded.get<uint32_t>("ask_qty").value_or(0U);
  message.bid_price_micros = decoded.get<uint32_t>("bid_price_micros").value_or(0U);
  message.ask_price_micros = decoded.get<uint32_t>("ask_price_micros").value_or(0U);
  const auto symbol = decoded.get_fixed_string<kMarketSymbolBytes>("symbol");
  if (!symbol.has_value()) {
    std::cerr << "UPR market-data symbol missing.\n";
    std::abort();
  }
  std::memcpy(message.symbol.data(), symbol->data(), kMarketSymbolBytes);
  message.flags = decoded.get<uint8_t>("flags").value_or(0U);
  message.checksum = decoded.get<uint16_t>("checksum").value_or(0U);
  return fold_market_data(message);
}

template <typename Decoder>
uint64_t decode_length_prefixed_stream(const CorpusBundle& corpus, Decoder&& decode_frame) {
  size_t offset = 0;
  uint64_t folded = 0;
  while (offset < corpus.stream_bytes.size()) {
    if (offset + kOuterPrefixWidthBytes > corpus.stream_bytes.size()) {
      std::cerr << "Benchmark stream prefix truncated.\n";
      std::abort();
    }
    const ByteSpan prefix(corpus.stream_bytes.data() + offset, kOuterPrefixWidthBytes);
    const size_t frame_bytes = read_outer_length(prefix);
    offset += kOuterPrefixWidthBytes;
    if (offset + frame_bytes > corpus.stream_bytes.size()) {
      std::cerr << "Benchmark stream frame truncated.\n";
      std::abort();
    }
    const ByteSpan frame(corpus.stream_bytes.data() + offset, frame_bytes);
    folded ^= decode_frame(frame);
    offset += frame_bytes;
  }
  return folded;
}

ProtocolDefinition make_upr_blob_protocol_definition() {
  FieldDefinition message_type;
  message_type.name = "message_type";
  message_type.kind = FieldKind::kUnsigned;
  message_type.width_bytes = 1;
  message_type.byte_order = ByteOrder::kLittleEndian;
  message_type.has_expected_unsigned = true;
  message_type.expected_unsigned = kBlobMessageType;

  FieldDefinition payload_length;
  payload_length.name = "payload_length";
  payload_length.kind = FieldKind::kUnsigned;
  payload_length.width_bytes = kBlobLengthWidthBytes;
  payload_length.byte_order = ByteOrder::kLittleEndian;

  FieldDefinition payload;
  payload.name = "payload";
  payload.kind = FieldKind::kBytes;
  payload.size_from_field = "payload_length";

  FieldDefinition checksum;
  checksum.name = "checksum";
  checksum.kind = FieldKind::kUnsigned;
  checksum.width_bytes = 1;
  checksum.byte_order = ByteOrder::kLittleEndian;
  checksum.checksum = ChecksumDefinition{
      .algorithm = "xor8",
      .from = "frame_start",
      .to = "before_self",
  };

  MessageDefinition message;
  message.name = "BlobEnvelope";
  message.fields = {message_type, payload_length, payload, checksum};

  ProtocolDefinition protocol;
  protocol.name = "benchmark_blob_envelope";
  protocol.messages = {message};
  return protocol;
}

ProtocolDefinition make_upr_market_data_protocol_definition() {
  FieldDefinition message_type;
  message_type.name = "message_type";
  message_type.kind = FieldKind::kUnsigned;
  message_type.width_bytes = 1;
  message_type.byte_order = ByteOrder::kLittleEndian;
  message_type.has_expected_unsigned = true;
  message_type.expected_unsigned = kMarketDataMessageType;

  auto make_u32 = [](std::string name) {
    FieldDefinition field;
    field.name = std::move(name);
    field.kind = FieldKind::kUnsigned;
    field.width_bytes = sizeof(uint32_t);
    field.byte_order = ByteOrder::kLittleEndian;
    return field;
  };
  auto make_u64 = [](std::string name) {
    FieldDefinition field;
    field.name = std::move(name);
    field.kind = FieldKind::kUnsigned;
    field.width_bytes = sizeof(uint64_t);
    field.byte_order = ByteOrder::kLittleEndian;
    return field;
  };

  FieldDefinition last_price;
  last_price.name = "last_price";
  last_price.kind = FieldKind::kFloat64;
  last_price.width_bytes = sizeof(double);
  last_price.byte_order = ByteOrder::kLittleEndian;

  FieldDefinition symbol;
  symbol.name = "symbol";
  symbol.kind = FieldKind::kString;
  symbol.fixed_size = kMarketSymbolBytes;
  symbol.string_encoding = StringEncoding::kAscii;

  FieldDefinition flags;
  flags.name = "flags";
  flags.kind = FieldKind::kUnsigned;
  flags.width_bytes = 1;
  flags.byte_order = ByteOrder::kLittleEndian;

  FieldDefinition checksum;
  checksum.name = "checksum";
  checksum.kind = FieldKind::kUnsigned;
  checksum.width_bytes = kMarketChecksumWidthBytes;
  checksum.byte_order = ByteOrder::kLittleEndian;
  checksum.checksum = ChecksumDefinition{
      .algorithm = "sum16",
      .from = "frame_start",
      .to = "before_self",
  };

  MessageDefinition message;
  message.name = "MarketData";
  message.fields = {
      message_type,
      make_u32("instrument_id"),
      make_u32("sequence"),
      make_u64("exchange_time_ns"),
      make_u64("receive_time_ns"),
      last_price,
      make_u32("last_qty"),
      make_u32("bid_qty"),
      make_u32("ask_qty"),
      make_u32("bid_price_micros"),
      make_u32("ask_price_micros"),
      symbol,
      flags,
      checksum,
  };

  ProtocolDefinition protocol;
  protocol.name = "benchmark_market_data";
  protocol.messages = {message};
  return protocol;
}

const CompiledProtocol& upr_protocol(ScenarioKind scenario) {
  static const CompiledProtocol kBlobProtocol = [] {
    auto compiled = compile_protocol(make_upr_blob_protocol_definition());
    if (!compiled.ok()) {
      std::cerr << "Failed to compile blob benchmark protocol: " << compiled.status().message() << '\n';
      std::abort();
    }
    return compiled.value();
  }();
  static const CompiledProtocol kMarketProtocol = [] {
    auto compiled = compile_protocol(make_upr_market_data_protocol_definition());
    if (!compiled.ok()) {
      std::cerr << "Failed to compile market-data benchmark protocol: " << compiled.status().message() << '\n';
      std::abort();
    }
    return compiled.value();
  }();

  switch (scenario) {
    case ScenarioKind::KBlobSmall:
    case ScenarioKind::KBlobLarge:
      return kBlobProtocol;
    case ScenarioKind::KMarketData:
      return kMarketProtocol;
  }
  unreachable();
}

CorpusBundle build_blob_corpus(size_t payload_bytes, size_t messages_per_seed, ProtocolKind protocol) {
  CorpusBundle corpus;
  const size_t total_messages = messages_per_seed * kDatasetSeeds.size();
  corpus.message_count = total_messages;
  for (const uint64_t seed : kDatasetSeeds) {
    for (size_t ordinal = 0; ordinal < messages_per_seed; ++ordinal) {
      const BlobEnvelopeData message = make_blob_message(seed, ordinal, payload_bytes);
      std::vector<std::byte> frame;
      switch (protocol) {
        case ProtocolKind::KUpr:
        case ProtocolKind::KPackedBinary:
          frame = encode_binary_blob_frame(message);
          break;
        case ProtocolKind::KProtobuf:
          frame = encode_protobuf_blob_frame(message);
          break;
        case ProtocolKind::KFlatbuffers:
          frame = encode_flatbuffers_blob_frame(message);
          break;
      }
      corpus.total_frame_bytes += frame.size();
      append_length_prefixed_frame(&corpus.stream_bytes, frame);
    }
  }
  return corpus;
}

CorpusBundle build_market_data_corpus(size_t messages_per_seed, ProtocolKind protocol) {
  CorpusBundle corpus;
  const size_t total_messages = messages_per_seed * kDatasetSeeds.size();
  corpus.message_count = total_messages;
  for (const uint64_t seed : kDatasetSeeds) {
    for (size_t ordinal = 0; ordinal < messages_per_seed; ++ordinal) {
      const MarketDataData message = make_market_data_message(seed, ordinal);
      std::vector<std::byte> frame;
      switch (protocol) {
        case ProtocolKind::KUpr:
        case ProtocolKind::KPackedBinary:
          frame = encode_binary_market_data_frame(message);
          break;
        case ProtocolKind::KProtobuf:
          frame = encode_protobuf_market_data_frame(message);
          break;
        case ProtocolKind::KFlatbuffers:
          frame = encode_flatbuffers_market_data_frame(message);
          break;
      }
      corpus.total_frame_bytes += frame.size();
      append_length_prefixed_frame(&corpus.stream_bytes, frame);
    }
  }
  return corpus;
}

size_t protocol_index(ProtocolKind protocol) {
  switch (protocol) {
    case ProtocolKind::KUpr:
      return 0U;
    case ProtocolKind::KPackedBinary:
      return 1U;
    case ProtocolKind::KProtobuf:
      return 2U;
    case ProtocolKind::KFlatbuffers:
      return 3U;
  }
  unreachable();
}

size_t scenario_index(ScenarioKind scenario) {
  switch (scenario) {
    case ScenarioKind::KBlobSmall:
      return 0U;
    case ScenarioKind::KBlobLarge:
      return 1U;
    case ScenarioKind::KMarketData:
      return 2U;
  }
  unreachable();
}

const std::array<std::array<CorpusBundle, kScenarios.size()>, kProtocols.size()>& corpus_bundles() {
  static const auto kBundles = [] {
    std::array<std::array<CorpusBundle, kScenarios.size()>, kProtocols.size()> values{};
    for (const ProtocolKind protocol : kProtocols) {
      values[protocol_index(protocol)][scenario_index(ScenarioKind::KBlobSmall)] =
          build_blob_corpus(kBlobSmallPayloadBytes, kBlobSmallMessagesPerSeed, protocol);
      values[protocol_index(protocol)][scenario_index(ScenarioKind::KBlobLarge)] =
          build_blob_corpus(kBlobLargePayloadBytes, kBlobLargeMessagesPerSeed, protocol);
      values[protocol_index(protocol)][scenario_index(ScenarioKind::KMarketData)] =
          build_market_data_corpus(kMarketMessagesPerSeed, protocol);
    }
    return values;
  }();
  return kBundles;
}

const CorpusBundle& corpus_bundle(const BenchmarkCase& benchmark_case) {
  return corpus_bundles()[protocol_index(benchmark_case.protocol)][scenario_index(benchmark_case.scenario)];
}

std::function<uint64_t()> make_upr_runner(ScenarioKind scenario, const CorpusBundle* corpus) {
  ProtocolDecoder decoder(upr_protocol(scenario));
  switch (scenario) {
    case ScenarioKind::KBlobSmall:
    case ScenarioKind::KBlobLarge:
      return [corpus, decoder]() {
        return decode_length_prefixed_stream(
            *corpus, [&decoder](ByteSpan frame) { return decode_upr_blob_frame(decoder, frame); });
      };
    case ScenarioKind::KMarketData:
      return [corpus, decoder]() {
        return decode_length_prefixed_stream(
            *corpus, [&decoder](ByteSpan frame) { return decode_upr_market_data_frame(decoder, frame); });
      };
  }
  unreachable();
}

}  // namespace

std::span<const BenchmarkCase> benchmark_cases() { return kBenchmarkCases; }

std::string_view to_string(ProtocolKind protocol) {
  switch (protocol) {
    case ProtocolKind::KUpr:
      return "upr";
    case ProtocolKind::KPackedBinary:
      return "packed_binary";
    case ProtocolKind::KProtobuf:
      return "protobuf_lite";
    case ProtocolKind::KFlatbuffers:
      return "flatbuffers";
  }
  unreachable();
}

std::string_view to_string(ScenarioKind scenario) { return scenario_label(scenario); }

CorpusMetrics corpus_metrics(const BenchmarkCase& benchmark_case) {
  const CorpusBundle& corpus = corpus_bundle(benchmark_case);
  return CorpusMetrics{
      .message_count = corpus.message_count,
      .stream_bytes = corpus.stream_bytes.size(),
      .encoded_bytes_per_message = corpus.message_count == 0 ? 0.0
                                                             : static_cast<double>(corpus.total_frame_bytes) /
                                                                   static_cast<double>(corpus.message_count),
      .seed_count = kDatasetSeeds.size(),
  };
}

std::function<uint64_t()> make_decode_runner(const BenchmarkCase& benchmark_case) {
  const CorpusBundle* corpus = &corpus_bundle(benchmark_case);
  switch (benchmark_case.protocol) {
    case ProtocolKind::KUpr:
      return make_upr_runner(benchmark_case.scenario, corpus);
    case ProtocolKind::KPackedBinary:
      switch (benchmark_case.scenario) {
        case ScenarioKind::KBlobSmall:
        case ScenarioKind::KBlobLarge:
          return [corpus]() {
            return decode_length_prefixed_stream(*corpus,
                                                 [](ByteSpan frame) { return decode_binary_blob_frame(frame); });
          };
        case ScenarioKind::KMarketData:
          return [corpus]() {
            return decode_length_prefixed_stream(*corpus,
                                                 [](ByteSpan frame) { return decode_binary_market_data_frame(frame); });
          };
      }
      break;
    case ProtocolKind::KProtobuf:
      switch (benchmark_case.scenario) {
        case ScenarioKind::KBlobSmall:
        case ScenarioKind::KBlobLarge:
          return [corpus]() {
            return decode_length_prefixed_stream(*corpus,
                                                 [](ByteSpan frame) { return decode_protobuf_blob_frame(frame); });
          };
        case ScenarioKind::KMarketData:
          return [corpus]() {
            return decode_length_prefixed_stream(
                *corpus, [](ByteSpan frame) { return decode_protobuf_market_data_frame(frame); });
          };
      }
      break;
    case ProtocolKind::KFlatbuffers:
      switch (benchmark_case.scenario) {
        case ScenarioKind::KBlobSmall:
        case ScenarioKind::KBlobLarge:
          return [corpus]() {
            return decode_length_prefixed_stream(*corpus,
                                                 [](ByteSpan frame) { return decode_flatbuffers_blob_frame(frame); });
          };
        case ScenarioKind::KMarketData:
          return [corpus]() {
            return decode_length_prefixed_stream(
                *corpus, [](ByteSpan frame) { return decode_flatbuffers_market_data_frame(frame); });
          };
      }
      break;
  }
  unreachable();
}

}  // namespace universal_protocol_runtime::benchmarks
