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

inline std::byte make_byte(const char* two_chrs) {
    return make_byte(*two_chrs, *(two_chrs + 1));
}

// `text` must be an uppper- or lowercase sha1 hexstr
// with optional ":123" appended (123 is the count).
inline pawned_pw convert_to_binary(const std::string& text) {
    pawned_pw ppw; // NOLINT initlialisation not needed here

    assert(text.length() >= ppw.hash.size() * 2);
    for (auto [i, b]: os::algo::enumerate(ppw.hash)) // note b is by reference!
        b = make_byte(&text[2 * i]);

    if (text.size() > ppw.hash.size() * 2 + 1)
        ppw.count = os::str::parse_nonnegative_int(text.c_str() + ppw.hash.size() * 2 + 1,
                                                   text.c_str() + text.size(), -1);
    else
        ppw.count = -1;

    return ppw;
}

} // namespace hibp
