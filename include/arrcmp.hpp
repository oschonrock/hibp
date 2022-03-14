#pragma once

#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <emmintrin.h>
#include <immintrin.h>

namespace arrcmp {

namespace impl {

#ifdef __cpp_lib_byteswap
using std::byteswap;
#else
template <class T>
constexpr T byteswap(T n) noexcept {

  // clang-format off
  #ifdef _MSC_VER
    #define BYTE_SWAP_16 _byteswap_ushort // NOLINT
    #define BYTE_SWAP_32 _byteswap_ulong  // NOLINT
    #define BYTE_SWAP_64 _byteswap_uint64 // NOLINT
  #else
    #define BYTE_SWAP_16 __builtin_bswap16 // NOLINT
    #define BYTE_SWAP_32 __builtin_bswap32 // NOLINT
    #define BYTE_SWAP_64 __builtin_bswap64 // NOLINT
  #endif
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

// Convert the sizeof(Target) bytes starting at `source` pointer to Target.
// Uses compiler intrinsics for endianess conversion if required and if
// `swap_if_required` == true.
// It is the caller's responsibility to ensure that enough bytes are
// readable/dereferencable etc. This compiles to just a `mov and `bswap`
template <typename T, bool swap_if_required = true>
constexpr T bytearray_cast(const std::byte* source) noexcept requires
    any_of<T, std::uint64_t, std::uint32_t, std::uint16_t, std::uint8_t> {

  static_assert(std::endian::native == std::endian::big ||
                    std::endian::native == std::endian::little,
                "mixed-endianess architectures are not supported");

  T value = *reinterpret_cast<const T*>(source); // NOLINT reincast

  if constexpr (swap_if_required && sizeof(T) > 1 && std::endian::native == std::endian::little) {
    return byteswap<T>(value);
  } else {
    return value;
  }
}

template <std::size_t N>
using largest_uint =
    std::conditional_t<N >= 8, std::uint64_t,
                       std::conditional_t<N >= 4, std::uint32_t,
                                          std::conditional_t<N >= 2, std::uint16_t, std::uint8_t>>>;

template <typename std::size_t N>
constexpr std::size_t next_size() {
  if constexpr (N < sizeof(__m128i)) {
    return sizeof(largest_uint<N>);
  } else {
    return sizeof(__m128i);
  }
}

template <typename T>
requires std::integral<T> std::strong_ordering cmp(T a, T b) {
// gcc compiles a fast `<=>`, but clang is quite slow
#ifdef __clang__
  return std::bit_cast<std::strong_ordering>(static_cast<std::int8_t>((a > b) - (a < b)));
#else
  return a <=> b;
#endif
}

inline std::strong_ordering cmp(std::byte a, std::byte b) {
  return cmp(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b));
}

template <typename T>
requires std::integral<T>
int cmp_by_substracting(T a, T b) {
  if constexpr (sizeof(T) < sizeof(int)) {
    // returning an int which has enough range for this subtraction
    return a - b;
  } else {
    // must protect against being larger than int return value
    return (a > b) - (a < b);
  }
}

inline int cmp_by_substracting(std::byte a, std::byte b) {
  return cmp_by_substracting(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b));
}

template <typename T>
std::uint16_t vector_cmp(const std::byte* a, const std::byte* b) {
  if constexpr (std::same_as<T, __m128i>) {
    const auto sa = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a)); // NOLINT reincast
    const auto sb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b)); // NOLINT reincast
    const auto sc = _mm_cmpeq_epi8(sa, sb);
    // careful about casting and integer size during `not`!
    const std::uint16_t mask = ~static_cast<std::uint16_t>(_mm_movemask_epi8(sc));
    return mask;
  }
}

} // namespace impl

struct three_way {
  template <std::size_t N>
  constexpr std::strong_ordering operator()(const std::byte* a, const std::byte* b) noexcept {
    static_assert(N <= sizeof(__m128i), "this cpu cannot handle compares this large");
    if constexpr (N <= sizeof(std::uint64_t)) {
      using T = impl::largest_uint<N>;
      return impl::cmp(impl::bytearray_cast<T>(a), impl::bytearray_cast<T>(b));
    } else {
      auto mask = impl::vector_cmp<__m128i>(a, b);
      if (mask == 0) return equality_value;
      auto count = static_cast<unsigned>(__builtin_ctz(mask));
      return impl::cmp(a[count], b[count]);
    }
  }
  using return_type                           = std::strong_ordering;
  static constexpr return_type equality_value = std::strong_ordering::equal;
};

struct three_way_int {
  template <std::size_t N>
  constexpr int operator()(const std::byte* a, const std::byte* b) noexcept {
    static_assert(N <= sizeof(__m128i), "this cpu cannot handle compares this large");
    if constexpr (N <= sizeof(std::uint64_t)) {
      using T = impl::largest_uint<N>;
      return impl::cmp_by_substracting(impl::bytearray_cast<T>(a), impl::bytearray_cast<T>(b));
    } else {
      auto mask = impl::vector_cmp<__m128i>(a, b);
      if (mask == 0) return equality_value;
      auto count = static_cast<unsigned>(__builtin_ctz(mask));
      return impl::cmp_by_substracting(a[count], b[count]);
    }
  }
  using return_type                           = int;
  static constexpr return_type equality_value = 0;
};

struct equal {
  template <std::size_t N>
  constexpr bool operator()(const std::byte* a, const std::byte* b) noexcept {
    static_assert(N <= sizeof(__m128i), "this cpu cannot handle compares this large");
    if constexpr (N <= sizeof(std::uint64_t)) {
      using T = impl::largest_uint<N>;
      // don't bother swapping for endianess, no need for simple equality
      return impl::bytearray_cast<T, false>(a) == impl::bytearray_cast<T, false>(b);
    } else {
      auto mask = impl::vector_cmp<__m128i>(a, b);
      return mask == 0;
    }
  }
  using return_type                           = bool;
  static constexpr return_type equality_value = true;
};

template <std::size_t N, typename Comp>
constexpr typename Comp::return_type array_compare(const std::byte* a, const std::byte* b,
                                                   Comp comp) noexcept {
  if constexpr (N == 0) {
    return Comp::equality_value;
  } else {
    constexpr std::size_t next_size = impl::next_size<N>();
    if (auto res = comp.template operator()<next_size>(a, b); res != Comp::equality_value)
      return res;
    return array_compare<N - next_size>(&a[next_size], &b[next_size], comp);
  }
}

template <std::size_t N, typename Comp>
constexpr typename Comp::return_type array_compare(const std::array<std::byte, N> a,
                                                   const std::array<std::byte, N> b,
                                                   Comp                           comp) noexcept {
  return array_compare<N>(&a[0], &b[0], comp);
}

} // namespace arrcmp
