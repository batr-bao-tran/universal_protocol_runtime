#include "universal_protocol_runtime/discovery/protocol_discovery.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "universal_protocol_runtime/compiler/schema_compiler.hpp"
#include "universal_protocol_runtime/core/compiler_hints.hpp"
#include "universal_protocol_runtime/core/unreachable.hpp"

namespace universal_protocol_runtime {
namespace {

struct Cluster {
  std::vector<std::vector<std::byte>> frames;
  size_t first_index = 0;
};

bool bytes_equal_at(const std::vector<std::vector<std::byte>>& frames, size_t position, std::byte* value) {
  if (frames.empty() || frames.front().size() <= position) {
    return false;
  }
  const std::byte expected = frames.front()[position];
  const bool matches =
      std::all_of(frames.begin(), frames.end(), [expected, position](const std::vector<std::byte>& frame) {
        return frame.size() > position && frame[position] == expected;
      });
  if (UPR_UNLIKELY(!matches)) {
    return false;  // LCOV_EXCL_LINE
  }
  if (value != nullptr) {
    *value = expected;
  }
  return true;
}

size_t min_frame_size(const std::vector<std::vector<std::byte>>& frames) {
  size_t result = frames.empty() ? 0 : frames.front().size();
  for (const std::vector<std::byte>& frame : frames) {
    result = std::min(result, frame.size());
  }
  return result;
}

size_t max_frame_size(const std::vector<std::vector<std::byte>>& frames) {
  size_t result = 0;
  for (const std::vector<std::byte>& frame : frames) {
    result = std::max(result, frame.size());
  }
  return result;
}

std::string hex_byte(std::byte value) {
  std::ostringstream stream;
  stream << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
         << static_cast<unsigned>(std::to_integer<unsigned char>(value));
  return stream.str();
}

std::string message_name_for_cluster(const std::vector<std::vector<std::byte>>& frames, size_t ordinal) {
  if (UPR_UNLIKELY(frames.empty() || frames.front().empty())) {
    return "Message_" + std::to_string(ordinal);  // LCOV_EXCL_LINE
  }
  return "Message_" + hex_byte(frames.front().front());
}

size_t compute_common_prefix_length(const std::vector<std::vector<std::byte>>& frames, size_t max_common_prefix_bytes) {
  const size_t limit = std::min(min_frame_size(frames), max_common_prefix_bytes);
  size_t prefix = 0;
  while (prefix < limit) {
    std::byte value{};
    if (!bytes_equal_at(frames, prefix, &value)) {
      break;
    }
    ++prefix;
  }
  return prefix;
}

uint64_t read_length_value(const std::vector<std::byte>& frame,
                           size_t offset,
                           uint8_t width_bytes,
                           ByteOrder byte_order) {
  uint64_t value = 0;
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      for (size_t index = 0; index < width_bytes; ++index) {
        value |= static_cast<uint64_t>(std::to_integer<uint8_t>(frame[offset + index])) << (index * 8U);
      }
      return value;
    case ByteOrder::kBigEndian:
      for (size_t index = 0; index < width_bytes; ++index) {
        value = (value << 8U) | static_cast<uint64_t>(std::to_integer<uint8_t>(frame[offset + index]));
      }
      return value;
  }
  unreachable();  // LCOV_EXCL_LINE
}

std::optional<LengthFieldInference> infer_length_field(const std::vector<std::vector<std::byte>>& frames,
                                                       size_t common_prefix_length,
                                                       const DiscoveryOptions& options) {
  const size_t minimum_size = min_frame_size(frames);
  if (UPR_UNLIKELY(minimum_size <= common_prefix_length)) {
    return std::nullopt;  // LCOV_EXCL_LINE
  }

  const size_t max_offset =
      std::min(minimum_size - 1U, common_prefix_length + options.length_field_search_bytes_after_prefix);
  for (size_t offset = common_prefix_length; offset <= max_offset; ++offset) {
    for (size_t width_bytes = 1; width_bytes <= options.max_length_field_width_bytes; ++width_bytes) {
      if (UPR_UNLIKELY(offset + width_bytes > minimum_size)) {
        continue;  // LCOV_EXCL_LINE
      }
      const std::vector<ByteOrder> byte_orders =
          width_bytes == 1 ? std::vector<ByteOrder>{ByteOrder::kLittleEndian}
                           : std::vector<ByteOrder>{ByteOrder::kLittleEndian, ByteOrder::kBigEndian};
      for (const ByteOrder byte_order : byte_orders) {
        for (size_t trailing_fixed_bytes = 0; trailing_fixed_bytes <= 2; ++trailing_fixed_bytes) {
          bool valid = true;
          for (const std::vector<std::byte>& frame : frames) {
            if (UPR_UNLIKELY(frame.size() < offset + width_bytes + trailing_fixed_bytes)) {
              valid = false;
              break;  // LCOV_EXCL_LINE
            }
            const auto expected_payload_size =
                static_cast<uint64_t>(frame.size() - offset - width_bytes - trailing_fixed_bytes);
            if (read_length_value(frame, offset, width_bytes, byte_order) != expected_payload_size) {
              valid = false;
              break;
            }
          }
          if (valid) {
            return LengthFieldInference{
                .offset = offset,
                .width_bytes = static_cast<uint8_t>(width_bytes),
                .byte_order = byte_order,
                .trailing_fixed_bytes = trailing_fixed_bytes,
            };
          }
        }
      }
    }
  }
  return std::nullopt;  // LCOV_EXCL_LINE
}

bool region_is_ascii_text(const std::vector<std::vector<std::byte>>& frames, size_t start, size_t end) {
  return std::all_of(frames.begin(), frames.end(), [start, end](const std::vector<std::byte>& frame) {
    return std::all_of(frame.begin() + static_cast<ptrdiff_t>(start),
                       frame.begin() + static_cast<ptrdiff_t>(end),
                       [](const std::byte byte) {
                         const auto value = std::to_integer<unsigned char>(byte);
                         return value >= 0x20U && value <= 0x7EU;
                       });
  });
}

void append_expected_byte_field(MessageDefinition* message, std::string name, std::byte value) {
  FieldDefinition field;
  field.name = std::move(name);
  field.kind = FieldKind::kUnsigned;
  field.width_bytes = 1;
  field.byte_order = ByteOrder::kLittleEndian;
  field.has_expected_unsigned = true;
  field.expected_unsigned = std::to_integer<uint8_t>(value);
  message->fields.push_back(std::move(field));
}

void append_fixed_region_fields(MessageDefinition* message,
                                const std::vector<std::vector<std::byte>>& frames,
                                size_t start,
                                size_t end,
                                size_t* next_field_index,
                                size_t* next_text_index,
                                size_t* next_bytes_index) {
  size_t position = start;
  while (position < end) {
    std::byte fixed_value{};
    if (bytes_equal_at(frames, position, &fixed_value)) {
      append_expected_byte_field(message, "fixed_" + std::to_string(position), fixed_value);
      ++position;
      continue;
    }

    size_t run_end = position + 1U;
    while (run_end < end) {
      std::byte later_value{};
      if (bytes_equal_at(frames, run_end, &later_value)) {
        break;
      }
      ++run_end;
    }

    FieldDefinition field;
    if (run_end - position == 1U) {
      field.name = "field_" + std::to_string((*next_field_index)++);
      field.kind = FieldKind::kUnsigned;
      field.width_bytes = 1;
      field.byte_order = ByteOrder::kLittleEndian;
    } else if (region_is_ascii_text(frames, position, run_end)) {
      field.name = "text_" + std::to_string((*next_text_index)++);
      field.kind = FieldKind::kString;
      field.fixed_size = run_end - position;
      field.string_encoding = StringEncoding::kAscii;
    } else {
      field.name = "bytes_" + std::to_string((*next_bytes_index)++);
      field.kind = FieldKind::kBytes;
      field.fixed_size = run_end - position;
    }
    message->fields.push_back(std::move(field));
    position = run_end;
  }
}

std::vector<std::byte> copy_prefix(const std::vector<std::vector<std::byte>>& frames, size_t common_prefix_length) {
  std::vector<std::byte> prefix;
  prefix.reserve(common_prefix_length);
  for (size_t index = 0; index < common_prefix_length; ++index) {
    prefix.push_back(frames.front()[index]);
  }
  return prefix;
}

std::string summarize_strategy(size_t common_prefix_length,
                               size_t minimum_size,
                               size_t maximum_size,
                               const std::optional<LengthFieldInference>& length_field,
                               bool allow_trailing_bytes) {
  std::ostringstream stream;
  stream << "common_prefix=" << common_prefix_length << " min_size=" << minimum_size << " max_size=" << maximum_size;
  if (length_field.has_value()) {
    stream << " length_field=offset:" << length_field->offset
           << "/width:" << static_cast<unsigned>(length_field->width_bytes)
           << " trailing_fixed=" << length_field->trailing_fixed_bytes;
  } else if (allow_trailing_bytes) {
    stream << " trailing_bytes=allowed";
  } else {
    stream << " fixed_size=yes";
  }
  return stream.str();
}

DiscoveredMessage discover_message(const std::vector<std::vector<std::byte>>& frames,
                                   size_t ordinal,
                                   const DiscoveryOptions& options) {
  DiscoveredMessage discovered;
  discovered.name = message_name_for_cluster(frames, ordinal);
  discovered.sample_count = frames.size();
  discovered.min_size = min_frame_size(frames);
  discovered.max_size = max_frame_size(frames);
  discovered.variable_length = discovered.min_size != discovered.max_size;
  const size_t common_prefix_length = compute_common_prefix_length(frames, options.max_common_prefix_bytes);
  discovered.common_prefix = copy_prefix(frames, common_prefix_length);
  if (discovered.variable_length) {
    discovered.inferred_length_field = infer_length_field(frames, common_prefix_length, options);
  }

  MessageDefinition draft_message;
  draft_message.name = discovered.name;
  size_t next_field_index = 0;
  size_t next_text_index = 0;
  size_t next_bytes_index = 0;

  for (size_t index = 0; index < common_prefix_length; ++index) {
    append_expected_byte_field(
        &draft_message, index == 0 ? "message_type" : "prefix_" + std::to_string(index), frames.front()[index]);
  }

  if (discovered.inferred_length_field.has_value()) {
    const LengthFieldInference& length_field = *discovered.inferred_length_field;
    append_fixed_region_fields(&draft_message,
                               frames,
                               common_prefix_length,
                               length_field.offset,
                               &next_field_index,
                               &next_text_index,
                               &next_bytes_index);

    FieldDefinition length_definition;
    length_definition.name = "length";
    length_definition.kind = FieldKind::kUnsigned;
    length_definition.width_bytes = length_field.width_bytes;
    length_definition.byte_order = length_field.byte_order;
    draft_message.fields.push_back(std::move(length_definition));

    FieldDefinition payload_definition;
    payload_definition.name = "payload";
    payload_definition.kind = FieldKind::kBytes;
    payload_definition.size_from_field = "length";
    draft_message.fields.push_back(std::move(payload_definition));

    if (length_field.trailing_fixed_bytes > 0) {
      FieldDefinition trailer_definition;
      trailer_definition.name = "trailer";
      trailer_definition.kind = FieldKind::kBytes;
      trailer_definition.fixed_size = length_field.trailing_fixed_bytes;
      draft_message.fields.push_back(std::move(trailer_definition));
    }
  } else if (discovered.variable_length) {
    append_fixed_region_fields(&draft_message,
                               frames,
                               common_prefix_length,
                               discovered.min_size,
                               &next_field_index,
                               &next_text_index,
                               &next_bytes_index);
    draft_message.allow_trailing_bytes = true;
    discovered.allow_trailing_bytes = true;
  } else {
    append_fixed_region_fields(&draft_message,
                               frames,
                               common_prefix_length,
                               discovered.max_size,
                               &next_field_index,
                               &next_text_index,
                               &next_bytes_index);
  }

  discovered.strategy_summary = summarize_strategy(common_prefix_length,
                                                   discovered.min_size,
                                                   discovered.max_size,
                                                   discovered.inferred_length_field,
                                                   discovered.allow_trailing_bytes);
  discovered.draft_message = draft_message;

  const size_t samples_to_copy = std::min(frames.size(), options.sample_frames_per_message);
  discovered.sample_frames.assign(frames.begin(), frames.begin() + static_cast<ptrdiff_t>(samples_to_copy));
  return discovered;
}

std::vector<Cluster> cluster_frames(const std::vector<std::vector<std::byte>>& frames,
                                    const DiscoveryOptions& options) {
  if (!options.cluster_by_first_byte) {
    return {Cluster{.frames = frames, .first_index = 0}};
  }

  std::map<uint8_t, Cluster> clusters_by_key;
  for (size_t index = 0; index < frames.size(); ++index) {
    const std::vector<std::byte>& frame = frames[index];
    const auto key = std::to_integer<uint8_t>(frame.front());
    Cluster& cluster = clusters_by_key[key];
    if (cluster.frames.empty()) {
      cluster.first_index = index;
    }
    cluster.frames.push_back(frame);
  }

  std::vector<Cluster> clusters;
  clusters.reserve(clusters_by_key.size());
  for (auto& entry : clusters_by_key) {
    clusters.push_back(std::move(entry.second));
  }
  std::sort(clusters.begin(), clusters.end(), [](const Cluster& left, const Cluster& right) {
    return left.first_index < right.first_index;
  });
  return clusters;
}

}  // namespace

