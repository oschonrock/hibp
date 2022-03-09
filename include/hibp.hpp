#pragma once

#include "fmt/format.h"
#include "os/algo.hpp"
#include "os/str.hpp"
#include <array>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>
#include <ios>
#include <ostream>
#include <pstl/utils.h>

namespace hibp {

struct pawned_pw {
  std::strong_ordering operator<=>(const pawned_pw& rhs) const {
    // ALERT: not entirely portable (yet?)
    // this compiles to a load and `bswap` which should be fast
    // measured > 33% faster than hash < rhs.hash, which compiles to `memcmp`

    static_assert(sizeof(std::uint64_t) == 8);
    static_assert(sizeof(std::uint32_t) == 4);

    // c++23 will have std::byteswap
#ifdef MSVC
  #define BYTE_SWAP_32 _byteswap_ulong
  #define BYTE_SWAP_64 _byteswap_uint64
#else
  #define BYTE_SWAP_32 __builtin_bswap32
  #define BYTE_SWAP_64 __builtin_bswap64
#endif

    std::uint64_t head     = BYTE_SWAP_64(*(std::uint64_t*)(&hash[0]));     // NOLINT
    std::uint64_t rhs_head = BYTE_SWAP_64(*(std::uint64_t*)(&rhs.hash[0])); // NOLINT
    if (head != rhs_head) return head <=> rhs_head;

    std::uint64_t mid     = BYTE_SWAP_64(*(std::uint64_t*)(&hash[8]));     // NOLINT
    std::uint64_t rhs_mid = BYTE_SWAP_64(*(std::uint64_t*)(&rhs.hash[8])); // NOLINT
    if (mid != rhs_mid) return mid <=> rhs_mid;

    std::uint32_t tail     = BYTE_SWAP_32(*(std::uint32_t*)(&hash[16]));     // NOLINT
    std::uint32_t rhs_tail = BYTE_SWAP_32(*(std::uint32_t*)(&rhs.hash[16])); // NOLINT
    return tail <=> rhs_tail;
  }

  bool operator==(const pawned_pw& rhs) const {
    static_assert(sizeof(std::uint64_t) == 8);
    static_assert(sizeof(std::uint32_t) == 4);

    std::uint64_t head     = *(std::uint64_t*)(&hash[0]);     // NOLINT
    std::uint64_t rhs_head = *(std::uint64_t*)(&rhs.hash[0]); // NOLINT
    if (head != rhs_head) return false;

    std::uint64_t mid     = *(std::uint64_t*)(&hash[8]);     // NOLINT
    std::uint64_t rhs_mid = *(std::uint64_t*)(&rhs.hash[8]); // NOLINT
    if (mid != rhs_mid) return false;

    std::uint32_t tail     = *(std::uint32_t*)(&hash[16]);     // NOLINT
    std::uint32_t rhs_tail = *(std::uint32_t*)(&rhs.hash[16]); // NOLINT
    return tail == rhs_tail;
  }

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

inline std::byte make_byte(const char* two_chrs) { return make_byte(*two_chrs, *(two_chrs + 1)); }

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
