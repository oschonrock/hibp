#pragma once

#include "bin_fuse_filter.hpp"
#include "fmt/format.h"
#include "mio/mmap.hpp"
#include "mio/page.hpp"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace binfuse {

// selecting the appropriate map for the access mode
template <mio::access_mode AccessMode>
struct sharded_mmap_base {
  mio::basic_mmap<AccessMode, char> mmap;
  std::vector<std::size_t>          filter_offsets;
};

template <mio::access_mode AccessMode, filter_type FilterType>
struct sharded_base {};

template <filter_type FilterType>
struct sharded_base<mio::access_mode::read, FilterType> {
  std::vector<filter<FilterType>> filters;
};

/* sharded_bin_fuse_filter.
 *
 * Wraps a set of `binfuse::filter`s.
 * Saves/loads them to/from an mmap'd file via mio::mmap.
 * Directs `contains` queries to the apropriate sub-filter.
 *
 */
template <filter_type FilterType, mio::access_mode AccessMode>
class sharded_filter : private sharded_mmap_base<AccessMode>,
                       private sharded_base<AccessMode, FilterType> {
public:
  sharded_filter() = default;
  explicit sharded_filter(std::filesystem::path path) : filepath(std::move(path)) { load(); }

  // here so we can have a default constructor, not normally used
  void set_filename(std::filesystem::path path) {
    filepath = std::move(path);
    load();
  }

  // only does something for AccessMode = read
  void load()
    requires(AccessMode == mio::access_mode::read)
  {
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

  void load()
    requires(AccessMode == mio::access_mode::write)
  {}

  [[nodiscard]] bool contains(std::uint64_t needle) const
    requires(AccessMode == mio::access_mode::read)
  {
    // TODO select the correct filter
    auto& filter = this->filters[0];
    return filter.contains(needle);
  }

  [[nodiscard]] std::uint32_t extract_prefix(std::uint64_t key) const {
    return key >> (sizeof(key) * 8 - shard_bits);
  }

  void add(filter<FilterType>&& filter, std::uint32_t prefix)
    requires(AccessMode == mio::access_mode::write)
  {
    if (prefix != next_prefix) {
      throw std::runtime_error(fmt::format("expecting a shard with prefix {}", next_prefix));
    }
    if (next_prefix == capacity()) {
      throw std::runtime_error(
          fmt::format("sharded filter has reached max capacity of {}", capacity()));
    }

    std::size_t new_size = ensure_header();

    std::size_t size_req          = filter.serialization_bytes();
    auto        new_filter_offset = new_size; // place new filter at end
    new_size += size_req;

    sync();
    std::filesystem::resize_file(filepath, new_size);
    map_whole_file();

    std::cerr << "prefix: " << prefix << "\n";
    std::cerr << "filter_index_offset(prefix): " << filter_index_offset(prefix) << "\n";
    std::cerr << "filter_offset(prefix): " << filter_offset(prefix) << "\n";

    memcpy(&this->mmap[filter_index_offset(prefix)], &new_filter_offset, sizeof(new_filter_offset));
    filter.serialize(&this->mmap[filter_offset(prefix)]);
    ++size;
    ++next_prefix;
  }

  std::uint8_t shard_bits = 8;

private:
  using clk    = std::chrono::high_resolution_clock;
  using micros = std::chrono::microseconds;

  using offset_t = typename decltype(sharded_mmap_base<AccessMode>::filter_offsets)::value_type;
  static constexpr auto empty_offset = static_cast<offset_t>(-1);

  std::uint32_t         next_prefix = 0;
  std::filesystem::path filepath;

  /* file structure is as follows:
   *
   * header [0 -> 16) : small number of bytes identifying the type of file, the
   * type of filters contained and how many shards are contained.
   *
   * index [16 -> 16 + 8 * max_capacity() ): table of offsets to each
   * filter in the body. The offsets in the table are relative to the
   * start of the body segment.
   *
   * body [16 + 8 * max_capacity() -> end ): the filters: each one has
   * the filter_struct_fields (ie the "header") followed by the large
   * array of (8 or 16bit) fingerprints. The offsets in the index will
   * point the start of the filter_header (relative to start of body
   * section), so that deserialize can be called directly on that.
   *
   */
  static constexpr std::size_t header_start  = 0;
  static constexpr std::size_t header_length = 16;

  static constexpr std::size_t index_start = header_start + header_length;

  [[nodiscard]] std::uint32_t capacity() const { return 1U << shard_bits; }
  std::uint32_t               size = 0;

  [[nodiscard]] std::size_t index_length() const { return sizeof(std::size_t) * capacity(); }

  [[nodiscard]] std::size_t body_start() const { return index_start + index_length(); }

  [[nodiscard]] std::size_t filter_index_offset(std::uint32_t prefix) const {
    return index_start + sizeof(std::size_t) * prefix;
  }

  [[nodiscard]] std::size_t filter_offset(std::uint32_t prefix) const {
    return *reinterpret_cast<const std::size_t*>(&this->mmap[filter_index_offset(prefix)]);
  }

  [[nodiscard]] std::size_t filter_offset(std::uint32_t prefix) {
    return *reinterpret_cast<std::size_t*>(&this->mmap[filter_index_offset(prefix)]);
  }

  [[nodiscard]] std::size_t filter_start(uint32_t pos) const {
    return body_start() + filter_offset(pos);
  }

  [[nodiscard]] std::string type_id() const {
    return fmt::format("sbinfuse{:02d}", sizeof(typename ftype<FilterType>::fingerprint_t) * 8);
  }

  void sync()
    requires(AccessMode == mio::access_mode::write)
  {
    std::error_code err;
    this->mmap.sync(err); // ensure any existing map is sync'd
    if (err) {
      throw std::runtime_error(
          fmt::format("sharded_bin_fuse_filter:: mmap.map(): {}", err.message()));
    }
  }

  void map_whole_file() {
    std::error_code err;
    this->mmap.map(filepath.string(), err);
    if (err) {
      throw std::runtime_error(
          fmt::format("sharded_bin_fuse_filter:: mmap.map(): {}", err.message()));
    }
  }

  void check_type_id() const {
    auto        tid = type_id();
    std::string check_tid;
    check_tid.resize(tid.size());
    memcpy(check_tid.data(), &this->mmap[0], check_tid.size());
    if (check_tid != tid) {
      throw std::runtime_error(
          fmt::format("incorrect type_id: expected {}, found: {} ", tid, check_tid));
    }
  }

  void check_capacity() const {
    std::uint32_t check_capacity = 0;
    std::from_chars(&this->mmap[11], &this->mmap[15], check_capacity);
    if (check_capacity != capacity()) {
      throw std::runtime_error(
          fmt::format("wrong capacity: expected: {}, found: {}", capacity(), check_capacity));
    }
  }

  // returns existing file size
  std::size_t ensure_file() {
    if (filepath.empty()) {
      throw std::runtime_error(
          fmt::format("filename not set or file doesn't exist '{}'", filepath));
    }

    std::size_t existing_filesize = 0;
    if (std::filesystem::exists(filepath)) {
      existing_filesize = std::filesystem::file_size(filepath);
    } else {
      std::ofstream tmp(filepath); // "touch"
    }
    return existing_filesize;
  }

  void create_filetag() {
    std::string filetag = fmt::format("{}-{:04d}", type_id(), capacity());
    memcpy(&this->mmap[0], filetag.data(), filetag.size() * sizeof(decltype(filetag)::value_type));
  }

  void create_index() {
    this->filter_offsets.resize(capacity(), empty_offset);
    memcpy(&this->mmap[index_start], this->filter_offsets.data(),
           this->filter_offsets.size() * sizeof(offset_t));
  }

  void load_index() {
    this->filter_offsets.resize(capacity(), empty_offset);
    memcpy(this->filter_offsets.data(), &this->mmap[index_start],
           this->filter_offsets.size() * sizeof(offset_t));

    auto iter = find_if(this->filter_offsets.begin(), this->filter_offsets.end(),
                        [](auto a) { return a != empty_offset; });
    size      = iter - this->filter_offsets.begin();
  }

  // returns new_filesize
  std::size_t ensure_header()
    requires(AccessMode == mio::access_mode::write)
  {
    std::size_t existing_filesize = ensure_file();
    std::size_t new_size          = existing_filesize;
    if (existing_filesize < header_length + index_length()) {
      std::cout << "new\n";
      if (existing_filesize != 0) {
        throw std::runtime_error("corrupt file: header and index half written?!");
      }
      // existing_size == 0 here
      new_size += header_length + index_length();
      std::filesystem::resize_file(filepath, new_size);
      std::error_code err;
      this->mmap.map(filepath.string(), err);
      if (err) {
        throw std::runtime_error(
            fmt::format("sharded_bin_fuse_filter::ensure_header mmap.map(): {}", err.message()));
      }
      create_filetag();
      create_index();

      sync(); // write to disk
      size = 0;
    } else {
      std::cout << "not new\n";
      // we have a header already
      std::error_code err;
      this->mmap.map(filepath.string(), err);
      if (err) {
        throw std::runtime_error(fmt::format(
            "sharded_bin_fuse_filter::ensure_header check mmap.map(): {}", err.message()));
      }
      check_type_id();
      check_capacity();
      load_index();
    }
    return new_size;
  }
};

// easy to use aliases
using sharded_filter8_sink = sharded_filter<binary_fuse8_t, mio::access_mode::write>;

using sharded_filter8_source = sharded_filter<binary_fuse8_t, mio::access_mode::read>;

using sharded_filter16_sink = sharded_filter<binary_fuse16_t, mio::access_mode::write>;

using sharded_filter16_source = sharded_filter<binary_fuse16_t, mio::access_mode::read>;

// explicit instantiations for clangd help
template class sharded_filter<binary_fuse8_t, mio::access_mode::write>;
template class sharded_filter<binary_fuse8_t, mio::access_mode::read>;
template class sharded_filter<binary_fuse16_t, mio::access_mode::write>;
template class sharded_filter<binary_fuse16_t, mio::access_mode::read>;

} // namespace binfuse
