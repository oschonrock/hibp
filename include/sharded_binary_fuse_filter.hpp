#include "binaryfusefilter.h"
#include "fmt/format.h"
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
#include <system_error>

template <typename T>
concept filter_type = std::same_as<T, binary_fuse8_t> || std::same_as<T, binary_fuse16_t>;

// modified API from lib until this is accepted as a patch
template <filter_type FilterType>
inline static const char* binary_fuse_deserialize_header(FilterType* filter, const char* buffer) {
  memcpy(&filter->Seed, buffer, sizeof(filter->Seed));
  buffer += sizeof(filter->Seed);
  memcpy(&filter->SegmentLength, buffer, sizeof(filter->SegmentLength));
  buffer += sizeof(filter->SegmentLength);
  filter->SegmentLengthMask = filter->SegmentLength - 1;
  memcpy(&filter->SegmentCount, buffer, sizeof(filter->SegmentCount));
  buffer += sizeof(filter->SegmentCount);
  memcpy(&filter->SegmentCountLength, buffer, sizeof(filter->SegmentCountLength));
  buffer += sizeof(filter->SegmentCountLength);
  memcpy(&filter->ArrayLength, buffer, sizeof(filter->ArrayLength));
  buffer += sizeof(filter->ArrayLength);
  return buffer;
}

// select which functions on the C-API will be called with specialisations of the function ptrs

template <filter_type FilterType>
struct dispatch {};

template <>
struct dispatch<binary_fuse8_t> {
  static constexpr auto* allocate            = binary_fuse8_allocate;
  static constexpr auto* populate            = binary_fuse8_populate;
  static constexpr auto* contains            = binary_fuse8_contain;
  static constexpr auto* free                = binary_fuse8_free;
  static constexpr auto* serialization_bytes = binary_fuse8_serialization_bytes;
  static constexpr auto* serialize           = binary_fuse8_serialize;
  static constexpr auto* deserialize_header  = binary_fuse_deserialize_header<binary_fuse8_t>;
};

template <>
struct dispatch<binary_fuse16_t> {
  static constexpr auto* allocate            = binary_fuse16_allocate;
  static constexpr auto* populate            = binary_fuse16_populate;
  static constexpr auto* contains            = binary_fuse16_contain;
  static constexpr auto* free                = binary_fuse16_free;
  static constexpr auto* serialization_bytes = binary_fuse16_serialization_bytes;
  static constexpr auto* deserialize_header  = binary_fuse_deserialize_header<binary_fuse16_t>;
};

/* bin_fuse_filter
 *
 * wraps a single bin_fuse(8|16)_filter
 */
template <filter_type FilterType>
class bin_fuse_filter {
public:
  bin_fuse_filter() = default;
  explicit bin_fuse_filter(const std::vector<std::uint64_t>& keys) { populate(keys); }

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

  ~bin_fuse_filter() {
    std::cerr << fmt::format("~bin_fuse_filter()\n");
    if (skip_free_fingerprints) {
      std::cerr << fmt::format(
          "fingerprints prt = {}  => nulling ptr before calling bin_fuseX_free()\n",
          (void*)filter.Fingerprints); // NOLINT
      filter.Fingerprints = nullptr;
    }
    dispatch<FilterType>::free(&filter);
  }

  void populate(const std::vector<std::uint64_t>& keys) {
    auto start = clk::now();

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
      throw std::runtime_error("failed to populate the filter");
    }
    std::cout << fmt::format("{:<15} {:>12}\n", "populate", duration_cast<micros>(clk::now() - start));
  }

  bool contains(std::uint64_t needle) {
    auto result = dispatch<FilterType>::contains(needle, &filter);
    return result;
  }

  std::size_t serialization_bytes() { return dispatch<FilterType>::serialization_bytes(&filter); }

  void serialize(char* buffer) { dispatch<FilterType>::serialize(&filter, buffer); }

  void deserialize(const char* buffer) {
    auto        start        = clk::now();
    const char* fingerprints = dispatch<FilterType>::deserialize_header(&filter, buffer);
    // NOLINTNEXTLINE const_cast & rein_cast: API is flawed
    filter.Fingerprints    = reinterpret_cast<std::uint8_t*>(const_cast<char*>(fingerprints));
    skip_free_fingerprints = true; // do not attempt to free this external buffer (probably an mmap)
    std::cout << fmt::format("{:<15} {:>12}\n", "deserialize",
                             duration_cast<micros>(clk::now() - start));
  }

  bool verify(const std::vector<std::uint64_t>& keys) {
    auto start = clk::now();
    for (auto key: keys) {
      if (!contains(key)) {
        std::cerr << "binary_fuse_filter::verify: Detected a false negative.\n";
        return false;
      }
    }
    std::cout << fmt::format("{:<30} {:>12}\n", "verify",
                             duration_cast<micros>(clk::now() - start));
    return true;
  }

  double estimate_false_positive_rate() {
    auto start = clk::now();

    auto   gen         = std::mt19937_64(std::random_device{}());
    size_t matches     = 0;
    size_t sample_size = 1'000'000;
    for (size_t t = 0; t < sample_size; t++) {
      if (contains(gen())) { // no distribution needed
        matches++;
      }
    }
    std::cout << fmt::format("{:<30} {:>12}\n", "est false +ve rate",
                             duration_cast<micros>(clk::now() - start));
    return static_cast<double>(matches) * 100.0 / static_cast<double>(sample_size) -
           static_cast<double>(size) /
               static_cast<double>(std::numeric_limits<std::uint64_t>::max());
  }

  std::uint8_t prefix = 0;

