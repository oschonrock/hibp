#include "arrcmp.hpp"
#include "gtest/gtest.h"
#include <string>
#include <vector>

template <typename T>
int three_way_cmp(T a, T b) {
  return static_cast<int>(a > b) - static_cast<int>(a < b);
}
int get_sign(int a) { return three_way_cmp(a, 0); }

template <typename... Ts>
std::array<std::byte, sizeof...(Ts)> make_bytes(Ts&&... args) noexcept {
  return {std::byte(std::forward<Ts>(args))...};
}

template <std::size_t N>
void test() {
  static_assert(N != 0, "cannot make or compare empty arrays");

  auto bytes = make_bytes(0x00, 0x01, 0x02, 0x7f, 0x80, 0x81, 0xfd, 0xfe, 0xff);
  for (auto a_byte: bytes) {
    for (auto b_byte: bytes) {
      for (std::size_t pos = 0; pos != N; ++pos) {
        auto a = std::array<std::byte, N>{};
        auto b = std::array<std::byte, N>{};
        a[pos] = a_byte;

        std::vector<std::size_t> b_positions{pos};
        // offset test, ensures correct byte swapping for endianess
        if (pos < N - 1) b_positions.push_back(pos + 1);
        for (auto b_pos: b_positions) {
          b[b_pos] = b_byte;

          SCOPED_TRACE("N=" + std::to_string(N) +
                       ", a_byte=" + std::to_string(static_cast<unsigned>(a_byte)) +
                       ", b_byte=" + std::to_string(static_cast<unsigned>(b_byte)) +
                       ", pos=" + std::to_string(pos) + ", b_pos=" + std::to_string(b_pos));

          EXPECT_EQ(arrcmp::array_compare(a, b, arrcmp::equal{}), a == b);
          EXPECT_EQ(arrcmp::array_compare(a, b, arrcmp::three_way{}), a <=> b);
          EXPECT_EQ(get_sign(arrcmp::array_compare(a, b, arrcmp::three_way_int{})),
                    three_way_cmp(a, b));

          EXPECT_EQ(arrcmp::array_compare(b, a, arrcmp::equal{}), b == a);
          EXPECT_EQ(arrcmp::array_compare(b, a, arrcmp::three_way{}), b <=> a);
          EXPECT_EQ(get_sign(arrcmp::array_compare(b, a, arrcmp::three_way_int{})),
                    three_way_cmp(b, a));
        }
      }
    }
  }
}

template <std::size_t Min, std::size_t Max>
void test_set() {
  []<std::size_t... Sizes>(std::index_sequence<Sizes...> /*sizes*/) {
    (test<Min + Sizes>(), ...);
  }(std::make_index_sequence<Max - Min + 1>{});
}

TEST(arrcmp, arrays) {                         // NOLINT
  test_set<1, 2 * arrcmp::impl::maxvec - 1>(); // TODO auto detect the largest MM register
}
