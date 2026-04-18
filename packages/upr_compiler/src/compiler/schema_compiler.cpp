#include "universal_protocol_runtime/compiler/schema_compiler.hpp"

#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "universal_protocol_runtime/core/unreachable.hpp"
#include "utils/xxhash64.hpp"

namespace universal_protocol_runtime {
namespace {

constexpr uint8_t kScalarWidth1 = sizeof(uint8_t);
constexpr uint8_t kScalarWidth2 = sizeof(uint16_t);
constexpr uint8_t kScalarWidth4 = sizeof(uint32_t);
constexpr uint8_t kScalarWidth8 = sizeof(uint64_t);
constexpr uint8_t kFloat32WidthBytes = sizeof(uint32_t);
constexpr uint8_t kFloat64WidthBytes = sizeof(uint64_t);
constexpr std::string_view kLayoutKindStruct = "struct";
constexpr std::string_view kLayoutKindMessage = "message";
constexpr std::string_view kFingerprintStructToken = "struct";
constexpr std::string_view kFingerprintMessageToken = "message";
constexpr std::string_view kChecksumAnchorFrameStart = "frame_start";
constexpr std::string_view kChecksumAnchorFrameEnd = "frame_end";
constexpr std::string_view kChecksumAnchorBeforeSelf = "before_self";
constexpr std::string_view kChecksumAnchorAfterSelf = "after_self";
constexpr std::string_view kChecksumAnchorStartSuffix = ".start";
constexpr std::string_view kChecksumAnchorEndSuffix = ".end";

}  // namespace

CompiledMessage::CompiledMessage(std::string name,
                                 std::vector<CompiledField> fields,
                                 std::vector<CompiledBitField> bit_fields,
                                 std::vector<CompiledChecksum> checksums,
                                 size_t minimum_size,
                                 bool allow_trailing_bytes,
                                 std::vector<std::byte> dispatch_prefix)
    : name_(std::move(name)),
      fields_(std::move(fields)),
      bit_fields_(std::move(bit_fields)),
      checksums_(std::move(checksums)),
      minimum_size_(minimum_size),
      allow_trailing_bytes_(allow_trailing_bytes),
      dispatch_prefix_(std::move(dispatch_prefix)) {
  field_ids_.reserve(fields_.size());
  for (const CompiledField& field : fields_) {
    field_ids_.emplace(field.name, field.id);
  }
  bit_field_ids_.reserve(bit_fields_.size());
  for (const CompiledBitField& bit_field : bit_fields_) {
    bit_field_ids_.emplace(bit_field.name, bit_field.id);
  }
}

std::optional<FieldId> CompiledMessage::find_field(std::string_view field_name) const {
  const auto it = field_ids_.find(field_name);
  if (it == field_ids_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<BitFieldId> CompiledMessage::find_bit_field(std::string_view bit_field_name) const {
  const auto it = bit_field_ids_.find(bit_field_name);
  if (it == bit_field_ids_.end()) {
    return std::nullopt;
  }
  return it->second;
}

CompiledProtocol::CompiledProtocol(std::string name,
                                   uint64_t fingerprint,
                                   std::vector<CompiledMessage> structs,
                                   std::vector<CompiledMessage> messages)
    : name_(std::move(name)), fingerprint_(fingerprint), structs_(std::move(structs)), messages_(std::move(messages)) {
  struct_ids_.reserve(structs_.size());
  for (size_t index = 0; index < structs_.size(); ++index) {
    struct_ids_.emplace(std::string(structs_[index].name()), index);
  }
  message_ids_.reserve(messages_.size());
  for (size_t index = 0; index < messages_.size(); ++index) {
    message_ids_.emplace(std::string(messages_[index].name()), index);
    const std::span<const std::byte> dispatch_prefix = messages_[index].dispatch_prefix();
    if (dispatch_prefix.empty()) {
      fallback_message_ids_.push_back(index);
      continue;
    }
    dispatch_message_ids_[std::to_integer<uint8_t>(dispatch_prefix.front())].push_back(index);
  }
}

const CompiledMessage* CompiledProtocol::find_message(std::string_view message_name) const {
  const auto it = message_ids_.find(message_name);
  if (it == message_ids_.end()) {
    return nullptr;
  }
  return &messages_[it->second];
}

const CompiledMessage* CompiledProtocol::find_struct(std::string_view struct_name) const {
  const auto it = struct_ids_.find(struct_name);
  if (it == struct_ids_.end()) {
    return nullptr;
  }
  return &structs_[it->second];
}

const CompiledMessage* CompiledProtocol::struct_by_id(size_t struct_id) const {
  if (struct_id >= structs_.size()) {
    return nullptr;
  }
  return &structs_[struct_id];
}

std::span<const size_t> CompiledProtocol::dispatch_candidate_ids(ByteSpan frame) const noexcept {
  if (frame.empty()) {
    return {};
  }
  return dispatch_message_ids_[std::to_integer<uint8_t>(frame.front())];
}

namespace {

bool is_valid_scalar_width(uint8_t width_bytes) {
  return width_bytes == kScalarWidth1 || width_bytes == kScalarWidth2 || width_bytes == kScalarWidth4 ||
         width_bytes == kScalarWidth8;
}

void append_expected_scalar_bytes(std::vector<std::byte>* dispatch_prefix,
                                  uint64_t value,
                                  uint8_t width_bytes,
                                  ByteOrder byte_order) {
  const size_t start = dispatch_prefix->size();
  dispatch_prefix->resize(start + width_bytes);
  switch (byte_order) {
    case ByteOrder::kLittleEndian:
      for (size_t index = 0; index < width_bytes; ++index) {
        (*dispatch_prefix)[start + index] = std::byte{static_cast<uint8_t>((value >> (index * kBitsPerByte)) & 0xFFU)};
      }
      return;
    case ByteOrder::kBigEndian:
      for (size_t index = 0; index < width_bytes; ++index) {
        const size_t shift = (static_cast<size_t>(width_bytes) - index - 1U) * kBitsPerByte;
        (*dispatch_prefix)[start + index] = std::byte{static_cast<uint8_t>((value >> shift) & 0xFFU)};
      }
      return;
  }
  unreachable();
}

void fingerprint_bit_field(XxHash64State* hasher, const BitFieldDefinition& bit_field) {
  hasher->update(bit_field.name);
  hasher->update_integral(bit_field.offset_bits);
  hasher->update_integral(bit_field.width_bits);
  hasher->update_bool(bit_field.is_signed);
  for (const EnumValueDefinition& enum_value : bit_field.enum_values) {
    hasher->update_integral(enum_value.value);
    hasher->update(enum_value.name);
  }
}

void fingerprint_field(XxHash64State* hasher, const FieldDefinition& field) {
  hasher->update(field.name);
  hasher->update_integral(field.kind);
  hasher->update_integral(field.width_bytes);
  hasher->update_integral(field.byte_order);
  hasher->update_integral(field.string_encoding);
  hasher->update_integral(field.fixed_size);
  hasher->update(field.size_from_field);
  hasher->update(field.referenced_type);
  hasher->update_bool(field.has_expected_unsigned);
  hasher->update_integral(field.expected_unsigned);
  for (const EnumValueDefinition& enum_value : field.enum_values) {
    hasher->update_integral(enum_value.value);
    hasher->update(enum_value.name);
  }
  for (const BitFieldDefinition& bit_field : field.bit_fields) {
    fingerprint_bit_field(hasher, bit_field);
  }
  hasher->update_bool(field.checksum.has_value());
  if (field.checksum.has_value()) {
    hasher->update(field.checksum->algorithm);
    hasher->update(field.checksum->from);
    hasher->update(field.checksum->to);
  }
}

void fingerprint_enum(XxHash64State* hasher, const EnumDefinition& definition) {
  hasher->update("enum");
  hasher->update(definition.name);
  hasher->update_integral(definition.width_bytes);
  hasher->update_integral(definition.byte_order);
  for (const EnumValueDefinition& enum_value : definition.values) {
    hasher->update_integral(enum_value.value);
    hasher->update(enum_value.name);
  }
}

Status validate_protocol_definition(const ProtocolDefinition& definition) {
  if (!definition.imports.empty()) {
    return invalid_argument("Protocol imports must be resolved before compilation.");
  }
  if (definition.name.empty()) {
    return invalid_argument("Protocol name must not be empty.");
  }
  if (definition.messages.empty()) {
    return invalid_argument("Protocol must define at least one message.");
  }
  return Status::ok_status();
}

struct LayoutDefinitionView {
  std::string_view name;
  const std::vector<FieldDefinition>* fields = nullptr;
  bool allow_trailing_bytes = false;
  std::string_view kind_name;
};

struct PendingChecksum {
  FieldId field_id = 0;
  ChecksumAlgorithmSpec spec;
  std::string from_anchor;
  std::string to_anchor;
};

using FieldIdMap = std::unordered_map<std::string, FieldId, TransparentStringHash, std::equal_to<>>;

CompiledChecksum::BuiltinKind checksum_builtin_kind(std::string_view algorithm) noexcept {
  if (algorithm == "xor8") {
    return CompiledChecksum::BuiltinKind::kXor8;
  }
  if (algorithm == "sum16") {
    return CompiledChecksum::BuiltinKind::kSum16;
  }
  if (algorithm == "crc16_ccitt") {
    return CompiledChecksum::BuiltinKind::kCrc16Ccitt;
  }
  if (algorithm == "crc32") {
    return CompiledChecksum::BuiltinKind::kCrc32;
  }
  if (algorithm == "crc32c") {
    return CompiledChecksum::BuiltinKind::kCrc32c;
  }
  return CompiledChecksum::BuiltinKind::kCustom;
}

class ProtocolCompiler {
 public:
  explicit ProtocolCompiler(const ProtocolDefinition& definition) : definition_(definition) {}
  ~ProtocolCompiler() noexcept = default;

  StatusOr<CompiledProtocol> compile() {
    const Status definition_status = validate_protocol_definition(definition_);
    if (!definition_status.ok()) {
      return definition_status;
    }

    const Status name_status = validate_names();
    if (!name_status.ok()) {
      return name_status;
    }

    compiled_structs_.resize(definition_.structs.size());
    struct_states_.assign(definition_.structs.size(), CompileState::KUnvisited);
    for (size_t index = 0; index < definition_.structs.size(); ++index) {
      const StatusOr<size_t> compiled_struct_id = compile_struct(index);
      if (!compiled_struct_id.ok()) {
        return compiled_struct_id.status();
      }
    }

    std::vector<CompiledMessage> compiled_messages;
    compiled_messages.reserve(definition_.messages.size());
    for (const MessageDefinition& message : definition_.messages) {
      const auto compiled_message = compile_layout(LayoutDefinitionView{
          .name = message.name,
          .fields = &message.fields,
          .allow_trailing_bytes = message.allow_trailing_bytes,
          .kind_name = kLayoutKindMessage,
      });
      if (!compiled_message.ok()) {
        return compiled_message.status();
      }
      compiled_messages.push_back(std::move(compiled_message).value());
    }

    return CompiledProtocol(
        definition_.name, fingerprint(), std::move(compiled_structs_), std::move(compiled_messages));
  }

 private:
  enum class CompileState {
    KUnvisited,
    KVisiting,
    KDone,
  };

  Status validate_names() {
    std::unordered_set<std::string> layout_names;
    layout_names.reserve(definition_.enums.size() + definition_.structs.size() + definition_.messages.size());
    enum_ids_.reserve(definition_.enums.size());
    for (const EnumDefinition& enum_definition : definition_.enums) {
      if (enum_definition.name.empty()) {
        return invalid_argument("Enum name must not be empty.");
      }
      if (!layout_names.insert(enum_definition.name).second) {
        return invalid_argument("Duplicate layout name: " + enum_definition.name);
      }
      if (!is_valid_scalar_width(enum_definition.width_bytes)) {
        return invalid_argument("Enum '" + enum_definition.name + "' has an unsupported underlying width.");
      }
      enum_ids_.emplace(enum_definition.name, enum_ids_.size());
    }
    struct_ids_.reserve(definition_.structs.size());
    for (const StructDefinition& struct_definition : definition_.structs) {
      if (struct_definition.name.empty()) {
        return invalid_argument("Struct name must not be empty.");
      }
      if (!layout_names.insert(struct_definition.name).second) {
        return invalid_argument("Duplicate layout name: " + struct_definition.name);
      }
      struct_ids_.emplace(struct_definition.name, struct_ids_.size());
    }
    for (const MessageDefinition& message : definition_.messages) {
      if (message.name.empty()) {
        return invalid_argument("Message name must not be empty.");
      }
      if (!layout_names.insert(message.name).second) {
        return invalid_argument("Duplicate layout name: " + message.name);
      }
    }
    return Status::ok_status();
  }

  // Struct compilation intentionally recurses through nested struct references.
  // NOLINTNEXTLINE(misc-no-recursion)
  StatusOr<size_t> compile_struct(size_t struct_id) {
    if (struct_states_[struct_id] == CompileState::KDone) {
      return struct_id;
    }
    if (struct_states_[struct_id] == CompileState::KVisiting) {
      return invalid_argument("Recursive struct dependency detected for '" + definition_.structs[struct_id].name +
                              "'.");
    }

    const StructDefinition& struct_definition = definition_.structs[struct_id];
    struct_states_[struct_id] = CompileState::KVisiting;
    const auto compiled_struct = compile_layout(LayoutDefinitionView{
        .name = struct_definition.name,
        .fields = &struct_definition.fields,
        .allow_trailing_bytes = false,
        .kind_name = kLayoutKindStruct,
    });
    if (!compiled_struct.ok()) {
      struct_states_[struct_id] = CompileState::KUnvisited;
      return compiled_struct.status();
    }
    compiled_structs_[struct_id] = std::move(compiled_struct).value();
    struct_states_[struct_id] = CompileState::KDone;
    return struct_id;
  }

  static StatusOr<CompiledChecksumAnchor> resolve_checksum_anchor(std::string_view anchor,
                                                                  FieldId self_field_id,
                                                                  const FieldIdMap& field_ids) {
    if (anchor == kChecksumAnchorFrameStart) {
      return CompiledChecksumAnchor{.kind = ChecksumAnchorKind::kFrameStart};
    }
    if (anchor == kChecksumAnchorFrameEnd) {
      return CompiledChecksumAnchor{.kind = ChecksumAnchorKind::kFrameEnd};
    }
    if (anchor == kChecksumAnchorBeforeSelf) {
      return CompiledChecksumAnchor{
          .kind = ChecksumAnchorKind::kBeforeSelf,
          .field_id = self_field_id,
      };
    }
    if (anchor == kChecksumAnchorAfterSelf) {
      return CompiledChecksumAnchor{
          .kind = ChecksumAnchorKind::kAfterSelf,
          .field_id = self_field_id,
      };
    }

    if (anchor.size() > kChecksumAnchorStartSuffix.size() &&
        anchor.substr(anchor.size() - kChecksumAnchorStartSuffix.size()) == kChecksumAnchorStartSuffix) {
      const auto it = field_ids.find(anchor.substr(0, anchor.size() - kChecksumAnchorStartSuffix.size()));
      if (it == field_ids.end()) {
        return invalid_argument("Unknown checksum anchor field: " + std::string(anchor));
      }
      return CompiledChecksumAnchor{
          .kind = ChecksumAnchorKind::kFieldStart,
          .field_id = it->second,
      };
    }
    if (anchor.size() > kChecksumAnchorEndSuffix.size() &&
        anchor.substr(anchor.size() - kChecksumAnchorEndSuffix.size()) == kChecksumAnchorEndSuffix) {
      const auto it = field_ids.find(anchor.substr(0, anchor.size() - kChecksumAnchorEndSuffix.size()));
      if (it == field_ids.end()) {
        return invalid_argument("Unknown checksum anchor field: " + std::string(anchor));
      }
      return CompiledChecksumAnchor{
          .kind = ChecksumAnchorKind::kFieldEnd,
          .field_id = it->second,
      };
    }

    return invalid_argument("Unsupported checksum anchor: " + std::string(anchor));
  }

  // Layout compilation intentionally recurses when struct fields reference compiled structs.
  // NOLINTNEXTLINE(misc-no-recursion)
  StatusOr<CompiledMessage> compile_layout(const LayoutDefinitionView& layout) {
    if (layout.fields == nullptr || layout.fields->empty()) {
      return invalid_argument(std::string(layout.kind_name) + " '" + std::string(layout.name) +
                              "' must have at least one field.");
    }
    if (layout.fields->size() > kMaxFieldsPerMessage) {
      return exhausted(std::string(layout.kind_name) + " '" + std::string(layout.name) +
                       "' exceeds the per-message field limit.");
    }

    std::unordered_set<std::string> field_names;
    std::unordered_set<std::string> bit_field_names;
    FieldIdMap field_ids;
    std::vector<CompiledField> compiled_fields;
    std::vector<CompiledBitField> compiled_bit_fields;
    std::vector<PendingChecksum> pending_checksums;
    std::vector<std::byte> dispatch_prefix;
    size_t minimum_size = 0;
    bool collecting_dispatch_prefix = true;

    field_names.reserve(layout.fields->size());
    field_ids.reserve(layout.fields->size());
    compiled_fields.reserve(layout.fields->size());
    for (size_t index = 0; index < layout.fields->size(); ++index) {
      const FieldDefinition& field = (*layout.fields)[index];
      if (field.name.empty()) {
        return invalid_argument("Field name must not be empty in " + std::string(layout.kind_name) + " '" +
                                std::string(layout.name) + "'.");
      }
      if (!field_names.insert(field.name).second) {
        return invalid_argument("Duplicate field name '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                std::string(layout.name) + "'.");
      }

      CompiledField compiled_field;
      compiled_field.id = static_cast<FieldId>(index);
      compiled_field.name = field.name;
      compiled_field.kind = field.kind;
      compiled_field.width_bytes = field.width_bytes;
      compiled_field.byte_order = field.byte_order;
      compiled_field.string_encoding = field.string_encoding;
      compiled_field.fixed_size = field.fixed_size;
      compiled_field.has_expected_unsigned = field.has_expected_unsigned;
      compiled_field.expected_unsigned = field.expected_unsigned;
      compiled_field.enum_values = field.enum_values;

      if (compiled_field.kind == FieldKind::kBytes || compiled_field.kind == FieldKind::kString) {
        if (field.has_expected_unsigned) {
          return invalid_argument(std::string(to_string(compiled_field.kind)) + " field '" + field.name + "' in " +
                                  std::string(layout.kind_name) + " '" + std::string(layout.name) +
                                  "' cannot use 'expect'.");
        }
        if ((field.fixed_size == 0) == field.size_from_field.empty()) {
          return invalid_argument(std::string("Field '") + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                  std::string(layout.name) + "' must declare exactly one of 'size' or 'size_from'.");
        }
        if (!field.size_from_field.empty()) {
          const auto it = field_ids.find(field.size_from_field);
          if (it == field_ids.end()) {
            return invalid_argument("Dynamic field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                    std::string(layout.name) + "' must reference a prior field.");
          }
          const CompiledField& dependency = compiled_fields[it->second];
          if (dependency.kind != FieldKind::kUnsigned && dependency.kind != FieldKind::kEnum) {
            return invalid_argument("Dynamic field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                    std::string(layout.name) + "' must reference an unsigned or enum field.");
          }
          compiled_field.dynamic_size = true;
          compiled_field.size_from_field = dependency.id;
        }
        if (!field.bit_fields.empty()) {
          return invalid_argument("Bitfields are only supported on scalar containers.");
        }
      } else if (compiled_field.kind == FieldKind::kStruct) {
        if (field.referenced_type.empty()) {
          return invalid_argument("Struct field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                  std::string(layout.name) + "' must reference a struct type.");
        }
        const auto enum_it = enum_ids_.find(field.referenced_type);
        if (enum_it != enum_ids_.end()) {
          const EnumDefinition& enum_definition = definition_.enums[enum_it->second];
          compiled_field.kind = FieldKind::kEnum;
          compiled_field.width_bytes = enum_definition.width_bytes;
          compiled_field.byte_order = enum_definition.byte_order;
          compiled_field.enum_values = enum_definition.values;
        } else {
          if (field.has_expected_unsigned) {
            return invalid_argument("Struct field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                    std::string(layout.name) + "' cannot use 'expect'.");
          }
          if (field.fixed_size != 0 || !field.size_from_field.empty()) {
            return invalid_argument("Struct field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                    std::string(layout.name) + "' derives its size from the referenced struct.");
          }
          if (!field.bit_fields.empty()) {
            return invalid_argument("Bitfields are only supported on scalar containers.");
          }
          const auto struct_it = struct_ids_.find(field.referenced_type);
          if (struct_it == struct_ids_.end()) {
            return not_found("Unknown struct type: " + field.referenced_type);
          }
          const auto compiled_struct_id = compile_struct(struct_it->second);
          if (!compiled_struct_id.ok()) {
            return compiled_struct_id.status();
          }
          compiled_field.struct_id = compiled_struct_id.value();
          compiled_field.fixed_size = compiled_structs_[compiled_struct_id.value()].minimum_size();
        }
      } else {
        if (!is_valid_scalar_width(compiled_field.width_bytes)) {
          return invalid_argument("Scalar field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                  std::string(layout.name) + "' has an unsupported width.");
        }
        if (compiled_field.kind == FieldKind::kFloat32 && compiled_field.width_bytes != kFloat32WidthBytes) {
          return invalid_argument("float32 fields must be 4 bytes wide.");
        }
        if (compiled_field.kind == FieldKind::kFloat64 && compiled_field.width_bytes != kFloat64WidthBytes) {
          return invalid_argument("float64 fields must be 8 bytes wide.");
        }
      }

      field_ids.emplace(field.name, compiled_field.id);
      minimum_size += compiled_field.minimum_size_contribution();
      if (collecting_dispatch_prefix && compiled_field.is_scalar() && compiled_field.has_expected_unsigned) {
        append_expected_scalar_bytes(
            &dispatch_prefix, compiled_field.expected_unsigned, compiled_field.width_bytes, compiled_field.byte_order);
      } else {
        collecting_dispatch_prefix = false;
      }

      if (!field.bit_fields.empty()) {
        if (compiled_field.kind != FieldKind::kUnsigned && compiled_field.kind != FieldKind::kSigned &&
            compiled_field.kind != FieldKind::kEnum) {
          return invalid_argument("Bitfields require an unsigned, signed, or enum container field in '" + field.name +
                                  "'.");
        }
        const uint16_t container_bits =
            static_cast<uint16_t>(compiled_field.width_bytes) * static_cast<uint16_t>(kBitsPerByte);
        for (const BitFieldDefinition& bit_field : field.bit_fields) {
          if (bit_field.name.empty()) {
            return invalid_argument("Bitfield names must not be empty.");
          }
          if (!bit_field_names.insert(bit_field.name).second) {
            return invalid_argument("Duplicate bitfield name '" + bit_field.name + "' in " +
                                    std::string(layout.kind_name) + " '" + std::string(layout.name) + "'.");
          }
          if (bit_field.width_bits == 0) {
            return invalid_argument("Bitfield '" + bit_field.name + "' must be at least 1 bit wide.");
          }
          if (bit_field.width_bits > 64) {
            return invalid_argument("Bitfield '" + bit_field.name + "' cannot exceed 64 bits.");
          }
          if (static_cast<uint16_t>(bit_field.offset_bits) + static_cast<uint16_t>(bit_field.width_bits) >
              container_bits) {
            return invalid_argument("Bitfield '" + bit_field.name + "' exceeds the width of container field '" +
                                    field.name + "'.");
          }
          if (compiled_bit_fields.size() >= kMaxBitFieldsPerMessage) {
            return exhausted(std::string(layout.kind_name) + " '" + std::string(layout.name) +
                             "' exceeds the bitfield limit.");
          }
          compiled_bit_fields.push_back(CompiledBitField{
              .id = static_cast<BitFieldId>(compiled_bit_fields.size()),
              .name = bit_field.name,
              .container_field_id = compiled_field.id,
              .shift_bits = bit_field.offset_bits,
              .width_bits = bit_field.width_bits,
              .mask = bit_field.width_bits == 64 ? std::numeric_limits<uint64_t>::max()
                                                 : ((1ULL << bit_field.width_bits) - 1ULL),
              .is_signed = bit_field.is_signed,
              .enum_values = bit_field.enum_values,
          });
        }
      }

      if (field.checksum.has_value()) {
        if (compiled_field.kind != FieldKind::kUnsigned && compiled_field.kind != FieldKind::kEnum) {
          return invalid_argument("Checksum fields must use an unsigned or enum scalar type.");
        }
        const auto checksum_spec = find_checksum_algorithm(field.checksum->algorithm);
        if (!checksum_spec.ok()) {
          return checksum_spec.status();
        }
        if (compiled_field.width_bytes != checksum_spec.value().result_width_bytes) {
          return invalid_argument("Checksum field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                  std::string(layout.name) + "' must match the width of algorithm '" +
                                  checksum_spec.value().name + "'.");
        }
        pending_checksums.push_back(PendingChecksum{
            .field_id = compiled_field.id,
            .spec = checksum_spec.value(),
            .from_anchor = field.checksum->from,
            .to_anchor = field.checksum->to,
        });
      }

      compiled_fields.push_back(std::move(compiled_field));
    }

    std::vector<CompiledChecksum> compiled_checksums;
    compiled_checksums.reserve(pending_checksums.size());
    for (const PendingChecksum& pending : pending_checksums) {
      const auto from_anchor = resolve_checksum_anchor(pending.from_anchor, pending.field_id, field_ids);
      if (!from_anchor.ok()) {
        return from_anchor.status();
      }
      const auto to_anchor = resolve_checksum_anchor(pending.to_anchor, pending.field_id, field_ids);
      if (!to_anchor.ok()) {
        return to_anchor.status();
      }
      compiled_checksums.push_back(CompiledChecksum{
          .field_id = pending.field_id,
          .result_width_bytes = pending.spec.result_width_bytes,
          .function = pending.spec.function,
          .algorithm_name = pending.spec.name,
          .builtin_kind = checksum_builtin_kind(pending.spec.name),
          .from = from_anchor.value(),
          .to = to_anchor.value(),
      });
    }

    return CompiledMessage(std::string(layout.name),
                           std::move(compiled_fields),
                           std::move(compiled_bit_fields),
                           std::move(compiled_checksums),
                           minimum_size,
                           layout.allow_trailing_bytes,
                           std::move(dispatch_prefix));
  }

  uint64_t fingerprint() const {
    XxHash64State hasher;
    hasher.update(definition_.name);
    for (const StructDefinition& struct_definition : definition_.structs) {
      hasher.update(kFingerprintStructToken);
      hasher.update(struct_definition.name);
      for (const FieldDefinition& field : struct_definition.fields) {
        fingerprint_field(&hasher, field);
      }
    }
    for (const EnumDefinition& enum_definition : definition_.enums) {
      fingerprint_enum(&hasher, enum_definition);
    }
    for (const MessageDefinition& message : definition_.messages) {
      hasher.update(kFingerprintMessageToken);
      hasher.update(message.name);
      hasher.update_bool(message.allow_trailing_bytes);
      for (const FieldDefinition& field : message.fields) {
        fingerprint_field(&hasher, field);
      }
    }
    return hasher.digest();
  }

  const ProtocolDefinition& definition_;
  std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> enum_ids_;
  std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> struct_ids_;
  std::vector<CompiledMessage> compiled_structs_;
  std::vector<CompileState> struct_states_;
};

}  // namespace

StatusOr<CompiledProtocol> compile_protocol(const ProtocolDefinition& definition) {
  return ProtocolCompiler(definition).compile();
}

}  // namespace universal_protocol_runtime
