#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_COMPILER_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_COMPILER__CHECKSUM_REGISTRY_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_COMPILER_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_COMPILER__CHECKSUM_REGISTRY_HPP_
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "universal_protocol_runtime/core/byte_view.hpp"
#include "utils/status.hpp"

namespace universal_protocol_runtime {

using ChecksumFunction = uint64_t (*)(ByteSpan) noexcept;

struct ChecksumAlgorithmSpec {
  std::string name;
  uint8_t result_width_bytes = 0;
  ChecksumFunction function = nullptr;
};

Status register_checksum_algorithm(ChecksumAlgorithmSpec spec);

StatusOr<ChecksumAlgorithmSpec> find_checksum_algorithm(std::string_view name);

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_COMPILER__CHECKSUM_REGISTRY_HPP_
