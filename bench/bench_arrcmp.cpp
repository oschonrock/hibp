#include "arrcmp.hpp"
#include "asmlib.h"
#include <benchmark/benchmark.h>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>

static constexpr std::size_t size = 100;
static constexpr std::size_t step = 4;

void arrCmpInt(benchmark::State& state) {
  auto idx = static_cast<std::size_t>(state.range(0));

  std::array<std::byte, size> hash1{};
  hash1[idx] = static_cast<std::byte>(0x01);

  std::array<std::byte, size> hash2{};
  hash2[idx] = static_cast<std::byte>(0x02);

  for (auto _: state) {
    auto result = arrcmp::array_compare<size>(&hash1[0], &hash2[0], arrcmp::three_way_int{});
    benchmark::DoNotOptimize(result);
    benchmark::DoNotOptimize(hash1);
    benchmark::DoNotOptimize(hash2);
  }
}
BENCHMARK(arrCmpInt)->DenseRange(0, size - 1, step);

void arrCmpOrdering(benchmark::State& state) {
  auto idx = static_cast<std::size_t>(state.range(0));

  std::array<std::byte, size> hash1{};
  hash1[idx] = static_cast<std::byte>(0x01);

  std::array<std::byte, size> hash2{};
  hash2[idx] = static_cast<std::byte>(0x02);

  for (auto _: state) {
    auto result = arrcmp::array_compare<size>(&hash1[0], &hash2[0], arrcmp::three_way{});
    benchmark::DoNotOptimize(result);
    benchmark::DoNotOptimize(hash1);
    benchmark::DoNotOptimize(hash2);
  }
}
BENCHMARK(arrCmpOrdering)->DenseRange(0, size - 1, step);

void asmlib(benchmark::State& state) {
  auto idx = static_cast<std::size_t>(state.range(0));

  std::array<std::byte, size> hash1{};
  hash1[idx] = static_cast<std::byte>(0x01);

  std::array<std::byte, size> hash2{};
  hash2[idx] = static_cast<std::byte>(0x02);

  for (auto _: state) {
    auto result = A_memcmp(&hash1[0], &hash2[0], size);
    benchmark::DoNotOptimize(result);
    benchmark::DoNotOptimize(hash1);
    benchmark::DoNotOptimize(hash2);
  }
}
BENCHMARK(asmlib)->DenseRange(0, size - 1, step);

void glibc(benchmark::State& state) {
  auto idx = static_cast<std::size_t>(state.range(0));

  std::array<std::byte, size> hash1{};
  hash1[idx] = static_cast<std::byte>(0x01);

  std::array<std::byte, size> hash2{};
  hash2[idx] = static_cast<std::byte>(0x02);

  for (auto _: state) {
    auto result = memcmp(&hash1[0], &hash2[0], size);
    benchmark::DoNotOptimize(result);
    benchmark::DoNotOptimize(hash1);
    benchmark::DoNotOptimize(hash2);
  }
}
BENCHMARK(glibc)->DenseRange(0, size - 1, step);

BENCHMARK_MAIN();
