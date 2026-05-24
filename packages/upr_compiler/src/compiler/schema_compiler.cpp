#include "universal_protocol_runtime/compiler/schema_compiler.hpp"

#include <algorithm>
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
constexpr uint64_t kMaxDenseVariantLookupSpan = 256U;

size_t align_up(size_t value, size_t alignment) {
  if (alignment <= 1U) {
    return value;
  }
  const size_t remainder = value % alignment;
  return remainder == 0U ? value : (value + (alignment - remainder));
}

}  // namespace

CompiledMessage::CompiledMessage(std::string name,
                                 std::vector<CompiledField> fields,
                                 std::vector<CompiledBitField> bit_fields,
                                 std::vector<CompiledChecksum> checksums,
                                 std::vector<CompiledValidationRule> validations,
                                 size_t minimum_size,
                                 bool has_fixed_size,
                                 bool allow_trailing_bytes,
                                 std::vector<std::byte> dispatch_prefix)
    : name_(std::move(name)),
      fields_(std::move(fields)),
      bit_fields_(std::move(bit_fields)),
      checksums_(std::move(checksums)),
      validations_(std::move(validations)),
      minimum_size_(minimum_size),
      has_fixed_size_(has_fixed_size),
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

CompiledMessage::CompiledMessage(std::string name,
                                 std::vector<CompiledField> fields,
                                 std::vector<CompiledBitField> bit_fields,
                                 std::vector<CompiledChecksum> checksums,
                                 std::vector<CompiledValidationRule> validations,
                                 size_t minimum_size,
                                 bool allow_trailing_bytes,
                                 std::vector<std::byte> dispatch_prefix)
    : CompiledMessage(std::move(name),
                      std::move(fields),
                      std::move(bit_fields),
                      std::move(checksums),
                      std::move(validations),
                      minimum_size,
                      true,
                      allow_trailing_bytes,
                      std::move(dispatch_prefix)) {}

CompiledMessage::CompiledMessage(std::string name,
                                 std::vector<CompiledField> fields,
                                 std::vector<CompiledBitField> bit_fields,
                                 std::vector<CompiledChecksum> checksums,
                                 size_t minimum_size,
                                 bool has_fixed_size,
                                 bool allow_trailing_bytes,
                                 std::vector<std::byte> dispatch_prefix)
    : CompiledMessage(std::move(name),
                      std::move(fields),
                      std::move(bit_fields),
                      std::move(checksums),
                      {},
                      minimum_size,
                      has_fixed_size,
                      allow_trailing_bytes,
                      std::move(dispatch_prefix)) {}

CompiledMessage::CompiledMessage(std::string name,
                                 std::vector<CompiledField> fields,
                                 std::vector<CompiledBitField> bit_fields,
                                 std::vector<CompiledChecksum> checksums,
                                 size_t minimum_size,
                                 bool allow_trailing_bytes,
                                 std::vector<std::byte> dispatch_prefix)
    : CompiledMessage(std::move(name),
                      std::move(fields),
                      std::move(bit_fields),
                      std::move(checksums),
                      {},
                      minimum_size,
                      true,
                      allow_trailing_bytes,
                      std::move(dispatch_prefix)) {}

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
  hasher->update_integral(field.alignment);
  hasher->update_bool(field.is_reserved);
  hasher->update_integral(field.reserved_fill_byte);
  hasher->update_integral(field.fixed_count);
  hasher->update(field.count_from_field);
  hasher->update(field.tag_from_field);
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
  hasher->update_bool(field.condition.has_value());
  if (field.condition.has_value()) {
    hasher->update(field.condition->field);
    hasher->update_integral(field.condition->equals_unsigned);
  }
  hasher->update_bool(field.presence.has_value());
  if (field.presence.has_value()) {
    hasher->update(field.presence->field);
    hasher->update_integral(field.presence->bit_index);
  }
  for (const VariantCaseDefinition& variant_case : field.variant_cases) {
    hasher->update_integral(variant_case.tag_value);
    hasher->update(variant_case.referenced_type);
  }
}

