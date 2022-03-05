#pragma once

#include "fmt/format.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <numeric>
#include <ranges>
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
    explicit stream_writer(std::ostream& os, std::size_t buf_size = 100)
        : db_(os), buf_(buf_size) {}

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

    // a "scoped manager" .. no special members other than destruct
    stream_writer(const stream_writer& other) = delete;
    stream_writer(stream_writer&& other)      = delete;
    stream_writer& operator=(const stream_writer& other) = delete;
    stream_writer& operator=(stream_writer&& other) = delete;
    ~stream_writer() { flush(); }

  private:
    std::ostream&          db_;
    std::size_t            buf_pos_ = 0;
    std::vector<ValueType> buf_;
};

template <typename ValueType>
class writer : private impl::ofstream_holder, public stream_writer<ValueType> {
  public:
    explicit writer(std::string dbfilename)
        : ofstream_holder(std::move(dbfilename)), stream_writer<ValueType>(ofstream_) {
        if (!ofstream_.is_open()) throw std::domain_error("cannot open db: " + filename_);
    }
};

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
            throw std::domain_error("db file size is not a multiple of the record size");

        dbsize_ = dbfsize_ / sizeof(ValueType);

        if (!db_.is_open()) throw std::domain_error("cannot open db: " + filename_);
    }

    using value_type = ValueType;
    struct const_iterator;

    void get_record(std::size_t pos, ValueType& rec) {
        if (!(pos >= buf_start_ && pos < buf_end_)) {
            db_.seekg(static_cast<long>(pos * sizeof(ValueType)));
            std::size_t nrecs = std::min(buf_.size(), dbsize_ - pos + 1);
            db_.read(reinterpret_cast<char*>(buf_.data()), // NOLINT reinterpret_cast
                     static_cast<std::streamsize>(sizeof(ValueType) * nrecs));
            buf_start_ = pos;
            buf_end_   = pos + nrecs;
        }
        rec = buf_[pos - buf_start_];
    }

    const_iterator begin() { return {*this, 0}; }
    const_iterator end() { return {*this, dbsize_}; }

    std::string filename() const { return filename_; }
    std::size_t filesize() const { return dbfsize_; }
    std::size_t number_records() const { return dbsize_; }

    template <typename Comp = std::less<>, typename Proj = std::identity>
    void sort(Comp comp = {}, Proj proj = {});

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
        value_type operator*() { return current(); }
        pointer    operator->() { current(); return &cur_; }

        bool operator==(const const_iterator& other) const { return ffdb_ == other.ffdb_ && pos_ == other.pos_; }
        
        const_iterator& operator++() { set_pos(pos_ + 1); return *this; }
        const_iterator operator++(int) { const_iterator tmp = *this; ++(*this); return tmp; } // NOLINT why const?
        const_iterator& operator--() { set_pos(pos_ - 1); return *this; }
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

  private:
    database*   ffdb_ = nullptr;
    std::size_t pos_{};
    ValueType   cur_;
    bool        cur_valid_ = false;

    void set_pos(std::size_t pos) {
        pos_       = pos;
        cur_valid_ = false;
    }

    ValueType& current() {
        if (!cur_valid_) {
            ffdb_->get_record(pos_, cur_);
            cur_valid_ = true;
        }
        return cur_;
    }
};

