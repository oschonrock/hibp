#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <execution>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <queue>
#include <string>
#include <type_traits>
#include <vector>

namespace flat_file {

namespace impl {

struct ofstream_holder {
  explicit ofstream_holder(std::string dbfilename)
      : filename_(std::move(dbfilename)), ofstream_(filename_, std::ios::binary) {}

  std::string   filename_;
  std::ofstream ofstream_;
};

} // namespace impl

template <typename ValueType>
class stream_writer {

public:
  explicit stream_writer(std::ostream& os, std::size_t buf_size = 1000) : db_(os), buf_(buf_size) {
    db_.exceptions(std::ios::badbit | std::ios::failbit);
  }

  void write(const ValueType& value) {
    if (buf_pos_ == buf_.size()) flush();
    std::memcpy(&buf_[buf_pos_], &value, sizeof(ValueType));
    ++buf_pos_;
  }

  void flush() {
    if (buf_pos_ != 0) {
      db_.write(reinterpret_cast<char*>(buf_.data()), // NOLINT reincast
                static_cast<std::streamsize>(sizeof(ValueType) * buf_pos_));
      buf_pos_ = 0;
    }
  }

  // a "unique manager" .. no copies, move-only
  stream_writer(const stream_writer& other)            = delete;
  stream_writer& operator=(const stream_writer& other) = delete;

  stream_writer(stream_writer&& other) noexcept            = default;
  stream_writer& operator=(stream_writer&& other) noexcept = default;

  ~stream_writer() { flush(); }

private:
  std::ostream&          db_;
  std::size_t            buf_pos_ = 0;
  std::vector<ValueType> buf_;
};

template <typename ValueType>
class file_writer : private impl::ofstream_holder, public stream_writer<ValueType> {
public:
  explicit file_writer(std::string dbfilename)
      : ofstream_holder(std::move(dbfilename)), stream_writer<ValueType>(ofstream_) {
    if (!ofstream_.is_open()) throw std::domain_error("cannot open db: " + filename_);
  }
};

// CAUTION: flat_file::database ALWAYS INVALIDATES its iterators during move assignment.
// This can be surprising as this is UNLIKE the STL containers.
template <typename ValueType>
class database {

  static_assert(std::is_trivially_copyable_v<ValueType>);
  static_assert(std::is_standard_layout_v<ValueType>);

public:
  explicit database(std::string filename, std::size_t buf_size = 1)
      : filename_(std::move(filename)), dbfsize_(std::filesystem::file_size(filename_)),
        db_(filename_, std::ios::binary), buf_(buf_size) {

    static_assert(std::is_move_assignable_v<database>); // but that invalidates iterators!

    if (dbfsize_ % sizeof(ValueType) != 0)
      throw std::ios::failure("db file size is not a multiple of the record size");

    dbsize_ = dbfsize_ / sizeof(ValueType);

    if (!db_.is_open()) throw std::ios::failure("cannot open db: " + filename_);

    db_.exceptions(std::ios::badbit | std::ios::failbit); // throw on any future errors
  }

  using value_type = ValueType;
  struct const_iterator;

  const ValueType& get_record(std::size_t pos) {
    if (!(pos >= buf_start_ && pos < buf_end_)) {
      db_.seekg(static_cast<long>(pos * sizeof(ValueType)));

      std::size_t nrecs = std::min(buf_.size(), dbsize_ - pos);

      db_.read(reinterpret_cast<char*>(buf_.data()), // NOLINT reinterpret_cast
               static_cast<std::streamsize>(sizeof(ValueType) * nrecs));

      buf_start_ = pos;
      buf_end_   = pos + nrecs;
    }
    return buf_[pos - buf_start_];
  }

  const_iterator begin() { return {*this, 0}; }
  const_iterator end() {
    return {*this, dbsize_};
  }

  const ValueType& back() {
    return get_record(dbsize_ - 1); // using iter here does not work (lifetime issue?)
  }
  
  std::string filename() const { return filename_; }
  std::size_t filesize() const { return dbfsize_; }
  std::size_t number_records() const { return dbsize_; }

