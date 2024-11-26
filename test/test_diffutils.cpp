#include "arrcmp.hpp"
#include "gtest/gtest.h"

TEST(arrcmp, arrays) {                         // NOLINT
  test_set<1, 2 * arrcmp::impl::maxvec - 1>(); // TODO auto detect the largest MM register
}
