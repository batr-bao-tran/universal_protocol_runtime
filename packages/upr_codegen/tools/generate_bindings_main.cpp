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

constexpr std::string_view kUsage =
    "usage: upr-gen --lang {cpp,python,typescript} --input <schema.upr> --output <path> [options]\n"
    "\n"
    "options:\n"
    "  --lang {cpp,python,typescript}  Target language (default: cpp). 'ts' and 'js' alias typescript.\n"
    "  --input <path>              Schema definition (.upr or YAML). Required.\n"
    "  --output <path>             Output file path. Required.\n"
    "  --namespace-prefix <name>   C++ outer namespace (default: upr_generated).\n"
    "  --protocol-namespace <name> C++ inner namespace (default: derived from protocol).\n"
    "  --header-guard <macro>      C++ include guard (default: derived).\n"
    "  --module-name <name>        Python module name (default: derived from protocol).\n"
    "  --runtime-import <spec>     TypeScript runtime import (default: universal-protocol-runtime).\n"
    "  -h, --help                  Show this help.\n";

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
  std::string language = "cpp";
  upr::CppBindingsOptions cpp_options;
  upr::PythonBindingsOptions python_options;
  upr::TypescriptBindingsOptions typescript_options;

  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--input") {
      input_path = require_value(argc, argv, &index, argument);
    } else if (argument == "--output") {
      output_path = require_value(argc, argv, &index, argument);
    } else if (argument == "--lang" || argument == "--language") {
      language = require_value(argc, argv, &index, argument);
    } else if (argument == "--namespace-prefix") {
      cpp_options.namespace_prefix = require_value(argc, argv, &index, argument);
    } else if (argument == "--protocol-namespace") {
      cpp_options.protocol_namespace = require_value(argc, argv, &index, argument);
    } else if (argument == "--header-guard") {
      cpp_options.header_guard = require_value(argc, argv, &index, argument);
    } else if (argument == "--module-name") {
      python_options.module_name = require_value(argc, argv, &index, argument);
    } else if (argument == "--runtime-import") {
      typescript_options.runtime_import = require_value(argc, argv, &index, argument);
    } else if (argument == "-h" || argument == "--help") {
      std::cout << kUsage;
      return 0;
    } else {
      std::cerr << kUsage;
      return fail("Unknown argument: " + std::string(argument));
    }
  }

  if (language == "ts" || language == "js" || language == "javascript") {
    language = "typescript";
  }
  if (language != "cpp" && language != "python" && language != "typescript") {
    std::cerr << kUsage;
    return fail("Unsupported --lang value: " + language + " (expected cpp, python or typescript)");
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

  upr::StatusOr<std::string> generated = upr::generate_cpp_bindings_header(compiled.value(), cpp_options);
  if (language == "python") {
    generated = upr::generate_python_bindings_module(compiled.value(), python_options);
  } else if (language == "typescript") {
    generated = upr::generate_typescript_bindings_module(compiled.value(), typescript_options);
  }
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
