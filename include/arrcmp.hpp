#pragma once

#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstdint>
#include <emmintrin.h>
#include <immintrin.h>

namespace arrcmp {

// https://godbolt.org/z/qEf7dnxj4

#ifdef _MSC_VER
#include <intrin.h>

constexpr static inline int __builtin_ctzll(std::uint64_t x) noexcept {
  unsigned long ret;
  _BitScanForward64(&ret, x);
  return (int)ret;
}
constexpr static inline std::uint16_t __builtin_bswap16(std::uint16_t x) noexcept {
  return _byteswap_ushort(x);
}
constexpr static inline std::uint32_t __builtin_bswap32(std::uint32_t x) noexcept {
  return _byteswap_ulong(x);
}
constexpr static inline std::uint64_t __builtin_bswap64(std::uint64_t x) noexcept {
  return _byteswap_uint64(x);
}
#endif

namespace impl {

#ifdef __cpp_lib_byteswap
using std::byteswap;
#else
template <class T>
constexpr T byteswap(T n) noexcept {
  if constexpr (std::same_as<T, std::uint64_t>) {
    return __builtin_bswap64(n);
  } else if constexpr (std::same_as<T, std::uint32_t>) {
    return __builtin_bswap32(n);
  } else if constexpr (std::same_as<T, std::uint16_t>) {
    return __builtin_bswap16(n);
  }
}
#endif

template <typename T, typename... U>
concept any_of = (std::same_as<T, U> || ...);

// Convert the sizeof(Target) bytes starting at `source` pointer to Target.
// Uses compiler intrinsics for endianess conversion if required and if
// `swap_if_required` == true.
// It is the caller's responsibility to ensure that enough bytes are
// readable/dereferencable etc. This compiles to just a `mov` and `bswap`
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

// clang-format off
#ifdef __AVX512BW__
static constexpr bool        avx512 = true;
static constexpr bool        avx2   = true;
static constexpr bool        sse    = true;
static constexpr std::size_t maxvec = sizeof(__m512i);
#else
  #ifdef __AVX2__
  static constexpr bool        avx512 = false;
  static constexpr bool        avx2   = true;
  static constexpr bool        sse    = true;
  static constexpr std::size_t maxvec = sizeof(__m256i);
  #else
    #ifdef __SSE__
    static constexpr bool        avx512 = false;
    static constexpr bool        avx2   = false;
    static constexpr bool        sse    = true;
    static constexpr std::size_t maxvec = sizeof(__m128i);
    #else
    static constexpr bool        avx512 = false;
    static constexpr bool        avx2   = false;
    static constexpr bool        sse    = false;
    static constexpr std::size_t maxvec = sizeof(std::uint64_t);
    #endif
  #endif
#endif
// clang-format on

template <std::size_t N>
using largest_vector = std::conditional_t<
    avx512 && N >= sizeof(__m512i), __m512i,
    std::conditional_t<avx2 && N >= sizeof(__m256i), __m256i,
                       std::conditional_t<sse && N >= sizeof(__m128i), __m128i, std::uint64_t>>>;

template <typename std::size_t N>
constexpr std::size_t next_size() noexcept {
  if constexpr (N > sizeof(std::uint64_t)) {
    return sizeof(largest_vector<N>);
  } else {
    return sizeof(largest_uint<N>);
  }
}

template <typename T>
requires std::integral<T>
constexpr std::strong_ordering cmp(T a, T b) noexcept {
// gcc & MSVC compile a fast `<=>`, but clang is quite slow
#ifdef __clang__
  return std::bit_cast<std::strong_ordering>(static_cast<std::int8_t>((a > b) - (a < b)));
#else
  return a <=> b;
#endif
}

constexpr inline std::strong_ordering cmp(std::byte a, std::byte b) noexcept {
  return cmp(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b));
}

template <typename T>
requires std::integral<T>
constexpr int cmp_by_substracting(T a, T b) noexcept {
  if constexpr (sizeof(T) < sizeof(int)) {
    // returning an int which has enough range for this subtraction
    return a - b;
  } else {
    // must protect against being larger than int return value
    return (a > b) - (a < b);
  }
}

constexpr inline int cmp_by_substracting(std::byte a, std::byte b) noexcept {
  return cmp_by_substracting(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b));
}

