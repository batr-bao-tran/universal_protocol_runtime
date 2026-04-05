#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "protocol_benchmark_support.hpp"
#include "universal_protocol_runtime/decoder/direct_decode_support.hpp"

namespace uprb = universal_protocol_runtime::benchmarks;
namespace upr = universal_protocol_runtime;

namespace {

constexpr int kBenchmarkRepetitions = 12;
constexpr double kMinBenchmarkTimeSeconds = 0.75;
constexpr double kMinWarmupTimeSeconds = 0.20;
constexpr std::size_t kByteOpCorpusCount = 8U;
constexpr std::array<std::size_t, 3> kByteOpSizes = {256U, 4096U, 65536U};

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

std::vector<std::vector<std::byte>> make_byte_corpus(std::size_t sample_size, uint8_t value_mask) {
  std::vector<std::vector<std::byte>> corpus;
  corpus.reserve(kByteOpCorpusCount);
  for (std::size_t seed = 0; seed < kByteOpCorpusCount; ++seed) {
    std::vector<std::byte> sample(sample_size);
    uint32_t state = 0x9E3779B9U ^ static_cast<uint32_t>((seed + 1U) * 0x45D9F3BU);
    for (auto& index : sample) {
      state = (state * 1664525U) + 1013904223U + static_cast<uint32_t>(seed * 17U);
      index = static_cast<std::byte>(static_cast<uint8_t>(state & value_mask));
    }
    corpus.push_back(std::move(sample));
  }
  return corpus;
}

std::vector<std::vector<std::byte>> make_utf8_corpus(std::size_t sample_size) {
  constexpr std::array<std::array<std::byte, 1>, 1> kAsciiSequences = {{{static_cast<std::byte>('U')}}};
  constexpr std::array<std::array<std::byte, 2>, 1> kTwoByteSequences = {
      {{static_cast<std::byte>(0xC2), static_cast<std::byte>(0xA2)}}};
  constexpr std::array<std::array<std::byte, 3>, 1> kThreeByteSequences = {
      {{static_cast<std::byte>(0xE2), static_cast<std::byte>(0x82), static_cast<std::byte>(0xAC)}}};
  constexpr std::array<std::array<std::byte, 4>, 1> kFourByteSequences = {{{static_cast<std::byte>(0xF0),
                                                                            static_cast<std::byte>(0x9F),
                                                                            static_cast<std::byte>(0x92),
                                                                            static_cast<std::byte>(0xA9)}}};

  std::vector<std::vector<std::byte>> corpus;
  corpus.reserve(kByteOpCorpusCount);
  for (std::size_t seed = 0; seed < kByteOpCorpusCount; ++seed) {
    std::vector<std::byte> sample;
    sample.reserve(sample_size);
    uint32_t state = 0x85EBCA6BU ^ static_cast<uint32_t>((seed + 3U) * 0xC2B2AE35U);
    while (sample.size() < sample_size) {
      state = (state * 1664525U) + 1013904223U + static_cast<uint32_t>(seed * 29U);
      const auto append_sequence = [&sample, sample_size](const auto& sequence) {
        if (sample.size() + sequence.size() <= sample_size) {
          sample.insert(sample.end(), sequence.begin(), sequence.end());
          return true;
        }
        return false;
      };
      switch (state % 4U) {
        case 0:
          append_sequence(kAsciiSequences[0]);
          break;
        case 1:
          if (!append_sequence(kTwoByteSequences[0])) {
            append_sequence(kAsciiSequences[0]);
          }
          break;
        case 2:
          if (!append_sequence(kThreeByteSequences[0])) {
            append_sequence(kAsciiSequences[0]);
          }
          break;
        default:
          if (!append_sequence(kFourByteSequences[0])) {
            append_sequence(kAsciiSequences[0]);
          }
          break;
      }
    }
    corpus.push_back(std::move(sample));
  }
  return corpus;
}

void register_byte_span_benchmark(const std::string& name,
                                  std::vector<std::vector<std::byte>> corpus,
                                  std::size_t sample_size,
                                  auto&& operation) {
  benchmark::Benchmark* benchmark_rule = benchmark::RegisterBenchmark(
      name,
      [corpus = std::move(corpus), sample_size, operation = std::forward<decltype(operation)>(operation)](
          benchmark::State& state) {
        std::size_t sample_index = 0;
        uint64_t sink = 0;
        for (auto _ : state) {
          const std::vector<std::byte>& sample = corpus[sample_index % corpus.size()];
          sink ^= operation(upr::ByteSpan(sample));
          ++sample_index;
        }
        benchmark::DoNotOptimize(sink);
        state.SetItemsProcessed(state.iterations());
        state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(sample_size));
        state.counters["seed_count"] = static_cast<double>(corpus.size());
      });
  configure_benchmark(benchmark_rule);
}

