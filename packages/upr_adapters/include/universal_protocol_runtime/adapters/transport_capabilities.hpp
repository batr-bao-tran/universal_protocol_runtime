#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__TRANSPORT_CAPABILITIES_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__TRANSPORT_CAPABILITIES_HPP_

#include <cstdint>

namespace universal_protocol_runtime {

/**
 * @brief Bit flags describing transport features and behavior.
 */
enum class TransportCapability : uint32_t {
  kStream = 1U << 0U,
  kDatagram = 1U << 1U,
  kZeroCopyReceive = 1U << 2U,
  kZeroCopySend = 1U << 3U,
  kSharedMemory = 1U << 4U,
  kKernelBatching = 1U << 5U,
  kOutOfOrderCompletions = 1U << 6U,
  kPreservesFrameBoundaries = 1U << 7U,
};

using TransportCapabilityMask = uint32_t;

/**
 * @brief Converts a capability enum value to a bitmask.
 */
constexpr TransportCapabilityMask capability_mask(TransportCapability capability) {
  return static_cast<TransportCapabilityMask>(capability);
}

/**
 * @brief Checks whether a capability bit is present in a mask.
 */
constexpr bool has_capability(TransportCapabilityMask mask, TransportCapability capability) {
  return (mask & capability_mask(capability)) != 0U;
}

/**
 * @brief Combines two capability enum values into a mask.
 */
constexpr TransportCapabilityMask operator|(TransportCapability lhs, TransportCapability rhs) {
  return capability_mask(lhs) | capability_mask(rhs);
}

/**
 * @brief Adds a capability enum value to an existing mask.
 */
constexpr TransportCapabilityMask operator|(TransportCapabilityMask lhs, TransportCapability rhs) {
  return lhs | capability_mask(rhs);
}

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_ADAPTERS_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_ADAPTERS__TRANSPORT_CAPABILITIES_HPP_
