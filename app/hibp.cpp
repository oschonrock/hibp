#include "fmt/core.h"
#include "os/algo.hpp"
#include "os/bch.hpp"
#include "os/str.hpp"
#include "sha1.hpp"
#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace hibp {

std::byte make_nibble(char nibblechr) {
    auto nibble = nibblechr - '0';
    if (nibble > 9) nibble = (nibble & ~('a' - 'A')) - ('A' - '0') + 10; // NOLINT signed
    assert(nibble >= 0 and nibble <= 15);
    return static_cast<std::byte>(nibble);
}

std::byte make_byte(char mschr, char lschr) {
    return make_nibble(mschr) << 4U | make_nibble(lschr);
}

struct pawned_pw {
    bool operator==(const pawned_pw& rhs) const { return hash == rhs.hash; }

    std::strong_ordering operator<=>(const pawned_pw& rhs) const { return hash <=> rhs.hash; }

    friend std::ostream& operator<<(std::ostream& os, const pawned_pw& rhs) {
        for (auto&& b: rhs.hash) os << fmt::format("{:02X}", b);
        return os << fmt::format(":{:d}", rhs.count);
    }

    std::array<std::byte, 20> hash;
    std::int32_t              count; // important to be definitive about size
};

// `text` must be an uppper- or lowercase sha1 hexstr
// with optional ":123" appended (123 is the count).
pawned_pw convert_to_binary(const std::string& text) {
    pawned_pw ppw; // NOLINT initlialisation not needed here

    assert(text.length() >= ppw.hash.size() * 2);
    for (auto [i, b]: os::algo::enumerate(ppw.hash)) // note b is by reference!
        b = make_byte(text[2 * i], text[2 * i + 1]);

    if (text.size() > ppw.hash.size() * 2 + 1)
        ppw.count = os::str::parse_nonnegative_int(text.c_str() + ppw.hash.size() * 2 + 1,
                                                   text.c_str() + text.size(), -1);
    else
        ppw.count = -1;

    return ppw;
}

// convert text file to binary. high throughput > ~230MB/s dependant on disk
void build_bin_db(std::istream& text_stream, std::ostream& binary_stream) {
    std::ios_base::sync_with_stdio(false);
    // std::getline is about 2x faster than `text_stream >> line` here

    // Using std::fwrite is about 8x faster than std::ostream::write.
    // But only if writing 1 password at a time. Instead buffer 100 of them.
    constexpr std::size_t          obufcnt = 100;
    std::array<pawned_pw, obufcnt> obuf{};
    std::size_t                    obufpos = 0;
    for (std::string line; std::getline(text_stream, line);) {

        pawned_pw ppw = convert_to_binary(line);
        if (obufpos == obuf.size()) {
            binary_stream.write(reinterpret_cast<char*>(&obuf), // NOLINT reincast
                                static_cast<std::streamsize>(sizeof(pawned_pw) * obuf.size()));
            obufpos = 0;
        }
        std::memcpy(&obuf[obufpos], &ppw, sizeof(ppw));
        ++obufpos;
    }
    if (obufpos > 0)
        binary_stream.write(reinterpret_cast<char*>(&obuf), // NOLINT reincast
                            static_cast<std::streamsize>(sizeof(pawned_pw) * obufpos));
}

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

} // namespace hibp

int main(int argc, char* argv[]) {

    // build db
    // hibp::build_bin_db(std::cin, std::cout);
    // return 0;

    try {
        if (argc < 3)
            throw std::domain_error("USAGE: " + std::string(argv[0]) +
                                    " dbfile.bin plaintext_password");

        hibp::flat_file_db<hibp::pawned_pw> db(argv[1]);

        SHA1 sha1;
        sha1.update(argv[2]);
        hibp::pawned_pw needle = hibp::convert_to_binary(sha1.final());

        std::optional<hibp::pawned_pw> maybe_ppw;

        {
            os::bch::Timer t("search took");
            if (auto iter = std::lower_bound(db.begin(), db.end(), needle);
                iter != db.end() && *iter == needle) {
                maybe_ppw = *iter;
            } else {
                maybe_ppw = std::nullopt;
            }
        }

        std::cout << "needle = " << needle << "\n";
        if (maybe_ppw)
            std::cout << "found  = " << *maybe_ppw << "\n";
        else
            std::cout << "not found\n";

    } catch (const std::exception& e) {
        std::cerr << "something went wrong: " << e.what() << "\n";
    }

    return EXIT_SUCCESS;
}
