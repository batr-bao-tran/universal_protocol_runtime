#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CODEGEN_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CODEGEN__BINDINGS_GENERATOR_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CODEGEN_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CODEGEN__BINDINGS_GENERATOR_HPP_

#include <string>

#include "universal_protocol_runtime/compiler/compiled_protocol.hpp"
#include "utils/status.hpp"

namespace universal_protocol_runtime {

/**
 * @brief Configuration for generated C++ bindings.
 */
struct CppBindingsOptions {
  std::string namespace_prefix = "upr_generated";
  std::string protocol_namespace;
  std::string header_guard;
};

/**
 * @brief Configuration for generated Python bindings.
 */
struct PythonBindingsOptions {
  std::string module_name;
};

/**
 * @brief Configuration for generated TypeScript bindings.
 */
struct TypescriptBindingsOptions {
  // Module specifier the generated code imports the runtime from. Defaults to
  // the published npm package name.
  std::string runtime_import = "universal-protocol-runtime";
};

/**
 * @brief Generates a C++ header with bindings for a compiled protocol.
 * @param protocol Compiled protocol metadata to emit.
 * @param options Code generation options for namespaces and guards.
 * @return Generated header text on success.
 */
StatusOr<std::string> generate_cpp_bindings_header(const CompiledProtocol& protocol,
                                                   const CppBindingsOptions& options = {});

/**
 * @brief Generates a Python module with bindings for a compiled protocol.
 * @param protocol Compiled protocol metadata to emit.
 * @param options Code generation options for the Python module.
 * @return Generated Python source text on success.
 */
StatusOr<std::string> generate_python_bindings_module(const CompiledProtocol& protocol,
                                                      const PythonBindingsOptions& options = {});

/**
 * @brief Generates a TypeScript module with bindings for a compiled protocol.
 * @param protocol Compiled protocol metadata to emit.
 * @param options Code generation options for the TypeScript module.
 * @return Generated TypeScript source text on success.
 */
StatusOr<std::string> generate_typescript_bindings_module(const CompiledProtocol& protocol,
                                                          const TypescriptBindingsOptions& options = {});

}  // namespace universal_protocol_runtime

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_CODEGEN_INCLUDE_UNIVERSAL_PROTOCOL_RUNTIME_CODEGEN__BINDINGS_GENERATOR_HPP_
