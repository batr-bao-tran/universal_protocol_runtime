#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CODEGEN_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CODEGEN__BINDINGS_GENERATOR_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CODEGEN_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CODEGEN__BINDINGS_GENERATOR_HPP_

#include <string>

#include "universal_protocol_runtime/compiler/compiled_protocol.hpp"
#include "utils/status.hpp"

namespace universal_protocol_runtime {

struct CppBindingsOptions {
  std::string namespace_prefix = "upr_generated";
  std::string protocol_namespace;
  std::string header_guard;
};

struct PythonBindingsOptions {
  std::string module_name;
};

StatusOr<std::string> generate_cpp_bindings_header(const CompiledProtocol& protocol,
                                                   const CppBindingsOptions& options = {});

StatusOr<std::string> generate_python_bindings_module(const CompiledProtocol& protocol,
                                                      const PythonBindingsOptions& options = {});

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CODEGEN_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CODEGEN__BINDINGS_GENERATOR_HPP_