void register_byte_op_benchmarks() {
  for (const std::size_t sample_size : kByteOpSizes) {
    register_byte_span_benchmark(
        "byte_ops/ascii_scalar/size_" + std::to_string(sample_size),
        make_byte_corpus(sample_size, 0x7FU),
        sample_size,
        [](upr::ByteSpan bytes) -> uint64_t { return upr::direct_decode_support::is_valid_ascii(bytes) ? 1U : 0U; });
    register_byte_span_benchmark("byte_ops/ascii_runtime/size_" + std::to_string(sample_size),
                                 make_byte_corpus(sample_size, 0x7FU),
                                 sample_size,
                                 [](upr::ByteSpan bytes) -> uint64_t {
                                   return upr::direct_decode_support::runtime_is_valid_ascii(bytes) ? 1U : 0U;
                                 });
    register_byte_span_benchmark(
        "byte_ops/utf8_scalar/size_" + std::to_string(sample_size),
        make_utf8_corpus(sample_size),
        sample_size,
        [](upr::ByteSpan bytes) -> uint64_t {
          return upr::direct_decode_support::validate_string<upr::StringEncoding::kUtf8>(bytes) ? 1U : 0U;
        });
    register_byte_span_benchmark(
        "byte_ops/utf8_runtime/size_" + std::to_string(sample_size),
        make_utf8_corpus(sample_size),
        sample_size,
        [](upr::ByteSpan bytes) -> uint64_t {
          return upr::direct_decode_support::runtime_validate_string<upr::StringEncoding::kUtf8>(bytes) ? 1U : 0U;
        });
    register_byte_span_benchmark(
        "byte_ops/checksum_xor8_scalar/size_" + std::to_string(sample_size),
        make_byte_corpus(sample_size, 0xFFU),
        sample_size,
        [](upr::ByteSpan bytes) -> uint64_t { return upr::direct_decode_support::checksum_xor8(bytes); });
    register_byte_span_benchmark(
        "byte_ops/checksum_xor8_runtime/size_" + std::to_string(sample_size),
        make_byte_corpus(sample_size, 0xFFU),
        sample_size,
        [](upr::ByteSpan bytes) -> uint64_t { return upr::direct_decode_support::runtime_checksum_xor8(bytes); });
    register_byte_span_benchmark(
        "byte_ops/checksum_sum16_scalar/size_" + std::to_string(sample_size),
        make_byte_corpus(sample_size, 0xFFU),
        sample_size,
        [](upr::ByteSpan bytes) -> uint64_t { return upr::direct_decode_support::checksum_sum16(bytes); });
    register_byte_span_benchmark(
        "byte_ops/checksum_sum16_runtime/size_" + std::to_string(sample_size),
        make_byte_corpus(sample_size, 0xFFU),
        sample_size,
        [](upr::ByteSpan bytes) -> uint64_t { return upr::direct_decode_support::runtime_checksum_sum16(bytes); });
    register_byte_span_benchmark(
        "byte_ops/checksum_crc32c_scalar/size_" + std::to_string(sample_size),
        make_byte_corpus(sample_size, 0xFFU),
        sample_size,
        [](upr::ByteSpan bytes) -> uint64_t { return upr::direct_decode_support::checksum_crc32c(bytes); });
    register_byte_span_benchmark(
        "byte_ops/checksum_crc32c_runtime/size_" + std::to_string(sample_size),
        make_byte_corpus(sample_size, 0xFFU),
        sample_size,
        [](upr::ByteSpan bytes) -> uint64_t { return upr::direct_decode_support::runtime_checksum_crc32c(bytes); });
  }
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

  register_byte_op_benchmarks();

  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
