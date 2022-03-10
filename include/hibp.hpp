#pragma once

#include "fmt/format.h"
#include "os/algo.hpp"
#include "os/str.hpp"
#include <array>
#include <bit>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>
#include <ostream>
#include <type_traits>

namespace hibp {

#ifdef __cpp_lib_byteswap
using std::byteswap;
#else
template <class T>
constexpr T byteswap(T n) noexcept {
// clang-format off
  // NOLINTBEGIN
  #ifdef _MSC_VER
    #define BYTE_SWAP_16 _byteswap_ushort
    #define BYTE_SWAP_32 _byteswap_ulong
    #define BYTE_SWAP_64 _byteswap_uint64
  #else
    #define BYTE_SWAP_16 __builtin_bswap16
    #define BYTE_SWAP_32 __builtin_bswap32
    #define BYTE_SWAP_64 __builtin_bswap64
  #endif
  // NOLINTEND
  // clang-format on

  if constexpr (std::same_as<T, std::uint64_t>) {
    return BYTE_SWAP_64(n);
  } else if constexpr (std::same_as<T, std::uint32_t>) {
    return BYTE_SWAP_32(n);
  } else if constexpr (std::same_as<T, std::uint16_t>) {
    return BYTE_SWAP_16(n);
  }
}
#endif

template <typename T, typename... U>
concept any_of = (std::same_as<T, U> || ...);

// convert the sizeof(Target) bytes starting at `source` pointer to Target
// uses compiler intrinsics for endianess conversion if required and if `swap` == true
// caller responsibility to ensure that enough bytes are readable/dereferencable etc
// this compiles to a load and `bswap` which is very fast and can beat eg `memcmp`
template <typename T, bool swap_if_required = true>
constexpr T bytearray_cast(const std::byte* source) noexcept requires
    any_of<T, std::uint64_t, std::uint32_t, std::uint16_t, std::uint8_t> {

  static_assert(std::endian::native == std::endian::big ||
                    std::endian::native == std::endian::little,
                "mixed-endianess architectures are not supported");

  T value = *reinterpret_cast<const T*>(source); // NOLINT

  if constexpr (swap_if_required && sizeof(T) > 1 && std::endian::native == std::endian::little) {
    return byteswap<T>(value);
  } else {
    return value;
  }
}

template <typename T>
constexpr std::strong_ordering three_way(const std::byte* a, const std::byte* b) noexcept requires
    any_of<T, std::uint64_t, std::uint32_t, std::uint16_t, std::uint8_t> {
  return bytearray_cast<T>(a) <=> bytearray_cast<T>(b);
}

template <typename T>
constexpr bool
equal(const std::byte* a,
      const std::byte* b) requires any_of<T, std::uint64_t, std::uint32_t, std::uint16_t, std::uint8_t> {
  // don't bother swapping for endianess, since we don't need to for simple equality
  return bytearray_cast<T, false>(a) == bytearray_cast<T, false>(b);
}

template <std::size_t N>
using largest_uint =
    std::conditional_t<N >= 8, std::uint64_t,
                       std::conditional_t<N >= 4, std::uint32_t,
                                          std::conditional_t<N >= 2, std::uint16_t, std::uint8_t>>>;

template <std::size_t N>
constexpr std::strong_ordering array_three_way(const std::byte* a, const std::byte* b) noexcept {

  if constexpr (N == 0) {
    return std::strong_ordering::equal;
  } else {
    using next_type                 = largest_uint<N>;
    constexpr std::size_t next_size = sizeof(next_type);
    if (auto cmp = three_way<next_type>(&a[0], &b[0]); cmp != std::strong_ordering::equal)
      return cmp;
    return array_three_way<N - next_size>(&a[next_size], &b[next_size]);
  }
}

struct pawned_pw {

  std::strong_ordering operator<=>(const pawned_pw& rhs) const {
    return array_three_way<19>(&hash[0], &rhs.hash[0]);
  }

  bool operator==(const pawned_pw& rhs) const {
    if (bool cmp = equal<std::uint64_t>(&hash[0], &rhs.hash[0]); !cmp) return cmp;
    if (bool cmp = equal<std::uint64_t>(&hash[8], &rhs.hash[8]); !cmp) return cmp;
    return equal<std::uint32_t>(&hash[16], &rhs.hash[16]);
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
