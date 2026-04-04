#include "universal_protocol_runtime/compiler/checksum_registry.hpp"

#include <array>
#include <cctype>
#include <mutex>
#include <unordered_map>

namespace universal_protocol_runtime {
namespace {

constexpr size_t kCrcTableEntries = 256U;
constexpr size_t kBitsPerByte = 8U;
constexpr uint16_t kCrc16CcittPolynomial = 0x8408U;
constexpr uint32_t kCrc32Polynomial = 0xEDB88320U;
constexpr uint32_t kCrc32cPolynomial = 0x82F63B78U;
constexpr uint16_t kCrc16InitialValue = 0xFFFFU;
constexpr uint32_t kCrc32InitialValue = 0xFFFFFFFFU;
constexpr uint8_t kChecksumXor8WidthBytes = sizeof(uint8_t);
constexpr uint8_t kChecksumCrc16WidthBytes = sizeof(uint16_t);
constexpr uint8_t kChecksumCrc32WidthBytes = sizeof(uint32_t);
constexpr std::string_view kChecksumAlgorithmCrc16Ccitt = "crc16_ccitt";
constexpr std::string_view kChecksumAlgorithmCrc32 = "crc32";
constexpr std::string_view kChecksumAlgorithmCrc32c = "crc32c";
constexpr std::string_view kChecksumAlgorithmXor8 = "xor8";
constexpr std::string_view kChecksumAlgorithmSum16 = "sum16";
constexpr std::string_view kErrorChecksumNameEmpty = "Checksum algorithm name must not be empty.";
constexpr std::string_view kErrorChecksumFunctionNull = "Checksum algorithm function must not be null.";
constexpr std::string_view kErrorChecksumWidthRange = "Checksum algorithm result width must be between 1 and 8 bytes.";

template <typename UIntType>
constexpr std::array<UIntType, kCrcTableEntries> make_crc_table(UIntType polynomial) {
  std::array<UIntType, kCrcTableEntries> table{};
  for (size_t index = 0; index < table.size(); ++index) {
    auto value = static_cast<UIntType>(index);
    for (size_t bit = 0; bit < kBitsPerByte; ++bit) {
      value =
          (value & 1U) != 0U ? static_cast<UIntType>((value >> 1U) ^ polynomial) : static_cast<UIntType>(value >> 1U);
    }
    table[index] = value;
  }
  return table;
}

constexpr std::array<uint16_t, kCrcTableEntries> kCrc16CcittTable = make_crc_table<uint16_t>(kCrc16CcittPolynomial);
constexpr std::array<uint32_t, kCrcTableEntries> kCrc32Table = make_crc_table<uint32_t>(kCrc32Polynomial);
constexpr std::array<uint32_t, kCrcTableEntries> kCrc32cTable = make_crc_table<uint32_t>(kCrc32cPolynomial);

uint64_t checksum_crc16_ccitt(ByteSpan bytes) noexcept {
  uint16_t crc = kCrc16InitialValue;
  for (const std::byte byte : bytes) {
    const auto index = static_cast<uint8_t>(crc ^ static_cast<uint16_t>(std::to_integer<uint8_t>(byte)));
    crc = static_cast<uint16_t>((crc >> kBitsPerByte) ^ kCrc16CcittTable[index]);
  }
  return static_cast<uint16_t>(~crc);
}

uint64_t checksum_crc32(ByteSpan bytes) noexcept {
  uint32_t crc = kCrc32InitialValue;
  for (const std::byte byte : bytes) {
    const auto index = static_cast<uint8_t>(crc ^ static_cast<uint32_t>(std::to_integer<uint8_t>(byte)));
    crc = (crc >> kBitsPerByte) ^ kCrc32Table[index];
  }
  return ~crc;
}

uint64_t checksum_crc32c(ByteSpan bytes) noexcept {
  uint32_t crc = kCrc32InitialValue;
  for (const std::byte byte : bytes) {
    const auto index = static_cast<uint8_t>(crc ^ static_cast<uint32_t>(std::to_integer<uint8_t>(byte)));
    crc = (crc >> kBitsPerByte) ^ kCrc32cTable[index];
  }
  return ~crc;
}

uint64_t checksum_xor8(ByteSpan bytes) noexcept {
  uint8_t value = 0;
  for (const std::byte byte : bytes) {
    value ^= std::to_integer<uint8_t>(byte);
  }
  return value;
}

uint64_t checksum_sum16(ByteSpan bytes) noexcept {
  uint32_t sum = 0;
  for (const std::byte byte : bytes) {
    sum += std::to_integer<uint8_t>(byte);
  }
  return static_cast<uint16_t>(sum & 0xFFFFU);
}

std::string normalize_name(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (const char character : value) {
    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return normalized;
}

using Registry = std::unordered_map<std::string, ChecksumAlgorithmSpec>;

Registry& checksum_registry() {
  static Registry registry = [] {
    Registry value;
    value.emplace(kChecksumAlgorithmCrc16Ccitt,
                  ChecksumAlgorithmSpec{
                      .name = std::string(kChecksumAlgorithmCrc16Ccitt),
                      .result_width_bytes = kChecksumCrc16WidthBytes,
                      .function = &checksum_crc16_ccitt,
                  });
    value.emplace(kChecksumAlgorithmCrc32,
                  ChecksumAlgorithmSpec{
                      .name = std::string(kChecksumAlgorithmCrc32),
                      .result_width_bytes = kChecksumCrc32WidthBytes,
                      .function = &checksum_crc32,
                  });
    value.emplace(kChecksumAlgorithmCrc32c,
                  ChecksumAlgorithmSpec{
                      .name = std::string(kChecksumAlgorithmCrc32c),
                      .result_width_bytes = kChecksumCrc32WidthBytes,
                      .function = &checksum_crc32c,
                  });
    value.emplace(kChecksumAlgorithmXor8,
                  ChecksumAlgorithmSpec{
                      .name = std::string(kChecksumAlgorithmXor8),
                      .result_width_bytes = kChecksumXor8WidthBytes,
                      .function = &checksum_xor8,
                  });
    value.emplace(kChecksumAlgorithmSum16,
                  ChecksumAlgorithmSpec{
                      .name = std::string(kChecksumAlgorithmSum16),
                      .result_width_bytes = kChecksumCrc16WidthBytes,
                      .function = &checksum_sum16,
                  });
    return value;
  }();
  return registry;
}

std::mutex& checksum_registry_mutex() {
  static std::mutex mutex;
  return mutex;
}

}  // namespace

Status register_checksum_algorithm(ChecksumAlgorithmSpec spec) {
  if (spec.name.empty()) {
    return invalid_argument(std::string(kErrorChecksumNameEmpty));
  }
  if (spec.function == nullptr) {
    return invalid_argument(std::string(kErrorChecksumFunctionNull));
  }
  if (spec.result_width_bytes == 0 || spec.result_width_bytes > sizeof(uint64_t)) {
    return invalid_argument(std::string(kErrorChecksumWidthRange));
  }

  std::scoped_lock lock(checksum_registry_mutex());
  Registry& registry = checksum_registry();
  const std::string normalized = normalize_name(spec.name);
  if (registry.contains(normalized)) {
    return invalid_argument("Duplicate checksum algorithm: " + normalized);
  }
  spec.name = normalized;
  registry.emplace(spec.name, std::move(spec));
  return Status::ok_status();
}

StatusOr<ChecksumAlgorithmSpec> find_checksum_algorithm(std::string_view name) {
  if (name.empty()) {
    return invalid_argument(std::string(kErrorChecksumNameEmpty));
  }

  std::scoped_lock lock(checksum_registry_mutex());
  Registry& registry = checksum_registry();
  const std::string normalized = normalize_name(name);
  const auto it = registry.find(normalized);
  if (it == registry.end()) {
    return not_found("Unknown checksum algorithm: " + normalized);
  }
  return it->second;
}

}  // namespace universal_protocol_runtime