private:
  using clk    = std::chrono::high_resolution_clock;
  using micros = std::chrono::microseconds;

  std::size_t size = 0;
  FilterType  filter;
  bool        skip_free_fingerprints = false;
};

using bin_fuse8_filter  = bin_fuse_filter<binary_fuse8_t>;
using bin_fuse16_filter = bin_fuse_filter<binary_fuse16_t>;

// selecting the appropriate map for the access mode
template <mio::access_mode AccessMode>
struct sharded_mmap_base {
  mio::basic_mmap<AccessMode, char> mmap;
};

template <mio::access_mode AccessMode, filter_type FilterType>
struct sharded_base {};

template <filter_type FilterType>
struct sharded_base<mio::access_mode::read, FilterType> {
  std::vector<bin_fuse_filter<FilterType>> filters;
};

/* sharded_bin_fuse_filter
 *
 * Wraps a set of `bin_fuse_filter`s.
 * Saves/loads them to/from an mmap'd file.
 * Directs `contains` queries to the apropriate sub-filter
 */
template <filter_type FilterType, mio::access_mode AccessMode>
class sharded_bin_fuse_filter : private sharded_mmap_base<AccessMode>,
                                private sharded_base<AccessMode, FilterType> {
public:
  sharded_bin_fuse_filter() = default;
  explicit sharded_bin_fuse_filter(std::filesystem::path path) : filepath(std::move(path)) {
    load();
  }

  // here so we can have a default constructor, not normally used
  void set_filename(std::filesystem::path path) {
    filepath = std::move(path);
    load();
  }

  // only does something for AccessMode = read
  void load() {
    if constexpr (AccessMode == mio::access_mode::read) {
      auto start = clk::now();

      std::error_code err;
      this->mmap.map(filepath.string(), err);
      if (err) {
        throw std::runtime_error(fmt::format("mmap.map(): {}", err.message()));
      }
      std::cout << fmt::format("{:<15} {:>8}\n", "mmap", duration_cast<micros>(clk::now() - start));

      // TODO load all filters
      this->filters.resize(1);
      auto& filter = this->filters[0];
      filter.deserialize(this->mmap.data());
    }
  }

  bool contains(std::uint64_t needle)
    requires(AccessMode == mio::access_mode::read)
  {
    // TODO select the correct filter
    auto& filter = this->filters[0];
    return filter.contains(needle);
  }

  void add(bin_fuse_filter<FilterType>&& filter)
    requires(AccessMode == mio::access_mode::write)
  {
    auto start = clk::now();
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

    std::size_t size_req      = filter.serialization_bytes();
    std::size_t existing_size = 0;
    if (std::filesystem::exists(filepath)) {
      existing_size = std::filesystem::file_size(filepath);
    } else {
      std::ofstream tmp(filepath); // "touch"
    }
    std::size_t new_size = existing_size + size_req;

    this->mmap.unmap(); // ensure any existing map is sync'd
    std::filesystem::resize_file(filepath, new_size);
    std::error_code err;
    this->mmap.map(filepath.string(), err);
    if (err) {
      throw std::runtime_error(fmt::format("sharded_bin_fuse_filter:: mmap.map(): {}", err.message()));
    }
    filter.serialize(this->mmap.data());
    ++next_prefix;
    std::cout << fmt::format("{:<15} {:>12}\n", "add", duration_cast<micros>(clk::now() - start));
  }

private:
  using clk    = std::chrono::high_resolution_clock;
  using micros = std::chrono::microseconds;

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
