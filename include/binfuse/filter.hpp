#pragma once

#include "binaryfusefilter.h"
#include "fmt/format.h"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace binfuse {

template <typename T>
concept filter_type = std::same_as<T, binary_fuse8_t> || std::same_as<T, binary_fuse16_t>;

// select which functions on the C-API will be called with specialisations of the function ptrs

template <filter_type FilterType>
struct ftype {};

template <>
struct ftype<binary_fuse8_t> {
  static constexpr auto* allocate            = binary_fuse8_allocate;
  static constexpr auto* populate            = binary_fuse8_populate;
  static constexpr auto* contains            = binary_fuse8_contain;
  static constexpr auto* free                = binary_fuse8_free;
  static constexpr auto* serialization_bytes = binary_fuse8_serialization_bytes;
  static constexpr auto* serialize           = binary_fuse8_serialize;
  static constexpr auto* deserialize_header  = binary_fuse8_deserialize_header;
  using fingerprint_t                        = std::uint8_t;
};

template <>
struct ftype<binary_fuse16_t> {
  static constexpr auto* allocate            = binary_fuse16_allocate;
  static constexpr auto* populate            = binary_fuse16_populate;
  static constexpr auto* contains            = binary_fuse16_contain;
  static constexpr auto* free                = binary_fuse16_free;
  static constexpr auto* serialization_bytes = binary_fuse16_serialization_bytes;
  static constexpr auto* serialize           = binary_fuse16_serialize;
  static constexpr auto* deserialize_header  = binary_fuse16_deserialize_header;
  using fingerprint_t                        = std::uint16_t;
};

/* binfuse::filter
 *
 * wraps a single binary_fuse(8|16)_filter
 */
template <filter_type FilterType>
class filter {
public:
  filter() = default;
  explicit filter(const std::vector<std::uint64_t>& keys) { populate(keys); }

  filter(const filter& other)          = delete;
  filter& operator=(const filter& rhs) = delete;

  filter(filter&& other) noexcept : size(other.size), fil(other.fil) {
    other.fil.Fingerprints = nullptr;
  }
  filter& operator=(filter&& rhs) noexcept {
    if (this != &rhs) *this = fil(std::move(rhs));
    return *this;
  }

  ~filter() {
    if (skip_free_fingerprints) {
      fil.Fingerprints = nullptr;
    }
    ftype<FilterType>::free(&fil);
  }

  void populate(const std::vector<std::uint64_t>& keys) {
    if (keys.empty()) {
      throw std::runtime_error("empty input");
    }
    size = keys.size();

    if (!ftype<FilterType>::allocate(keys.size(), &fil)) {
      throw std::runtime_error("failed to allocate memory.\n");
    }
    if (!ftype<FilterType>::populate(
            const_cast<std::uint64_t*>(keys.data()), // NOLINT const_cast until API changed
            keys.size(), &fil)) {
      throw std::runtime_error("failed to populate the filter");
    }
  }

  [[nodiscard]] bool contains(std::uint64_t needle) const {
    auto result = ftype<FilterType>::contains(needle, &fil);
    return result;
  }

  [[nodiscard]] std::size_t serialization_bytes() const {
    // upstream API should be const
    return ftype<FilterType>::serialization_bytes(const_cast<FilterType*>(&fil));
  }

  void serialize(char* buffer) const { ftype<FilterType>::serialize(&fil, buffer); }

  void deserialize(const char* buffer) {
    const char* fingerprints = ftype<FilterType>::deserialize_header(&fil, buffer);

    // set the freshly deserialized object's Fingerprint ptr (which is
    // where the bulk of the data is), to the byte immediately AFTER
    // the block where header bytes were deserialized FROM.
    fil.Fingerprints = // NOLINTNEXTLINE const_cast & rein_cast due to upstream API
        reinterpret_cast<ftype<FilterType>::fingerprint_t*>(const_cast<char*>(fingerprints));

    skip_free_fingerprints = true; // do not attempt to free this external buffer (probably an mmap)
  }

  [[nodiscard]] bool verify(const std::vector<std::uint64_t>& keys) const {
    for (auto key: keys) {
      if (!contains(key)) {
        std::cerr << fmt::format(
            "binary_fuse_filter::verify: Detected a false negative: {:016X} \n", key);
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] double estimate_false_positive_rate() const {
    auto   gen         = std::mt19937_64(std::random_device{}());
    size_t matches     = 0;
    const size_t sample_size = 1'000'000;
    for (size_t t = 0; t < sample_size; t++) {
      if (contains(gen())) { // no distribution needed
        matches++;
      }
    }
    return static_cast<double>(matches) / static_cast<double>(sample_size) -
           static_cast<double>(size) /
               static_cast<double>(std::numeric_limits<std::uint64_t>::max());
  }

private:
  std::size_t size = 0;
  FilterType  fil{};
  bool        skip_free_fingerprints = false;
};

using filter8  = filter<binary_fuse8_t>;
using filter16 = filter<binary_fuse16_t>;

} // namespace binfuse
