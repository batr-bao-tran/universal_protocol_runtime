#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_DISCOVERY_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DISCOVERY__PROTOCOL_DISCOVERY_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_DISCOVERY_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DISCOVERY__PROTOCOL_DISCOVERY_HPP_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_protocol_runtime/core/types.hpp"
#include "universal_protocol_runtime/pdl/protocol_definition.hpp"
#include "utils/status.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Heuristic description of a likely length field inside sampled messages.
 */
struct LengthFieldInference {
  size_t offset = 0;
  uint8_t width_bytes = 0;
  ByteOrder byte_order = ByteOrder::kLittleEndian;
  size_t trailing_fixed_bytes = 0;
};

/**
 * @brief One discovered message cluster inferred from sampled frames.
 */
struct DiscoveredMessage {
  std::string name;
  size_t sample_count = 0;
  size_t min_size = 0;
  size_t max_size = 0;
  bool variable_length = false;
  bool allow_trailing_bytes = false;
  std::vector<std::byte> common_prefix;
  std::optional<LengthFieldInference> inferred_length_field;
  std::string strategy_summary;
  std::vector<std::vector<std::byte>> sample_frames;
  MessageDefinition draft_message;
};

/**
 * @brief Aggregate discovery result for one sampled protocol.
 */
struct DiscoveryReport {
  std::string protocol_name;
  size_t frames_analyzed = 0;
  size_t frames_discarded = 0;
  uint64_t draft_fingerprint = 0;
  std::vector<DiscoveredMessage> messages;
  ProtocolDefinition draft_protocol;
};

/**
 * @brief Options controlling heuristic protocol discovery.
 */
struct DiscoveryOptions {
  std::string protocol_name = "discovered_protocol";
  size_t max_common_prefix_bytes = 4;
  size_t sample_frames_per_message = 3;
  size_t max_length_field_width_bytes = 2;
  size_t length_field_search_bytes_after_prefix = 3;
  bool cluster_by_first_byte = true;
};

/**
 * @brief Infers a draft protocol definition from sampled framed payloads.
 * @param frames Sampled framed payloads to analyze.
 * @param options Discovery heuristics and output controls.
 * @return Discovery report or an error status.
 */
StatusOr<DiscoveryReport> discover_protocol_from_samples(const std::vector<std::vector<std::byte>>& frames,
                                                         const DiscoveryOptions& options = {});

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_DISCOVERY_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_DISCOVERY__PROTOCOL_DISCOVERY_HPP_
