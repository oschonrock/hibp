#include "flat_file.hpp"
#include "hibp.hpp"
#include "toc.hpp"
#include "gtest/gtest.h"
#include <cstddef>
#include <filesystem>
#include <random>
#include <type_traits>

template <hibp::pw_type PwType>
void run_search(bool toc, unsigned toc_bits = 0) { // NOLINT complexity
  auto testdatadir =
      std::filesystem::canonical(std::filesystem::current_path() / "../../../../test/data");

  std::filesystem::path db_path;

  if constexpr (std::is_same_v<PwType, hibp::pawned_pw_sha1>) {
    db_path = testdatadir / "hibp_test.sha1.bin";
  } else if constexpr (std::is_same_v<PwType, hibp::pawned_pw_ntlm>) {
    db_path = testdatadir / "hibp_test.ntlm.bin";
  } else if constexpr (std::is_same_v<PwType, hibp::pawned_pw_sha1t64>) {
    db_path = testdatadir / "hibp_test.sha1t64.bin";
  }

  if (toc) {
    // this probably does nothing as these are checking into git as golden masters
    // the build is properly tested during system_tests
    hibp::toc_build<PwType>(db_path, toc_bits);
  }

  flat_file::database<PwType> db(db_path, 4096 / sizeof(PwType));

  std::mt19937_64                            generator{std::random_device{}()};
  std::uniform_int_distribution<std::size_t> distribution(0, db.number_records() - 1);

  for (std::size_t i = 0; i != db.number_records() / 50; ++i) {
    auto              needle_idx = distribution(generator);
    PwType            needle     = db.get_record(needle_idx);
    std::stringstream trace;
    trace << "Failed to find: " << needle << " which is in " << db_path << " at record "
          << needle_idx << " (byte offset: " << sizeof(PwType) * needle_idx << ")";
    SCOPED_TRACE(trace.str());

    if (toc) {
      std::optional<PwType> maybe_ppw = hibp::toc_search<PwType>(db, needle, toc_bits);
      EXPECT_TRUE(maybe_ppw);
      EXPECT_EQ(*maybe_ppw, needle);
      EXPECT_EQ(maybe_ppw->count, needle.count);
    } else {
      auto iter = std::lower_bound(db.begin(), db.end(), needle);
      EXPECT_NE(iter, db.end());
      EXPECT_EQ(*iter, needle);
      EXPECT_EQ(iter->count, needle.count);
    }
  }
}

TEST(hibp_integration, search_sha1) { // NOLINT
  run_search<hibp::pawned_pw_sha1>(false);
}

TEST(hibp_integration, search_ntlm) { // NOLINT
  run_search<hibp::pawned_pw_ntlm>(false);
}

TEST(hibp_integration, search_sha1t64) { // NOLINT
  run_search<hibp::pawned_pw_sha1t64>(false);
}

TEST(hibp_integration, toc_search_sha1) { // NOLINT
  run_search<hibp::pawned_pw_sha1>(true, 18);
}

TEST(hibp_integration, toc_search_ntlm) { // NOLINT
  run_search<hibp::pawned_pw_ntlm>(true, 18);
}

TEST(hibp_integration, toc_search_sha1t64) { // NOLINT
  run_search<hibp::pawned_pw_sha1t64>(true, 18);
}
