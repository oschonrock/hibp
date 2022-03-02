#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>


template <typename ValueType>
class flat_file_db {

    static_assert(std::is_trivially_copyable_v<ValueType>);
    static_assert(std::is_standard_layout_v<ValueType>);

  public:
    explicit flat_file_db(std::string dbfilename)
        : dbfilename_(std::move(dbfilename)), dbpath_(dbfilename_),
          dbfsize_(std::filesystem::file_size(dbpath_)), db_(dbpath_, std::ios::binary) {

        if (dbfsize_ % sizeof(ValueType) != 0)
            throw std::domain_error("db file size is not a multiple of the record size");

        dbsize_ = dbfsize_ / sizeof(ValueType);

        if (!db_.is_open()) throw std::domain_error("cannot open db: " + std::string(dbpath_));
    }

    struct iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = ValueType;
        using pointer           = ValueType*;
        using reference         = ValueType&;

        iterator(std::ifstream& db, std::size_t pos) : db_(&db), pos_(pos) {}

        // clang-format off
        value_type operator*() { return current(); }
        pointer operator->() { current(); return &cur_; }
        
        bool operator==(const iterator& other) const { return db_ == other.db_ && pos_ == other.pos_; }
        
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
        std::ifstream* db_ = nullptr; // using a reference would not work for copy assignment etc
        std::size_t    pos_{};
        ValueType      cur_;
        bool           cur_valid_ = false;

        void set_pos(std::size_t pos) {
            pos_       = pos;
            cur_valid_ = false;
        }

        ValueType& current() {
            if (!cur_valid_) {
                db_->seekg(static_cast<long>(pos_ * sizeof(ValueType)));
                db_->read(reinterpret_cast<char*>(&cur_), // NOLINT reinterpret_cast
                          sizeof(ValueType));
                cur_valid_ = true;
            }
            return cur_;
        }
    };

    iterator begin() { return iterator(db_, 0); }
    iterator end() { return iterator(db_, dbsize_); }

  private:
    std::string           dbfilename_;
    std::filesystem::path dbpath_;
    std::size_t           dbfsize_;
    std::size_t           dbsize_;
    std::ifstream         db_;
};