  template <typename Comp = std::less<>, typename Proj = std::identity>
  std::string disksort(Comp comp = {}, Proj proj = {},
                       std::size_t max_memory_usage = 1'000'000'000);

private:
  std::string            filename_;
  std::size_t            dbfsize_;
  std::size_t            dbsize_;
  std::ifstream          db_;
  std::size_t            buf_start_ = 0;
  std::size_t            buf_end_   = 0; // one past the end
  std::vector<ValueType> buf_;
};

template <typename ValueType>
struct database<ValueType>::const_iterator {
  using iterator_category = std::random_access_iterator_tag;
  using difference_type   = std::ptrdiff_t;
  using value_type        = ValueType;
  using pointer           = const ValueType*;
  using reference         = const ValueType&;

  const_iterator(database<ValueType>& ffdb, std::size_t pos) : ffdb_(&ffdb), pos_(pos) {}

  // clang-format off
  reference operator*() const { return current(); }
  pointer   operator->() const { current(); return &cur_; }

  bool operator==(const const_iterator& other) const { return ffdb_ == other.ffdb_ && pos_ == other.pos_; }
  
  const_iterator& operator++() { return *this += 1; }
  const_iterator operator++(int) { const_iterator tmp = *this; ++(*this); return tmp; } // NOLINT why const?
  const_iterator& operator--() { return *this -= 1; }
  const_iterator operator--(int) { const_iterator tmp = *this; --(*this); return tmp; } // NOLINT why const?

  const_iterator& operator+=(std::size_t offset) { set_pos(pos_ + offset); return *this; }
  const_iterator& operator-=(std::size_t offset) { set_pos(pos_ - offset); return *this; }

  friend const_iterator operator+(const_iterator iter, std::size_t offset) { return iter += offset; }
  friend const_iterator operator+(std::size_t offset, const_iterator iter) { return iter += offset; }
  friend const_iterator operator-(const_iterator iter, std::size_t offset) { return iter -= offset; }
  friend difference_type operator-(const const_iterator& a, const const_iterator& b) {
      return static_cast<difference_type>(a.pos_ - b.pos_);
  }
  // clang-format on

  std::size_t pos() { return pos_; }
  std::string filename() { return ffdb_->filename(); }

private:
  database*   ffdb_ = nullptr;
  std::size_t pos_{};
  // cur_ and cur_valid_ are mutable so that operator* can be const
  mutable value_type cur_;
  mutable bool       cur_valid_ = false;

  void set_pos(std::size_t pos) {
    pos_       = pos;
    cur_valid_ = false;
  }

