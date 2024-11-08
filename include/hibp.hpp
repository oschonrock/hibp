#pragma once

#include "arrcmp.hpp"
#include <array>
#include <cassert>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <ostream>

namespace hibp {

struct pawned_pw;

inline pawned_pw convert_to_binary(const std::string& text);

struct pawned_pw {
  pawned_pw() = default;
  
  pawned_pw(const std::string& text) : pawned_pw(hibp::convert_to_binary(text)) {} // NOLINT implicit conversion

  std::strong_ordering operator<=>(const pawned_pw& rhs) const {
    return arrcmp::array_compare(hash, rhs.hash, arrcmp::three_way{});
  }

  bool operator==(const pawned_pw& rhs) const {
    return arrcmp::array_compare(hash, rhs.hash, arrcmp::equal{});
  }

  friend std::ostream& operator<<(std::ostream& os, const pawned_pw& rhs) {
    for (auto&& b: rhs.hash) os << std::format("{:02X}", static_cast<unsigned>(b));
    return os << std::format(":{:d}", rhs.count);
  }

  std::array<std::byte, 20> hash;
  std::int32_t              count; // important to be definitive about size
};

constexpr inline std::byte make_nibble(char nibblechr) {
  auto nibble = nibblechr - '0';
  if (nibble > 9) nibble = (nibble & ~('a' - 'A')) - ('A' - '0') + 10; // NOLINT signed
  assert(nibble >= 0 && nibble <= 15);                                 // NOLINT assert array to ptr
  return static_cast<std::byte>(nibble);
}

constexpr inline std::byte make_byte(char mschr, char lschr) {
  return make_nibble(mschr) << 4U | make_nibble(lschr);
}

constexpr inline std::byte make_byte(const char* two_chrs) {
  return make_byte(*two_chrs, *(two_chrs + 1));
}

// `text` must be an uppper- or lowercase sha1 hexstr
// with optional ":123" appended (123 is the count).
inline pawned_pw convert_to_binary(const std::string& text) {
  pawned_pw ppw; // NOLINT initlialisation not needed here

  assert(text.length() >= ppw.hash.size() * 2); // NOLINT assert array to ptr
  std::size_t i = 0;
  for (auto& b: ppw.hash) {
    b = make_byte(&text[2 * i]);
    ++i;
  }

  ppw.count = -1;
  if (text.size() > ppw.hash.size() * 2 + 1) {
    std::from_chars(text.c_str() + ppw.hash.size() * 2 + 1, text.c_str() + text.size(), ppw.count);
  }
  return ppw;
}

} // namespace hibp
