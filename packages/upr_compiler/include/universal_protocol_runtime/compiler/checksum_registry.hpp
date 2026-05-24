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

/**
 * @brief Describes one named checksum algorithm available to the compiler and runtime.
 */
struct ChecksumAlgorithmSpec {
  std::string name;
  uint8_t result_width_bytes = 0;
  ChecksumFunction function = nullptr;
};

/**
 * @brief Registers a checksum algorithm by name.
 * @param spec Checksum algorithm specification to register.
 * @return Success or an error status if registration fails.
 */
Status register_checksum_algorithm(ChecksumAlgorithmSpec spec);

/**
 * @brief Looks up a registered checksum algorithm by name.
 * @param name Checksum algorithm name.
 * @return Matching checksum specification or an error status.
 */
StatusOr<ChecksumAlgorithmSpec> find_checksum_algorithm(std::string_view name);

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_COMPILER__CHECKSUM_REGISTRY_HPP_
