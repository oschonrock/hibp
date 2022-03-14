#include <benchmark/benchmark.h>
#include <bit>
#include <compare>
#include <cstdint>

template <typename T>
requires std::integral<T> std::strong_ordering cmp(T a, T b) { return a <=> b; }

template <typename T>
requires std::integral<T> std::strong_ordering cmp_new(T a, T b) {
  return std::bit_cast<std::strong_ordering>(static_cast<std::int8_t>((a > b) - (a < b)));
}

void cmpUint8(benchmark::State& state) {
  using T = std::uint8_t;
  T a     = 1;
  T b     = 2;
  for (auto _: state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    auto result = cmp(a, b);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(cmpUint8);

void cmpUint16(benchmark::State& state) {
  using T = std::uint16_t;
  T a     = 1;
  T b     = 2;
  for (auto _: state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    auto result = cmp(a, b);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(cmpUint16);

void cmpUint32(benchmark::State& state) {
  using T = std::uint32_t;
  T a     = 1;
  T b     = 2;
  for (auto _: state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    auto result = cmp(a, b);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(cmpUint32);

void cmpUint64(benchmark::State& state) {
  using T = std::uint64_t;
  T a     = 1;
  T b     = 2;
  for (auto _: state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    auto result = cmp(a, b);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(cmpUint64);

void cmpNewUint8(benchmark::State& state) {
  using T = std::uint8_t;
  T a     = 1;
  T b     = 2;
  for (auto _: state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    auto result = cmp_new(a, b);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(cmpNewUint8);

void cmpNewUint16(benchmark::State& state) {
  using T = std::uint16_t;
  T a     = 1;
  T b     = 2;
  for (auto _: state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    auto result = cmp_new(a, b);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(cmpNewUint16);

void cmpNewUint32(benchmark::State& state) {
  using T = std::uint32_t;
  T a     = 1;
  T b     = 2;
  for (auto _: state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    auto result = cmp_new(a, b);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(cmpNewUint32);

void cmpNewUint64(benchmark::State& state) {
  using T = std::uint64_t;
  T a     = 1;
  T b     = 2;
  for (auto _: state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    auto result = cmp_new(a, b);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(cmpNewUint64);

BENCHMARK_MAIN();
