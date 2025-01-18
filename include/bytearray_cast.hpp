#include <concepts>
#include <cstddef>
#include <cstdint>
#include <bit>

namespace hibp {

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
constexpr T bytearray_cast(const std::byte* source) noexcept
  requires any_of<T, std::uint64_t, std::uint32_t, std::uint16_t, std::uint8_t>
{

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

}  // namespace hibp
