#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "universal_protocol_runtime/codegen/bindings_generator.hpp"
#include "universal_protocol_runtime/compiler/schema_compiler.hpp"
#include "universal_protocol_runtime/pdl/yaml_loader.hpp"

namespace upr = universal_protocol_runtime;

namespace {

std::filesystem::path benchmark_artifact_path(std::string_view filename) {
  if (const char* runfiles_dir = std::getenv("RUNFILES_DIR")) {
    return std::filesystem::path(runfiles_dir) / "_main" / "packages" / "upr_runtime" / "benchmarks" / filename;
  }
  if (const char* test_srcdir = std::getenv("TEST_SRCDIR")) {
    return std::filesystem::path(test_srcdir) / "_main" / "packages" / "upr_runtime" / "benchmarks" / filename;
  }
  return std::filesystem::path("packages") / "upr_runtime" / "benchmarks" / filename;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.is_open()) << path;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

TEST(BenchmarkGeneratedHeaderTest, MatchesGeneratedBindingsFromSchema) {
  const std::filesystem::path schema_path = benchmark_artifact_path("benchmark_protocol.upr");
  const std::filesystem::path header_path = benchmark_artifact_path("upr_benchmark_generated.hpp");

  upr::StatusOr<upr::ProtocolDefinition> definition = upr::load_protocol_definition_from_file(schema_path.string());
  ASSERT_TRUE(definition.ok()) << definition.status().message();

  upr::StatusOr<upr::CompiledProtocol> compiled = upr::compile_protocol(definition.value());
  ASSERT_TRUE(compiled.ok()) << compiled.status().message();

  upr::CppBindingsOptions options;
  options.namespace_prefix = "universal_protocol_runtime";
  options.protocol_namespace = "benchmarks::generated";
  options.header_guard = "UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_BENCHMARKS__UPR_BENCHMARK_GENERATED_HPP_";

  upr::StatusOr<std::string> generated = upr::generate_cpp_bindings_header(compiled.value(), options);
  ASSERT_TRUE(generated.ok()) << generated.status().message();
  EXPECT_EQ(generated.value(), read_file(header_path));
}

}  // namespace