  reference current() const {
    if (!cur_valid_) {
      cur_       = ffdb_->get_record(pos_);
      cur_valid_ = true;
    }
    return cur_;
  }
};

template <typename ValueType, typename Comp = std::less<>, typename Proj = std::identity>
std::vector<std::string> sort_into_chunks(typename database<ValueType>::const_iterator first,
                                          typename database<ValueType>::const_iterator last,
                                          Comp comp = {}, Proj proj = {},
                                          std::size_t max_memory_usage = 1'000'000'000) {

  auto        records_to_sort = static_cast<std::size_t>(last - first);
  std::size_t chunk_size      = std::min(records_to_sort, max_memory_usage / sizeof(ValueType));
  std::size_t number_of_chunks =
      (records_to_sort / chunk_size) + static_cast<std::size_t>(records_to_sort % chunk_size != 0);

  std::cerr << std::format("{:20s} = {:12d}\n", "max memory usage", max_memory_usage);
  std::cerr << std::format("{:20s} = {:12d}\n", "records to sort", records_to_sort);
  std::cerr << std::format("{:20s} = {:12d}\n", "chunk size", chunk_size);
  std::cerr << std::format("{:20s} = {:12d}\n", "number of chunks", number_of_chunks) << "\n";

  std::vector<std::string> chunk_filenames;
  chunk_filenames.reserve(number_of_chunks);
  for (std::size_t chunk = 0; chunk != number_of_chunks; ++chunk) {
    std::string chunk_filename = first.filename() + std::format(".partial.{:04d}", chunk);
    chunk_filenames.push_back(chunk_filename);

    std::size_t start = chunk * chunk_size;
    std::size_t end   = start + std::min(chunk_size, records_to_sort - start);
    std::cerr << std::format("sorting [{:12d},{:12d}) => {:s}\n", start, end, chunk_filename);

    std::vector<ValueType> objs;
    std::copy(first + start, first + end, std::back_inserter(objs));
    std::sort(
#if __cpp_lib_parallel_algorithm
        // it is also possible to use std::sort(par_unseq from PTSL in libc++ with
        // -fexperimental-library
        std::execution::par_unseq,
#endif
        objs.begin(), objs.end(), [&](const auto& a, const auto& b) {
          return comp(std::invoke(proj, a), std::invoke(proj, b));
        });
    auto part = file_writer<ValueType>(chunk_filename);
    for (const auto& obj: objs) part.write(obj);
  }
  return chunk_filenames;
}

template <typename ValueType, typename Comp = std::less<>, typename Proj = std::identity>
void merge_sorted_chunks(const std::vector<std::string>& chunk_filenames,
                         const std::string& sorted_filename, Comp comp = {}, Proj proj = {}) {

  static_assert(std::is_invocable_v<Proj, ValueType>);

  struct chunk {
    flat_file::database<ValueType>                          db;
    typename flat_file::database<ValueType>::const_iterator current;
    typename flat_file::database<ValueType>::const_iterator end;

    explicit chunk(std::string filename, std::size_t buf_size)
        : db(std::move(filename), buf_size), current(db.begin()), end(db.end()) {}
  };

  std::vector<chunk> chunks;
  // MUST reserve this to avoid invalidating flat_file::iterators
  chunks.reserve(chunk_filenames.size());
  for (const auto& filename: chunk_filenames) chunks.emplace_back(filename, 1000);

  struct head {
    ValueType   value;
    std::size_t idx; // index into chunks to know where I came from
  };

  // Comparator for the priority queue. Negation because priority queue is "max" by default.
  auto cmp = [&](const auto& a, const auto& b) {
    return !comp(std::invoke(proj, a.value), std::invoke(proj, b.value));
  };

  std::priority_queue<head, std::vector<head>, decltype(cmp)> heads(cmp);

  // prime the queue
  for (std::size_t i = 0; i != chunks.size(); ++i) {
    heads.push({*(chunks[i].current), i});
    ++(chunks[i].current);
  }

  auto sorted = flat_file::file_writer<ValueType>(sorted_filename);
  while (!heads.empty()) {
    const head& t = heads.top();
    sorted.write(t.value);
    std::size_t chunk_idx = t.idx;
    heads.pop();
    if (auto& chunk = chunks[chunk_idx]; chunk.current != chunk.end) {
      heads.push({*(chunk.current), chunk_idx});
      ++(chunk.current);
    }
  }

  for (const auto& filename: chunk_filenames) std::filesystem::remove(filename);
}

template <typename ValueType, typename Comp = std::less<>, typename Proj = std::identity>
std::string disksort_range(typename database<ValueType>::const_iterator first,
                           typename database<ValueType>::const_iterator last, Comp comp = {},
                           Proj proj = {}, std::size_t max_memory_usage = 1'000'000'000) {

  std::vector<std::string> chunk_filenames =
      sort_into_chunks<ValueType>(first, last, comp, proj, max_memory_usage);

  std::string sorted_filename = first.filename() + ".sorted";

  if (chunk_filenames.size() == 1) {
    std::filesystem::rename(chunk_filenames[0], sorted_filename);
    std::cerr << std::format("\nrenaming {} => {}\n", chunk_filenames[0], sorted_filename);
  } else {
    std::cerr << std::format("\nmerging [{:12d},{:12d}) => {:s}\n", first.pos(), last.pos(),
                             sorted_filename);
    merge_sorted_chunks<ValueType>(chunk_filenames, sorted_filename, comp, proj);
  }
  return sorted_filename;
}

template <typename ValueType>
template <typename Comp, typename Proj>
std::string database<ValueType>::disksort(Comp comp, Proj proj, std::size_t max_memory_usage) {
  return disksort_range<ValueType>(begin(), end(), comp, proj, max_memory_usage);
}

} // namespace flat_file
