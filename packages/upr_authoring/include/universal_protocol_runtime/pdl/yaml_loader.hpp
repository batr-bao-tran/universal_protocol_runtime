#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_AUTHORING_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__YAML_LOADER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_AUTHORING_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__YAML_LOADER_HPP_
#include <string>
#include <string_view>

#include "universal_protocol_runtime/pdl/protocol_definition.hpp"
#include "utils/status.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Controls optional schema-loading behavior.
 */
struct SchemaLoadOptions {
  bool resolve_imports = true;
};

/**
 * @brief Loads a protocol definition from YAML text.
 * @param yaml_text YAML schema source text.
 * @return Parsed protocol definition or an error status.
 */
StatusOr<ProtocolDefinition> load_protocol_definition_from_yaml(std::string_view yaml_text);
/**
 * @brief Loads a protocol definition from UPR text.
 * @param upr_text UPR schema source text.
 * @return Parsed protocol definition or an error status.
 */
StatusOr<ProtocolDefinition> load_protocol_definition_from_upr(std::string_view upr_text);
/**
 * @brief Loads a protocol definition using an optional format hint.
 * @param schema_text Schema source text.
 * @param format_hint Optional format hint such as `yaml` or `upr`.
 * @return Parsed protocol definition or an error status.
 */
StatusOr<ProtocolDefinition> load_protocol_definition(std::string_view schema_text, std::string_view format_hint = {});

/**
 * @brief Loads a protocol definition from a file path.
 * @param path Schema file path.
 * @return Parsed protocol definition or an error status.
 */
StatusOr<ProtocolDefinition> load_protocol_definition_from_file(const std::string& path);
/**
 * @brief Loads a protocol definition from a file path with explicit options.
 * @param path Schema file path.
 * @param options Schema-loading options.
 * @return Parsed protocol definition or an error status.
 */
StatusOr<ProtocolDefinition> load_protocol_definition_from_file(const std::string& path,
                                                                const SchemaLoadOptions& options);

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__YAML_LOADER_HPP_
