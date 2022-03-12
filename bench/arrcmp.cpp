#include "arrcmp.hpp"
#include "asmlib.h"
#include <benchmark/benchmark.h>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>

static constexpr std::size_t size = 20;
static constexpr std::size_t step = 2;
static constexpr std::size_t seed = 2;

void arraycmp(benchmark::State& state) {
  std::mt19937_64                             rgen(seed); // NOLINT fixed seed
  std::uniform_int_distribution<std::uint8_t> dist(0, 255);

  auto idx = static_cast<std::size_t>(state.range(0));

  std::array<std::byte, size> hash1{};
  hash1[idx] = static_cast<std::byte>(dist(rgen));

  std::array<std::byte, size> hash2{};
  hash2[idx] = static_cast<std::byte>(dist(rgen));

  for (auto _: state) {
    auto result = arrcmp::array_compare<size>(&hash1[0], &hash2[0], arrcmp::three_way{});
    benchmark::DoNotOptimize(result);
    benchmark::DoNotOptimize(hash1);
    benchmark::DoNotOptimize(hash2);
  }
}
BENCHMARK(arraycmp)->DenseRange(0, size - 1, step);

void asmlib(benchmark::State& state) {
  std::mt19937_64                             rgen(seed); // NOLINT fixed seed
  std::uniform_int_distribution<std::uint8_t> dist(0, 255);

  auto idx = static_cast<std::size_t>(state.range(0));

  std::array<std::byte, size> hash1{};
  hash1[idx] = static_cast<std::byte>(dist(rgen));

  std::array<std::byte, size> hash2{};
  hash2[idx] = static_cast<std::byte>(dist(rgen));

  for (auto _: state) {
    auto result = A_memcmp(&hash1[0], &hash2[0], size);
    benchmark::DoNotOptimize(result);
    benchmark::DoNotOptimize(hash1);
    benchmark::DoNotOptimize(hash2);
  }
}
BENCHMARK(asmlib)->DenseRange(0, size - 1, step);

void glibc(benchmark::State& state) {
  std::mt19937_64                             rgen(seed); // NOLINT fixed seed
  std::uniform_int_distribution<std::uint8_t> dist(0, 255);

  auto idx = static_cast<std::size_t>(state.range(0));

  std::array<std::byte, size> hash1{};
  hash1[idx] = static_cast<std::byte>(dist(rgen));

  std::array<std::byte, size> hash2{};
  hash2[idx] = static_cast<std::byte>(dist(rgen));

  for (auto _: state) {
    auto result = memcmp(&hash1[0], &hash2[0], size);
    benchmark::DoNotOptimize(result);
    benchmark::DoNotOptimize(hash1);
    benchmark::DoNotOptimize(hash2);
  }
}
BENCHMARK(glibc)->DenseRange(0, size - 1, step);

BENCHMARK_MAIN();
