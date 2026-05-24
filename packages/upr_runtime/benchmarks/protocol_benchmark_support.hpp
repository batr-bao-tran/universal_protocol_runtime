#ifndef UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_BENCHMARKS__PROTOCOL_BENCHMARK_SUPPORT_HPP_
#define UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_BENCHMARKS__PROTOCOL_BENCHMARK_SUPPORT_HPP_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace universal_protocol_runtime::benchmarks {

/**
 * @brief Decode protocol implementations compared by the benchmark suite.
 */
enum class ProtocolKind {
  KUprReflective,
  KUprResolvedIds,
  KUprStaticSchema,
  KPackedBinary,
  KProtobuf,
  KFlatbuffers,
};

/**
 * @brief Encode protocol implementations compared by the benchmark suite.
 */
enum class EncodeProtocolKind {
  KUprRuntimeSchema,
  KUprStaticSchema,
  KPackedBinary,
  KProtobuf,
  KFlatbuffers,
};

/**
 * @brief Benchmark data scenarios used for encode and decode runs.
 */
enum class ScenarioKind {
  KBlobSmall,
  KBlobLarge,
  KMarketData,
};

/**
 * @brief One decode benchmark configuration.
 */
struct BenchmarkCase {
  ProtocolKind protocol = ProtocolKind::KUprReflective;
  ScenarioKind scenario = ScenarioKind::KBlobSmall;
};

/**
 * @brief One encode benchmark configuration.
 */
struct EncodeBenchmarkCase {
  EncodeProtocolKind protocol = EncodeProtocolKind::KUprRuntimeSchema;
  ScenarioKind scenario = ScenarioKind::KBlobSmall;
};

/**
 * @brief Aggregate metrics describing the generated benchmark corpus.
 */
struct CorpusMetrics {
  size_t message_count = 0;
  size_t stream_bytes = 0;
  double encoded_bytes_per_message = 0.0;
  size_t seed_count = 0;
};

/**
 * @brief Returns the decode benchmark matrix.
 * @return Span of decode benchmark cases.
 */
std::span<const BenchmarkCase> benchmark_cases();
/**
 * @brief Returns the encode benchmark matrix.
 * @return Span of encode benchmark cases.
 */
std::span<const EncodeBenchmarkCase> encode_benchmark_cases();

/**
 * @brief Converts a decode protocol kind to a stable string name.
 * @param protocol Decode protocol kind to stringify.
 * @return String representation of the protocol kind.
 */
std::string_view to_string(ProtocolKind protocol);
/**
 * @brief Converts an encode protocol kind to a stable string name.
 * @param protocol Encode protocol kind to stringify.
 * @return String representation of the protocol kind.
 */
std::string_view to_string(EncodeProtocolKind protocol);
/**
 * @brief Converts a scenario kind to a stable string name.
 * @param scenario Scenario kind to stringify.
 * @return String representation of the scenario kind.
 */
std::string_view to_string(ScenarioKind scenario);

/**
 * @brief Returns corpus metrics for a decode benchmark case.
 * @param benchmark_case Decode benchmark configuration.
 * @return Corpus metrics for the selected case.
 */
CorpusMetrics corpus_metrics(const BenchmarkCase& benchmark_case);
/**
 * @brief Returns corpus metrics for an encode benchmark case.
 * @param benchmark_case Encode benchmark configuration.
 * @return Corpus metrics for the selected case.
 */
CorpusMetrics corpus_metrics(const EncodeBenchmarkCase& benchmark_case);
/**
 * @brief Builds a callable that executes one decode benchmark iteration.
 * @param benchmark_case Decode benchmark configuration.
 * @return Callable producing a benchmark checksum or accumulator.
 */
std::function<uint64_t()> make_decode_runner(const BenchmarkCase& benchmark_case);
/**
 * @brief Builds a callable that executes one encode benchmark iteration.
 * @param benchmark_case Encode benchmark configuration.
 * @return Callable producing a benchmark checksum or accumulator.
 */
std::function<uint64_t()> make_encode_runner(const EncodeBenchmarkCase& benchmark_case);

}  // namespace universal_protocol_runtime::benchmarks

#endif  // UNIVERSAL_PROTOCOL_RUNTIME__PACKAGES_UPR_RUNTIME_BENCHMARKS__PROTOCOL_BENCHMARK_SUPPORT_HPP_