void fingerprint_validation_rule(XxHash64State* hasher, const ValidationRuleDefinition& rule) {
  hasher->update(rule.field);
  hasher->update_integral(rule.op);
  hasher->update(rule.other_field);
  hasher->update_bool(rule.compare_to_field);
  hasher->update_integral(rule.value);
  hasher->update_integral(rule.multiplier);
  hasher->update_bool(rule.when.has_value());
  if (rule.when.has_value()) {
    hasher->update(rule.when->field);
    hasher->update_integral(rule.when->equals_unsigned);
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
  const std::vector<ValidationRuleDefinition>* validations = nullptr;
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

StatusOr<CompiledValidationOperator> compile_validation_operator(ValidationOperator op) {
  switch (op) {
    case ValidationOperator::kEq:
      return CompiledValidationOperator::kEq;
    case ValidationOperator::kNe:
      return CompiledValidationOperator::kNe;
    case ValidationOperator::kLt:
      return CompiledValidationOperator::kLt;
    case ValidationOperator::kLe:
      return CompiledValidationOperator::kLe;
    case ValidationOperator::kGt:
      return CompiledValidationOperator::kGt;
    case ValidationOperator::kGe:
      return CompiledValidationOperator::kGe;
  }
  return invalid_argument("Unsupported validation operator.");
}

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
          .validations = &message.validations,
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
        .validations = &struct_definition.validations,
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
    std::vector<CompiledValidationRule> compiled_validations;
    std::vector<std::byte> dispatch_prefix;
    size_t minimum_size = 0;
    bool has_fixed_size = true;
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
      compiled_field.alignment = field.alignment == 0 ? 1U : field.alignment;
      compiled_field.is_reserved = field.is_reserved;
      compiled_field.reserved_fill_byte = field.reserved_fill_byte;
      compiled_field.has_expected_unsigned = field.has_expected_unsigned;
      compiled_field.expected_unsigned = field.expected_unsigned;
      compiled_field.enum_values = field.enum_values;

      if ((compiled_field.alignment & (compiled_field.alignment - 1U)) != 0U) {
        return invalid_argument("Field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                std::string(layout.name) + "' must use a power-of-two alignment.");
      }

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
        if (field.fixed_count != 0 || !field.count_from_field.empty()) {
          return invalid_argument("Byte and string fields use 'size'/'size_from', not collection counts.");
        }
        if (!field.tag_from_field.empty() || !field.variant_cases.empty()) {
          return invalid_argument("Byte and string fields do not support variant declarations.");
        }
        if (compiled_field.is_reserved) {
          if (compiled_field.kind != FieldKind::kBytes || compiled_field.dynamic_size ||
              compiled_field.fixed_size == 0U) {
            return invalid_argument("Reserved fields must be fixed-size byte ranges.");
          }
          if (field.checksum.has_value() || field.condition.has_value() || field.presence.has_value()) {
            return invalid_argument("Reserved fields cannot use checksum, conditional, or presence modifiers.");
          }
        }
        if (compiled_field.dynamic_size) {
          has_fixed_size = false;
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
          if (!compiled_structs_[compiled_struct_id.value()].has_fixed_size()) {
            has_fixed_size = false;
          }
        }
        if (field.fixed_count != 0 || !field.count_from_field.empty()) {
          return invalid_argument("Struct fields do not support collection counts; use a repeated collection field.");
        }
        if (!field.tag_from_field.empty() || !field.variant_cases.empty()) {
          return invalid_argument("Struct fields do not support variant declarations.");
        }
      } else if (compiled_field.kind == FieldKind::kCollection) {
        if (field.referenced_type.empty()) {
          return invalid_argument("Collection field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                  std::string(layout.name) + "' must reference a struct type.");
        }
        if ((field.fixed_count == 0) == field.count_from_field.empty()) {
          return invalid_argument("Collection field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                  std::string(layout.name) +
                                  "' must declare exactly one of a fixed count or 'count_from'.");
        }
        if (field.has_expected_unsigned || field.checksum.has_value() || !field.bit_fields.empty()) {
          return invalid_argument("Collection field '" + field.name + "' cannot use scalar-only modifiers.");
        }
        if (!field.tag_from_field.empty() || !field.variant_cases.empty()) {
          return invalid_argument("Collection field '" + field.name + "' cannot declare variant cases.");
        }
        const auto struct_it = struct_ids_.find(field.referenced_type);
        if (struct_it == struct_ids_.end()) {
          return not_found("Unknown struct type: " + field.referenced_type);
        }
        const auto compiled_struct_id = compile_struct(struct_it->second);
        if (!compiled_struct_id.ok()) {
          return compiled_struct_id.status();
        }
        const CompiledMessage& element_layout = compiled_structs_[compiled_struct_id.value()];
        compiled_field.struct_id = compiled_struct_id.value();
        compiled_field.element_minimum_size = element_layout.minimum_size();
        compiled_field.fixed_count = field.fixed_count;
        if (!field.count_from_field.empty()) {
          const auto it = field_ids.find(field.count_from_field);
          if (it == field_ids.end()) {
            return invalid_argument("Collection field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                    std::string(layout.name) + "' must reference a prior count field.");
          }
          const CompiledField& dependency = compiled_fields[it->second];
          if (dependency.kind != FieldKind::kUnsigned && dependency.kind != FieldKind::kEnum) {
            return invalid_argument("Collection field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                    std::string(layout.name) +
                                    "' must use an unsigned or enum field as its count source.");
          }
          compiled_field.dynamic_count = true;
          compiled_field.count_from_field = dependency.id;
          has_fixed_size = false;
          compiled_field.fixed_size = 0;
        } else {
          compiled_field.fixed_size = compiled_field.fixed_count * compiled_field.element_minimum_size;
          if (!element_layout.has_fixed_size()) {
            has_fixed_size = false;
          }
        }
      } else if (compiled_field.kind == FieldKind::kVariant) {
        if (field.tag_from_field.empty() || field.variant_cases.empty()) {
          return invalid_argument("Variant field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                  std::string(layout.name) + "' requires 'tag_from' and at least one case.");
        }
        if (field.has_expected_unsigned || field.checksum.has_value() || !field.bit_fields.empty() ||
            field.fixed_size != 0 || !field.size_from_field.empty() || field.fixed_count != 0 ||
            !field.count_from_field.empty()) {
          return invalid_argument("Variant field '" + field.name + "' cannot use unrelated field modifiers.");
        }
        const auto tag_it = field_ids.find(field.tag_from_field);
        if (tag_it == field_ids.end()) {
          return invalid_argument("Variant field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                  std::string(layout.name) + "' must reference a prior tag field.");
        }
        const CompiledField& tag_field = compiled_fields[tag_it->second];
        if (tag_field.kind != FieldKind::kUnsigned && tag_field.kind != FieldKind::kEnum) {
          return invalid_argument("Variant field '" + field.name + "' must use an unsigned or enum tag field.");
        }
        compiled_field.tag_from_field = tag_field.id;
        bool first_case = true;
        bool all_cases_fixed = true;
        size_t first_fixed_size = 0;
        for (const VariantCaseDefinition& variant_case : field.variant_cases) {
          const auto struct_it = struct_ids_.find(variant_case.referenced_type);
          if (struct_it == struct_ids_.end()) {
            return not_found("Unknown struct type: " + variant_case.referenced_type);
          }
          const auto compiled_struct_id = compile_struct(struct_it->second);
          if (!compiled_struct_id.ok()) {
            return compiled_struct_id.status();
          }
          const CompiledMessage& case_layout = compiled_structs_[compiled_struct_id.value()];
          compiled_field.variant_cases.push_back(CompiledVariantCase{
              .tag_value = variant_case.tag_value,
              .struct_id = compiled_struct_id.value(),
          });
          if (first_case || case_layout.minimum_size() < compiled_field.fixed_size) {
            compiled_field.fixed_size = case_layout.minimum_size();
          }
          if (first_case) {
            first_fixed_size = case_layout.minimum_size();
            first_case = false;
          }
          if (!case_layout.has_fixed_size() || case_layout.minimum_size() != first_fixed_size) {
            all_cases_fixed = false;
          }
        }
        std::sort(compiled_field.variant_cases.begin(),
                  compiled_field.variant_cases.end(),
                  [](const CompiledVariantCase& lhs, const CompiledVariantCase& rhs) {
                    return lhs.tag_value < rhs.tag_value;
                  });
        if (!compiled_field.variant_cases.empty()) {
          const uint64_t min_tag = compiled_field.variant_cases.front().tag_value;
          const uint64_t max_tag = compiled_field.variant_cases.back().tag_value;
          const uint64_t span = max_tag - min_tag + 1U;
          if (span <= kMaxDenseVariantLookupSpan) {
            compiled_field.variant_lookup_base = min_tag;
            compiled_field.variant_lookup_indices.assign(static_cast<size_t>(span),
                                                         CompiledField::kVariantLookupMissing);
            for (size_t case_index = 0; case_index < compiled_field.variant_cases.size(); ++case_index) {
              compiled_field.variant_lookup_indices[static_cast<size_t>(
                  compiled_field.variant_cases[case_index].tag_value - min_tag)] = static_cast<uint32_t>(case_index);
            }
          }
        }
        if (!all_cases_fixed) {
          has_fixed_size = false;
        }
      } else {
        if (compiled_field.is_reserved) {
          return invalid_argument("Reserved fill semantics are only supported on byte fields.");
        }
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
        if (field.fixed_count != 0 || !field.count_from_field.empty()) {
          return invalid_argument("Scalar field '" + field.name + "' cannot declare collection counts.");
        }
        if (!field.tag_from_field.empty() || !field.variant_cases.empty()) {
          return invalid_argument("Scalar field '" + field.name + "' cannot declare variant cases.");
        }
      }

      if (field.condition.has_value()) {
        const auto it = field_ids.find(field.condition->field);
        if (it == field_ids.end()) {
          return invalid_argument("Conditional field '" + field.name + "' in " + std::string(layout.kind_name) + " '" +
                                  std::string(layout.name) + "' must reference a prior condition field.");
        }
        const CompiledField& dependency = compiled_fields[it->second];
        if (dependency.kind != FieldKind::kUnsigned && dependency.kind != FieldKind::kEnum) {
          return invalid_argument("Conditional field '" + field.name + "' must depend on an unsigned or enum field.");
        }
        compiled_field.has_condition = true;
        compiled_field.condition_field = dependency.id;
        compiled_field.condition_equals = field.condition->equals_unsigned;
        has_fixed_size = false;
      }
      if (field.presence.has_value()) {
        const auto it = field_ids.find(field.presence->field);
        if (it == field_ids.end()) {
          return invalid_argument("Presence-gated field '" + field.name + "' in " + std::string(layout.kind_name) +
                                  " '" + std::string(layout.name) + "' must reference a prior presence field.");
        }
        const CompiledField& dependency = compiled_fields[it->second];
        if (dependency.kind != FieldKind::kUnsigned && dependency.kind != FieldKind::kEnum) {
          return invalid_argument("Presence-gated field '" + field.name +
                                  "' must depend on an unsigned or enum field.");
        }
        const uint16_t dependency_bits =
            static_cast<uint16_t>(dependency.width_bytes) * static_cast<uint16_t>(kBitsPerByte);
        if (field.presence->bit_index >= dependency_bits) {
          return invalid_argument("Presence bit for field '" + field.name + "' exceeds the width of field '" +
                                  field.presence->field + "'.");
        }
        compiled_field.has_presence = true;
        compiled_field.presence_field = dependency.id;
        compiled_field.presence_bit = field.presence->bit_index;
        has_fixed_size = false;
      }

      field_ids.emplace(field.name, compiled_field.id);
      minimum_size = align_up(minimum_size, compiled_field.alignment);
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

    if (layout.validations != nullptr) {
      compiled_validations.reserve(layout.validations->size());
      for (const ValidationRuleDefinition& rule : *layout.validations) {
        const auto left_it = field_ids.find(rule.field);
        if (left_it == field_ids.end()) {
          return invalid_argument("Validation in " + std::string(layout.kind_name) + " '" + std::string(layout.name) +
                                  "' references unknown field '" + rule.field + "'.");
        }
        const CompiledField& left_field = compiled_fields[left_it->second];
        if (left_field.kind != FieldKind::kUnsigned && left_field.kind != FieldKind::kEnum) {
          return invalid_argument("Validation field '" + rule.field + "' must be unsigned or enum typed.");
        }
        if (rule.multiplier == 0U) {
          return invalid_argument("Validation multiplier must be non-zero.");
        }
        const auto compiled_op = compile_validation_operator(rule.op);
        if (!compiled_op.ok()) {
          return compiled_op.status();
        }
        CompiledValidationRule compiled_rule;
        compiled_rule.field_id = left_field.id;
        compiled_rule.op = compiled_op.value();
        compiled_rule.multiplier = rule.multiplier;
        if (rule.compare_to_field) {
          const auto right_it = field_ids.find(rule.other_field);
          if (right_it == field_ids.end()) {
            return invalid_argument("Validation in " + std::string(layout.kind_name) + " '" + std::string(layout.name) +
                                    "' references unknown field '" + rule.other_field + "'.");
          }
          const CompiledField& right_field = compiled_fields[right_it->second];
          if (right_field.kind != FieldKind::kUnsigned && right_field.kind != FieldKind::kEnum) {
            return invalid_argument("Validation comparison field '" + rule.other_field +
                                    "' must be unsigned or enum typed.");
          }
          compiled_rule.compare_to_field = true;
          compiled_rule.other_field_id = right_field.id;
        } else {
          compiled_rule.value = rule.value;
        }
        if (rule.when.has_value()) {
          const auto when_it = field_ids.find(rule.when->field);
          if (when_it == field_ids.end()) {
            return invalid_argument("Validation condition in " + std::string(layout.kind_name) + " '" +
                                    std::string(layout.name) + "' references unknown field '" + rule.when->field +
                                    "'.");
          }
          const CompiledField& when_field = compiled_fields[when_it->second];
          if (when_field.kind != FieldKind::kUnsigned && when_field.kind != FieldKind::kEnum) {
            return invalid_argument("Validation condition field '" + rule.when->field +
                                    "' must be unsigned or enum typed.");
          }
          compiled_rule.has_when = true;
          compiled_rule.when_field_id = when_field.id;
          compiled_rule.when_equals = rule.when->equals_unsigned;
        }
        compiled_validations.push_back(compiled_rule);
      }
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
                           std::move(compiled_validations),
                           minimum_size,
                           has_fixed_size,
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
      for (const ValidationRuleDefinition& validation : struct_definition.validations) {
        fingerprint_validation_rule(&hasher, validation);
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
      for (const ValidationRuleDefinition& validation : message.validations) {
        fingerprint_validation_rule(&hasher, validation);
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