namespace impl {

template <typename ValueType, typename Comp = std::less<>, typename Proj = std::identity>
std::vector<std::string> sort_into_chunks(database<ValueType>& db, std::size_t records_to_sort,
                                          std::size_t number_of_chunks, std::size_t chunk_size,
                                          Comp comp = {}, Proj proj = {}) {

    std::vector<std::string> chunk_filenames;
    chunk_filenames.reserve(number_of_chunks);
    for (std::size_t chunk = 0; chunk != number_of_chunks; ++chunk) {
        std::string chunk_filename = db.filename() + ".partial." + fmt::format("{:06d}", chunk);
        chunk_filenames.push_back(chunk_filename);

        std::size_t start = chunk * chunk_size;
        std::size_t end   = chunk * chunk_size + std::min(chunk_size, records_to_sort - start);
        std::cerr << fmt::format("sorting [{:12d},{:12d}) => {:s}\n", start, end, chunk_filename);

        std::vector<ValueType> ppws;
        std::copy(db.begin() + start, db.begin() + end, std::back_inserter(ppws));
        std::ranges::sort(ppws, comp, proj);
        auto part = writer<ValueType>(chunk_filename);
        for (const auto& ppw: ppws) part.write(ppw);
    }
    return chunk_filenames;
}

template <typename ValueType, typename Comp = std::less<>, typename Proj = std::identity>
void merge_chunks(const std::vector<std::string>& chunk_filenames,
                  const std::string& sorted_filename, Comp comp = {}, Proj proj = {}) {

    static_assert(std::is_invocable_v<Proj, ValueType>);

    struct partial {
        flat_file::database<ValueType>                          db;
        typename flat_file::database<ValueType>::const_iterator current;
        typename flat_file::database<ValueType>::const_iterator end;

        explicit partial(std::string filename, std::size_t bufsize = 1)
            : db(std::move(filename), bufsize), current(db.begin()), end(db.end()) {}
    };

    // Don't use a std::vector<partial>! This will use move assignment of partials(and
    // therefore the flat_file) during re-allocaton which WILL INVALIDATE the iterators and
    // cause UB! flat_file ALWAYS INVALIDATES its iterators during move assignment. This can
    // be surprising and is UNLIKE other STL containers So we use a std::list<partials> or
    // alternatively a std::vector<std::unique_ptr<partial>>
    std::list<partial> partials;
    for (const auto& filename: chunk_filenames) partials.emplace_back(filename, 1000);

    auto records_to_sort =
        std::accumulate(partials.begin(), partials.end(), 0UL,
                        [](std::size_t sum, auto& p) { return sum + p.db.number_records(); });

    std::cerr << fmt::format("\nmerging [{:12d},{:12d}) => {:s}\n", 0, records_to_sort,
                             sorted_filename);

    auto sorted = flat_file::writer<ValueType>(sorted_filename);
    while (!partials.empty()) {
        auto min = std::ranges::min_element(
            partials, comp, [&](auto& partial) { return std::invoke(proj, *partial.current); });

        sorted.write(*(min->current));
        ++(min->current);

        if (min->current == min->end) partials.erase(min);
    }
    // for (const auto& filename: chunk_filenames)
    //     std::filesystem::remove(std::filesystem::path(filename));
}

} // namespace impl

template <typename ValueType>
template <typename Comp, typename Proj>
void database<ValueType>::sort(Comp comp, Proj proj) {
    const std::size_t max_memory_usage = 100; // ~1GB

    std::size_t records_to_sort = std::min(number_records(), 10UL); // limited for debug
    // std::size_t records_to_sort = db.number_records();

    std::size_t chunk_size = std::min(records_to_sort, max_memory_usage / sizeof(ValueType));

    std::size_t number_of_chunks = (records_to_sort / chunk_size) +
                                   static_cast<std::size_t>(records_to_sort % chunk_size != 0);

    std::cerr << fmt::format("{:20s} = {:12d}\n", "max_memory_usage", max_memory_usage);
    std::cerr << fmt::format("{:20s} = {:12d}\n", "records_to_sort", records_to_sort);
    std::cerr << fmt::format("{:20s} = {:12d}\n", "chunk_size", chunk_size);
    std::cerr << fmt::format("{:20s} = {:12d}\n", "number_of_chunks", number_of_chunks) << "\n";

    std::vector<std::string> chunk_filenames =
        impl::sort_into_chunks(*this, records_to_sort, number_of_chunks, chunk_size, comp, proj);

    std::string sorted_filename = filename() + ".sorted";

    if (number_of_chunks == 1) {
        std::filesystem::rename(chunk_filenames[0], sorted_filename);
        std::cerr << fmt::format("\nrenaming {:s} => {:s}\n", chunk_filenames[0], sorted_filename);
    } else {
        impl::merge_chunks<ValueType>(chunk_filenames, sorted_filename, comp, proj);
    }
}

} // namespace flat_file
