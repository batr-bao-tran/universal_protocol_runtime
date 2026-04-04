#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_AUTHORING_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__YAML_LOADER_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_AUTHORING_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__YAML_LOADER_HPP_
#include <string>
#include <string_view>

#include "universal_protocol_runtime/pdl/protocol_definition.hpp"
#include "utils/status.hpp"

namespace universal_protocol_runtime {

StatusOr<ProtocolDefinition> load_protocol_definition_from_yaml(std::string_view yaml_text);

StatusOr<ProtocolDefinition> load_protocol_definition_from_file(const std::string& path);

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__UNIVERSAL_PROTOCOL_RUNTIME_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_PDL__YAML_LOADER_HPP_
