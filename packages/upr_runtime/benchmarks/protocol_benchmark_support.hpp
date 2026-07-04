#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_BENCHMARKS__PROTOCOL_BENCHMARK_SUPPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_BENCHMARKS__PROTOCOL_BENCHMARK_SUPPORT_HPP_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace universal_protocol_runtime::benchmarks {

enum class ProtocolKind {
  KUpr,
  KPackedBinary,
  KProtobuf,
  KFlatbuffers,
};

enum class ScenarioKind {
  KBlobSmall,
  KBlobLarge,
  KMarketData,
};

struct BenchmarkCase {
  ProtocolKind protocol = ProtocolKind::KUpr;
  ScenarioKind scenario = ScenarioKind::KBlobSmall;
};

struct CorpusMetrics {
  size_t message_count = 0;
  size_t stream_bytes = 0;
  double encoded_bytes_per_message = 0.0;
  size_t seed_count = 0;
};

std::span<const BenchmarkCase> benchmark_cases();

std::string_view to_string(ProtocolKind protocol);
std::string_view to_string(ScenarioKind scenario);

CorpusMetrics corpus_metrics(const BenchmarkCase& benchmark_case);
std::function<uint64_t()> make_decode_runner(const BenchmarkCase& benchmark_case);

}  // namespace universal_protocol_runtime::benchmarks

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_BENCHMARKS__PROTOCOL_BENCHMARK_SUPPORT_HPP_
