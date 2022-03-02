#pragma once

#include "fmt/core.h"
#include "os/algo.hpp"
#include "os/str.hpp"
#include <array>
#include <cassert>
#include <compare>
#include <cstddef>
#include <ostream>

namespace hibp {

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

inline std::byte make_nibble(char nibblechr) {
    auto nibble = nibblechr - '0';
    if (nibble > 9) nibble = (nibble & ~('a' - 'A')) - ('A' - '0') + 10; // NOLINT signed
    assert(nibble >= 0 and nibble <= 15);
    return static_cast<std::byte>(nibble);
}

inline std::byte make_byte(char mschr, char lschr) {
    return make_nibble(mschr) << 4U | make_nibble(lschr);
}

// `text` must be an uppper- or lowercase sha1 hexstr
// with optional ":123" appended (123 is the count).
inline pawned_pw convert_to_binary(const std::string& text) {
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
inline void build_bin_db(std::istream& text_stream, std::ostream& binary_stream) {
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

} // namespace hibp
