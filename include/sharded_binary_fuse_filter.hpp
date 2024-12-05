#include "binaryfusefilter.h"
#include "mio/mmap.hpp"
#include "mio/page.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>

template <typename T>
concept filter_type = std::same_as<T, binary_fuse8_t> || std::same_as<T, binary_fuse16_t>;

// select which functions on the C-API will be called with specialisations of the function ptrs

template <filter_type FilterType>
struct dispatch {};

template <>
struct dispatch<binary_fuse8_t> {
  static constexpr auto* allocate            = binary_fuse8_allocate;
  static constexpr auto* populate            = binary_fuse8_populate;
  static constexpr auto* contain             = binary_fuse8_contain;
  static constexpr auto* free                = binary_fuse8_free;
  static constexpr auto* serialization_bytes = binary_fuse8_serialization_bytes;
  static constexpr auto* serialize           = binary_fuse8_serialize;
};

template <>
struct dispatch<binary_fuse16_t> {
  static constexpr auto* allocate            = binary_fuse16_allocate;
  static constexpr auto* populate            = binary_fuse16_populate;
  static constexpr auto* contain             = binary_fuse16_contain;
  static constexpr auto* free                = binary_fuse16_free;
  static constexpr auto* serialization_bytes = binary_fuse16_serialization_bytes;
  static constexpr auto* serialize           = binary_fuse16_serialize;
};

template <filter_type FilterType>
class bin_fuse_filter {
public:
  bin_fuse_filter() = default;

  bin_fuse_filter(const bin_fuse_filter& other)            = delete;
  bin_fuse_filter& operator=(const bin_fuse_filter& other) = delete;

  bin_fuse_filter(bin_fuse_filter&& other) noexcept
      : prefix(other.prefix), size(other.size), filter(other.filter) {
    other.filter.Fingerprints = nullptr;
  }
  bin_fuse_filter& operator=(bin_fuse_filter&& other) noexcept {
    if (this != &other) {
      *this = bin_fuse_filter(std::move(other));
    }
    return *this;
  }

  ~bin_fuse_filter() { dispatch<FilterType>::free(&filter); }

  void populate(const std::vector<std::uint64_t>& keys) {
    if (keys.empty()) {
      throw std::runtime_error("empty input");
    }
    auto front = keys.front();
    prefix     = front >> ((sizeof(front) - sizeof(prefix)) * 8);
    size       = keys.size();

    if (!dispatch<FilterType>::allocate(keys.size(), &filter)) {
      throw std::runtime_error("failed to allocate memory.\n");
    }
    if (!dispatch<FilterType>::populate(
            const_cast<std::uint64_t*>(keys.data()), // NOLINT const_cast until API changed
            keys.size(), &filter)) {
      throw std::runtime_error("failed to build the filter, do you have sufficient memory?\n");
    }
  }

  std::size_t serialization_bytes() { return dispatch<FilterType>::serialization_bytes(&filter); }

  void serialize(char* buffer) { dispatch<FilterType>::serialize(&filter, buffer); }

  bool verify(const std::vector<std::uint64_t>& keys) {
    using clk    = std::chrono::high_resolution_clock;
    using micros = std::chrono::microseconds;
    auto start   = clk::now();
    for (auto key: keys) {
      if (!dispatch<FilterType>::contain(key, &filter)) {
        std::cerr << "binary_fuse_filter::verify: Detected a false negative.\n";
        return false;
      }
    }
    std::cout << fmt::format("{:<30} {:>12}\n", "verify",
                             duration_cast<micros>(clk::now() - start));
    return true;
  }

  double estimate_false_positive_rate() {
    using clk    = std::chrono::high_resolution_clock;
    using micros = std::chrono::microseconds;
    auto start   = clk::now();

    auto   gen     = std::mt19937_64(std::random_device{}());
    size_t matches = 0;
    size_t volume  = 1'000'000;
    for (size_t t = 0; t < volume; t++) {
      if (dispatch<FilterType>::contain(gen(), &filter)) { // no distribution needed
        matches++;
      }
    }
    std::cout << fmt::format("{:<30} {:>12}\n", "est false +ve rate",
                             duration_cast<micros>(clk::now() - start));
    return static_cast<double>(matches) * 100.0 / static_cast<double>(volume) -
           static_cast<double>(size) /
               static_cast<double>(std::numeric_limits<std::uint64_t>::max());
  }

  std::uint8_t prefix = 0;

private:
  std::size_t size = 0;
  FilterType  filter;
};

// easy to use aliases

using bin_fuse8_filter  = bin_fuse_filter<binary_fuse8_t>;
using bin_fuse16_filter = bin_fuse_filter<binary_fuse16_t>;

template <typename T>
concept mmap_type = std::same_as<T, mio::mmap_sink> || std::same_as<T, mio::mmap_source>;

// sharded_bin_fuse_filter
template <filter_type FilterType, mio::access_mode AccessMode>
class sharded_bin_fuse_filter {
public:
  sharded_bin_fuse_filter() = default;
  explicit sharded_bin_fuse_filter(std::filesystem::path path) : filepath(std::move(path)) {}

  // here so we can have a default constructor, not normally used
  void set_filename(std::filesystem::path path) { filepath = std::move(path); }

  void load()
    requires(AccessMode == mio::access_mode::read)
  {
    std::cout << "loading\n";
  }

  void add(bin_fuse_filter<FilterType>&& filter)
    requires(AccessMode == mio::access_mode::write)
  {
    if (filepath.empty()) {
      throw std::runtime_error(
          fmt::format("filename not set or file doesn't exist '{}'", filepath));
    }

    if (filter.prefix != next_prefix) {
      throw std::runtime_error(fmt::format("expecting a shard with prefix {}", next_prefix));
    }
    // need check for overflow
    // if (filters.size() == max_capacity) {
    //   throw std::runtime_error(
    //       fmt::format("sharded filter has reached max capacity of {}", max_capacity));
    // }

    std::size_t size_req = filter.serialization_bytes();
    std::size_t existing_size = 0;
    if (std::filesystem::exists(filepath)) {
      existing_size = std::filesystem::file_size(filepath);
    } else {
      std::ofstream tmp(filepath); // "touch"
    }

    std::filesystem::resize_file(filepath, existing_size + size_req);
    sink_map =
        mio::mmap_sink(filepath.string()); // does move assignment destruct/unmap the old map?
    filter.serialize(sink_map.data());
    ++next_prefix;
    std::cout << "added\n";
  }

private:
  mio::mmap_sink   sink_map;
  mio::mmap_source source_map;

  std::uint8_t                 next_prefix = 0;
  std::filesystem::path        filepath;
  static constexpr std::size_t max_capacity = std::numeric_limits<decltype(next_prefix)>::max() + 1;
};

// easy to use aliases
using sharded_bin_fuse8_filter_sink =
    sharded_bin_fuse_filter<binary_fuse8_t, mio::access_mode::write>;
using sharded_bin_fuse8_filter_source =
    sharded_bin_fuse_filter<binary_fuse8_t, mio::access_mode::read>;

using sharded_bin_fuse16_filter_sink =
    sharded_bin_fuse_filter<binary_fuse16_t, mio::access_mode::write>;
using sharded_bin_fuse16_filter_source =
    sharded_bin_fuse_filter<binary_fuse16_t, mio::access_mode::read>;
