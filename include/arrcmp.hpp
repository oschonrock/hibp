#pragma once

#include <array>
#include <bit>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
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

} // namespace impl

struct three_way {
  template <typename T>
  constexpr std::strong_ordering operator()(const std::byte* a, const std::byte* b) noexcept
      requires impl::any_of<T, std::uint64_t, std::uint32_t, std::uint16_t, std::uint8_t> {
    return impl::bytearray_cast<T>(a) <=> impl::bytearray_cast<T>(b);
  }
  using return_type                           = std::strong_ordering;
  static constexpr return_type equality_value = std::strong_ordering::equal;
};

struct equal {
  template <typename T>
  constexpr bool operator()(const std::byte* a, const std::byte* b) noexcept requires
      impl::any_of<T, std::uint64_t, std::uint32_t, std::uint16_t, std::uint8_t> {
    // don't bother swapping for endianess, since we don't need to for simple
    // equality
    return impl::bytearray_cast<T, false>(a) == impl::bytearray_cast<T, false>(b);
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
    using next_type                 = impl::largest_uint<N>;
    constexpr std::size_t next_size = sizeof(next_type);
    if (auto res = comp.template operator()<next_type>(a, b); res != Comp::equality_value)
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
