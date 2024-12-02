#pragma once

#include "arrcmp.hpp"
#include <array>
#include <cassert>
#include <charconv>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <ostream>
#include <string>
#include <string_view>

namespace hibp {

template <unsigned HashSize>
struct pawned_pw;

namespace detail {

constexpr std::byte make_nibble(char c) {
  assert((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'));
  auto nibble = c - '0';
  if (nibble > 9) nibble = (nibble & ~('a' - 'A')) - ('A' - '0') + 10; // NOLINT signed
  assert(nibble >= 0 && nibble <= 15);
  return static_cast<std::byte>(nibble);
}

constexpr std::byte make_byte(char mschr, char lschr) {
  return make_nibble(mschr) << 4U | make_nibble(lschr);
}

constexpr std::byte make_byte(const char* two_chrs) {
  return make_byte(*two_chrs, *(two_chrs + 1));
}

constexpr char nibble_to_char(std::byte nibble) {
  auto n = std::to_integer<uint8_t>(nibble);
  assert(n <= 15);
  return static_cast<char>(n + (n < 10 ? '0' : 'A' - 10));
}
} // namespace detail

template <unsigned HashSize>
struct pawned_pw {
  constexpr static unsigned hash_size       = HashSize;
  constexpr static unsigned hash_str_size   = hash_size * 2;
  constexpr static unsigned prefix_str_size = 5;
  constexpr static unsigned suffix_str_size = hash_str_size - prefix_str_size;

  pawned_pw() = default;

  explicit pawned_pw(const std::string& text) {
    assert(text.length() >= hash_str_size);
    std::size_t i = 0;
    for (auto& b: hash) {
      b = detail::make_byte(&text[2 * i]);
      ++i;
    }

    count          = -1;
    auto count_idx = hash_str_size + 1;
    if constexpr (HashSize == 8) { // special case for sha1t64
      // is this a sha1 hash? 
      if (text.size() > hash_str_size && text[hash_str_size] != ':' && text.size() >= 20UL * 2) {
        count_idx = 20UL * 2 + 1; // skip foward to the count at end of sha1 hash
      }
    }
    if (text.size() > count_idx) {
      std::from_chars(text.c_str() + count_idx, text.c_str() + text.size(), count);
    }
  }

  std::strong_ordering operator<=>(const pawned_pw& rhs) const {
    if constexpr (HashSize == 8) { // alignment problems => fallback
      return hash <=> rhs.hash;
    } else {
      return arrcmp::array_compare(hash, rhs.hash, arrcmp::three_way{});
    }
  }

  bool operator==(const pawned_pw& rhs) const {
    if constexpr (HashSize == 8) { // alignment problems => fallback
      return hash == rhs.hash;
    } else {
      return arrcmp::array_compare(hash, rhs.hash, arrcmp::equal{});
    }
  }

  [[nodiscard]] std::string to_string() const {
    std::string buffer(60, '\0');
    char*       strptr = buffer.data();
    for (auto h: hash) {
      *strptr++ = detail::nibble_to_char(h >> 4U);
      *strptr++ = detail::nibble_to_char(h & std::byte(0x0FU));
    }
    *strptr++      = ':';
    auto [ptr, ec] = std::to_chars(strptr, buffer.data() + buffer.size(), count);
    buffer.resize(static_cast<std::size_t>(ptr - buffer.data()));
    return buffer;
  }

  friend std::ostream& operator<<(std::ostream& os, const pawned_pw& rhs) {
    return os << rhs.to_string();
  }

  std::array<std::byte, HashSize> hash{};
  std::int32_t                    count = -1; // important to be definitive about size
};

using pawned_pw_sha1    = pawned_pw<20>;
using pawned_pw_ntlm    = pawned_pw<16>;
using pawned_pw_sha1t64 = pawned_pw<8>;

template <typename T>
concept pw_type = std::is_same_v<T, pawned_pw_sha1> || std::is_same_v<T, pawned_pw_ntlm> ||
                  std::is_same_v<T, pawned_pw_sha1t64>;

template <pw_type PwType>
inline bool is_valid_hash(const std::string& hash) {
  return hash.size() == PwType::hash_size * 2 &&
         hash.find_first_not_of("0123456789ABCDEF") == std::string_view::npos;
}

template <pw_type PwType>
inline std::string url(const std::string& prefix_str, bool testing) {
  std::string server_path =
      testing ? "http://localhost:8090" : "https://api.pwnedpasswords.com/range";
  std::string url = fmt::format("{}/{}", server_path, prefix_str);
  if constexpr (std::is_same_v<PwType, pawned_pw_ntlm>) {
    url += "?mode=ntlm";
  }
  return url;
}

template <pw_type PwType>
inline std::string url(unsigned prefix, bool testing) {
  return url<PwType>(fmt::format("{:05X}", prefix), testing);
}

// runtime url selector
inline std::string url(const std::string& prefix_str, bool ntlm, bool testing) {
  if (ntlm) {
    return url<pawned_pw_ntlm>(prefix_str, testing);
  }
  return url<pawned_pw_sha1>(prefix_str, testing);
}
} // namespace hibp
