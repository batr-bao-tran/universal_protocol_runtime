#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "protocol_benchmark_support.hpp"

namespace uprb = universal_protocol_runtime::benchmarks;

namespace {

constexpr int kBenchmarkRepetitions = 12;
constexpr double kMinBenchmarkTimeSeconds = 0.75;
constexpr double kMinWarmupTimeSeconds = 0.20;

double compute_p90(const std::vector<double>& samples) {
  if (samples.empty()) {
    return 0.0;
  }
  std::vector<double> sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  const size_t index = (sorted.size() - 1U) * 9U / 10U;
  return sorted[index];
}

void configure_benchmark(benchmark::Benchmark* benchmark_rule) {
  benchmark_rule->Unit(benchmark::kMicrosecond)
      ->MeasureProcessCPUTime()
      ->MinTime(kMinBenchmarkTimeSeconds)
      ->MinWarmUpTime(kMinWarmupTimeSeconds)
      ->Repetitions(kBenchmarkRepetitions)
      ->ReportAggregatesOnly(true)
      ->ComputeStatistics("p90", compute_p90, benchmark::StatisticUnit::kTime);
}

}  // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);

  for (const uprb::BenchmarkCase& benchmark_case : uprb::benchmark_cases()) {
    const std::string name = "decode_stream/" + std::string(uprb::to_string(benchmark_case.protocol)) + "/" +
                             std::string(uprb::to_string(benchmark_case.scenario));
    auto runner = uprb::make_decode_runner(benchmark_case);
    const uprb::CorpusMetrics metrics = uprb::corpus_metrics(benchmark_case);

    benchmark::Benchmark* benchmark_rule =
        benchmark::RegisterBenchmark(name, [runner = std::move(runner), metrics](benchmark::State& state) {
          uint64_t sink = 0;
          for (auto _ : state) {
            sink ^= runner();
          }
          benchmark::DoNotOptimize(sink);
          state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(metrics.message_count));
          state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(metrics.stream_bytes));
          state.counters["encoded_bytes_per_message"] = metrics.encoded_bytes_per_message;
          state.counters["seed_count"] = static_cast<double>(metrics.seed_count);
        });
    configure_benchmark(benchmark_rule);
  }

  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