StatusOr<DiscoveryReport> discover_protocol_from_samples(const std::vector<std::vector<std::byte>>& frames,
                                                         const DiscoveryOptions& options) {
  if (options.protocol_name.empty()) {
    return invalid_argument("Discovery protocol name must not be empty.");
  }
  if (options.sample_frames_per_message == 0) {
    return invalid_argument("Discovery must retain at least one sample frame per message.");
  }
  if (frames.empty()) {
    return invalid_argument("Protocol discovery requires at least one frame sample.");
  }

  std::vector<std::vector<std::byte>> non_empty_frames;
  non_empty_frames.reserve(frames.size());
  size_t discarded = 0;
  for (const std::vector<std::byte>& frame : frames) {
    if (frame.empty()) {
      ++discarded;
      continue;
    }
    non_empty_frames.push_back(frame);
  }
  if (non_empty_frames.empty()) {
    return invalid_argument("Protocol discovery requires at least one non-empty frame sample.");
  }

  DiscoveryReport report;
  report.protocol_name = options.protocol_name;
  report.frames_analyzed = non_empty_frames.size();
  report.frames_discarded = discarded;
  report.draft_protocol.name = options.protocol_name;

  const std::vector<Cluster> clusters = cluster_frames(non_empty_frames, options);
  report.messages.reserve(clusters.size());
  report.draft_protocol.messages.reserve(clusters.size());
  for (size_t index = 0; index < clusters.size(); ++index) {
    DiscoveredMessage discovered = discover_message(clusters[index].frames, index, options);
    report.draft_protocol.messages.push_back(discovered.draft_message);
    report.messages.push_back(std::move(discovered));
  }

  StatusOr<CompiledProtocol> compiled = compile_protocol(report.draft_protocol);
  if (!compiled.ok()) {
    return Status(compiled.status().code(),
                  "Discovered protocol did not compile cleanly: " + std::string(compiled.status().message()));
  }
  report.draft_fingerprint = compiled.value().fingerprint();
  return report;
}

}  // namespace universal_protocol_runtime
