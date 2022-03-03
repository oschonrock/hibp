#pragma once

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

template <typename ValueType>
class flat_file_writer {

  public:
    explicit flat_file_writer(std::string dbfilename, std::size_t buf_size = 100)
        : dbfilename_(std::move(dbfilename)), dbpath_(dbfilename_), db_(dbpath_, std::ios::binary),
          buf_(buf_size) {
        if (!db_.is_open()) throw std::domain_error("cannot open db: " + std::string(dbpath_));
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

  private:
    std::string            dbfilename_;
    std::filesystem::path  dbpath_;
    std::ofstream          db_;
    std::size_t            buf_pos_ = 0;
    std::vector<ValueType> buf_;
};

template <typename ValueType>
class flat_file {

    static_assert(std::is_trivially_copyable_v<ValueType>);
    static_assert(std::is_standard_layout_v<ValueType>);

  public:
    explicit flat_file(std::string dbfilename, std::size_t buf_size = 1)
        : dbfilename_(std::move(dbfilename)), dbpath_(dbfilename_),
          dbfsize_(std::filesystem::file_size(dbpath_)), db_(dbpath_, std::ios::binary),
          buf_(buf_size) {

        if (dbfsize_ % sizeof(ValueType) != 0)
            throw std::domain_error("db file size is not a multiple of the record size");

        dbsize_ = dbfsize_ / sizeof(ValueType);

        if (!db_.is_open()) throw std::domain_error("cannot open db: " + std::string(dbpath_));
    }

    using value_type = ValueType;

    struct iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = ValueType;
        using pointer           = ValueType*;
        using reference         = ValueType&;

        iterator(flat_file<ValueType>& ffdb, std::size_t pos) : ffdb_(&ffdb), pos_(pos) {}

        // clang-format off
        value_type operator*() { return current(); }
        pointer operator->() { current(); return &cur_; }
        
        bool operator==(const iterator& other) const { return ffdb_ == other.ffdb_ && pos_ == other.pos_; }
        
        iterator& operator++() { set_pos(pos_ + 1); return *this; }
        iterator operator++(int) { iterator tmp = *this; ++(*this); return tmp; } // NOLINT why const?
        iterator& operator--() { set_pos(pos_ - 1); return *this; }
        iterator operator--(int) { iterator tmp = *this; --(*this); return tmp; } // NOLINT why const?

        iterator& operator+=(std::size_t offset) { set_pos(pos_ + offset); return *this; }
        iterator& operator-=(std::size_t offset) { set_pos(pos_ - offset); return *this; }
        // clang-format on

        friend iterator operator+(iterator iter, std::size_t offset) { return iter += offset; }
        friend iterator operator+(std::size_t offset, iterator iter) { return iter += offset; }
        friend iterator operator-(iterator iter, std::size_t offset) { return iter -= offset; }
        friend difference_type operator-(const iterator& a, const iterator& b) {
            return static_cast<difference_type>(a.pos_ - b.pos_);
        }

      private:
        flat_file<ValueType>* ffdb_ = nullptr;
        std::size_t           pos_{};
        ValueType             cur_;
        bool                  cur_valid_ = false;

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

    iterator begin() { return iterator(*this, 0); }
    iterator end() { return iterator(*this, dbsize_); }

    std::size_t filesize() const { return dbfsize_; }
    std::size_t number_records() const { return dbsize_; }


  private:
    std::string            dbfilename_;
    std::filesystem::path  dbpath_;
    std::size_t            dbfsize_;
    std::size_t            dbsize_;
    std::ifstream          db_;
    std::size_t            buf_start_ = 0;
    std::size_t            buf_end_   = 0; // one past the end
    std::vector<ValueType> buf_;
};