template <typename T>
constexpr std::uint64_t vector_cmp(const std::byte* a, const std::byte* b) noexcept {
  if constexpr (std::same_as<T, __m128i>) {
    const auto sa = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a)); // NOLINT reincast
    const auto sb = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b)); // NOLINT reincast
    const auto sc = _mm_cmpeq_epi8(sa, sb);
    // careful about casting and integer size during `not`!
    const std::uint16_t mask = ~static_cast<std::uint16_t>(_mm_movemask_epi8(sc));
    return mask;
  } else if constexpr (std::same_as<T, __m256i>) {
    const auto sa = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a)); // NOLINT reincast
    const auto sb = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(b)); // NOLINT reincast
    const auto sc = _mm256_cmpeq_epi8(sa, sb);
    // careful about casting and integer size during `not`!
    const std::uint32_t mask = ~static_cast<std::uint32_t>(_mm256_movemask_epi8(sc));
    // `vzeroupper` is automatically added by compiler
    return mask;
  } else if constexpr (std::same_as<T, __m512i>) {
    const auto sa = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(a)); // NOLINT reincast
    const auto sb = _mm512_loadu_si512(reinterpret_cast<const __m512i*>(b)); // NOLINT reincast
    // compare unsigned 8bit integers: 4 means Not euqal, so invert not required
    const std::uint64_t mask = _mm512_cmp_epu8_mask(sa, sb, 4);
    return mask;
  }
}

} // namespace impl

struct three_way {
  template <std::size_t N>
  constexpr std::strong_ordering operator()(const std::byte* a, const std::byte* b) const noexcept {
    static_assert(N <= impl::maxvec, "this cpu cannot handle compares this large");
    if constexpr (N <= sizeof(std::uint64_t)) {
      using T = impl::largest_uint<N>;
      return impl::cmp(impl::bytearray_cast<T>(a), impl::bytearray_cast<T>(b));
    } else {
      using T   = impl::largest_vector<N>;
      auto mask = impl::vector_cmp<T>(a, b);
      if (mask == 0) return equality_value;
      auto count = static_cast<unsigned>(__builtin_ctzll(mask));
      return impl::cmp(a[count], b[count]);
    }
  }
  using return_t                           = std::strong_ordering;
  static constexpr return_t equality_value = std::strong_ordering::equal;
};

struct three_way_int {
  template <std::size_t N>
  constexpr int operator()(const std::byte* a, const std::byte* b) const noexcept {
    static_assert(N <= impl::maxvec, "this cpu cannot handle compares this large");
    if constexpr (N <= sizeof(std::uint64_t)) {
      using T = impl::largest_uint<N>;
      return impl::cmp_by_substracting(impl::bytearray_cast<T>(a), impl::bytearray_cast<T>(b));
    } else {
      using T   = impl::largest_vector<N>;
      auto mask = impl::vector_cmp<T>(a, b);
      if (mask == 0) return equality_value;
      auto count = static_cast<unsigned>(__builtin_ctzll(mask));
      return impl::cmp_by_substracting(a[count], b[count]);
    }
  }
  using return_t                           = int;
  static constexpr return_t equality_value = 0;
};

struct equal {
  template <std::size_t N>
  constexpr bool operator()(const std::byte* a, const std::byte* b) const noexcept {
    static_assert(N <= impl::maxvec, "this cpu cannot handle compares this large");
    if constexpr (N <= sizeof(std::uint64_t)) {
      using T = impl::largest_uint<N>;
      // don't bother swapping for endianess, no need for simple equality
      return impl::bytearray_cast<T, false>(a) == impl::bytearray_cast<T, false>(b);
    } else {
      using T   = impl::largest_vector<N>;
      auto mask = impl::vector_cmp<T>(a, b);
      return mask == 0;
    }
  }
  using return_t                           = bool;
  static constexpr return_t equality_value = true;
};

template <std::size_t N, typename Comp>
constexpr typename Comp::return_t array_compare(const std::byte* a, const std::byte* b,
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
constexpr typename Comp::return_t array_compare(const std::array<std::byte, N>& a,
                                                const std::array<std::byte, N>& b,
                                                Comp                            comp) noexcept {
  return array_compare<N>(a.data(), b.data(), comp);
}

} // namespace arrcmp
