#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "universal_protocol_runtime/codegen/bindings_generator.hpp"
#include "universal_protocol_runtime/compiler/schema_compiler.hpp"
#include "universal_protocol_runtime/pdl/yaml_loader.hpp"

namespace upr = universal_protocol_runtime;

namespace {

int fail(std::string_view message) {
  std::cerr << message << '\n';
  return 1;
}

const char* require_value(int argc, char** argv, int* index, std::string_view option) {
  if (*index + 1 >= argc) {
    std::cerr << "Missing value for " << option << '\n';
    std::exit(1);
  }
  *index += 1;
  return argv[*index];
}

}  // namespace

int main(int argc, char** argv) {
  std::string input_path;
  std::string output_path;
  upr::CppBindingsOptions options;

  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--input") {
      input_path = require_value(argc, argv, &index, argument);
    } else if (argument == "--output") {
      output_path = require_value(argc, argv, &index, argument);
    } else if (argument == "--namespace-prefix") {
      options.namespace_prefix = require_value(argc, argv, &index, argument);
    } else if (argument == "--protocol-namespace") {
      options.protocol_namespace = require_value(argc, argv, &index, argument);
    } else if (argument == "--header-guard") {
      options.header_guard = require_value(argc, argv, &index, argument);
    } else {
      return fail("Unknown argument: " + std::string(argument));
    }
  }

  if (input_path.empty()) {
    return fail("Missing required --input path.");
  }
  if (output_path.empty()) {
    return fail("Missing required --output path.");
  }

  upr::StatusOr<upr::ProtocolDefinition> definition = upr::load_protocol_definition_from_file(input_path);
  if (!definition.ok()) {
    return fail(definition.status().message());
  }

  upr::StatusOr<upr::CompiledProtocol> compiled = upr::compile_protocol(definition.value());
  if (!compiled.ok()) {
    return fail(compiled.status().message());
  }

  upr::StatusOr<std::string> generated = upr::generate_cpp_bindings_header(compiled.value(), options);
  if (!generated.ok()) {
    return fail(generated.status().message());
  }

  std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return fail("Failed to open output file: " + output_path);
  }
  out << generated.value();
  out.close();
  if (!out) {
    return fail("Failed to write output file: " + output_path);
  }

  return 0;
}
