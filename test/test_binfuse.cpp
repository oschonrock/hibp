#include "binaryfusefilter.h"
#include "binfuse/filter.hpp"
#include "binfuse/sharded_filter.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "mio/page.hpp"
#include "gtest/gtest.h"
#include <concepts>
#include <filesystem>

class binfuse_test : public testing::Test {
protected:
  binfuse_test()
      : testtmpdir{std::filesystem::canonical(std::filesystem::current_path() /
                                              "../../../../test/tmp")},
        testdatadir{std::filesystem::canonical(testtmpdir / "../data")},
        filter_path{testtmpdir / "filter.bin"} {}

  // slow, so don't run as part of tests, but result is committed in test/data/...
  void build_sharded_sample() {
    // build a sample filter with 256 shards
    flat_file::database<hibp::pawned_pw_sha1> db{testdatadir / "hibp_all.sha1.bin",
                                                 (1U << 16U) / sizeof(hibp::pawned_pw_sha1)};

    flat_file::file_writer<hibp::pawned_pw_sha1> writer(
        (testdatadir / "hibp_sharded_sample.10.sha1.bin").string());

    auto start = db.begin();
    for (std::uint32_t prefix = 0; prefix != 0x100; ++prefix) {
      if (auto iter = std::find_if(start, db.end(),
                                   [=](const hibp::pawned_pw_sha1& pw) {
                                     return static_cast<std::uint8_t>(pw.hash[0]) == prefix;
                                   });
          iter != db.end()) {
        start = iter;
        for (unsigned i = 0; i != 1000; ++i) {
          writer.write(*iter++);
        }
      }
    }
  }

  template <binfuse::filter_type FilterType>
  void build_filter(double max_false_positive_rate) {
    flat_file::database<hibp::pawned_pw_sha1> db{testdatadir / "hibp_sharded_sample.10.sha1.bin",
                                                 (1U << 16U) / sizeof(hibp::pawned_pw_sha1)};

    std::vector<std::uint64_t> keys;
    for (const auto& record: db) {
      auto key = arrcmp::impl::bytearray_cast<std::uint64_t>(record.hash.data());
      keys.emplace_back(key);
    }
    auto filter = binfuse::filter<FilterType>(keys);
    EXPECT_TRUE(filter.verify(keys));
    EXPECT_LE(filter.estimate_false_positive_rate(), max_false_positive_rate);
  }

  template <binfuse::filter_type FilterType>
  void build_sharded_filter(double max_false_positive_rate) {
    flat_file::database<hibp::pawned_pw_sha1> db{testdatadir / "hibp_sharded_sample.10.sha1.bin",
                                                 (1U << 16U) / sizeof(hibp::pawned_pw_sha1)};

    std::filesystem::path filter_filename;
    if constexpr (std::same_as<FilterType, binary_fuse8_t>) {
      filter_filename = "sharded_filter8.bin";
    } else {
      filter_filename = "sharded_filter16.bin";
    }
    {
      binfuse::sharded_filter<FilterType, mio::access_mode::write> sharded_filter_sink(
          testtmpdir / filter_filename);

      std::vector<std::uint64_t> keys;
      std::uint32_t              last_prefix = 0;

      for (const auto& record: db) {
        auto key    = arrcmp::impl::bytearray_cast<std::uint64_t>(record.hash.data());
        auto prefix = sharded_filter_sink.extract_prefix(key);
        if (prefix != last_prefix) {
          sharded_filter_sink.add(binfuse::filter<FilterType>(keys), last_prefix);
          keys.clear();
          last_prefix = prefix;
        }
        keys.emplace_back(key);
      }
      if (!keys.empty()) {
        sharded_filter_sink.add(binfuse::filter<FilterType>(keys), last_prefix);
      }

      binfuse::sharded_filter<FilterType, mio::access_mode::read> sharded_filter_source(
          testtmpdir / filter_filename);

      // full verify across all shards
      for (const auto& pw: db) {
        auto needle = arrcmp::impl::bytearray_cast<std::uint64_t>(pw.hash.data());
        EXPECT_TRUE(sharded_filter_source.contains(needle));
      }

      EXPECT_LE(sharded_filter_source.estimate_false_positive_rate(), max_false_positive_rate);
    }

    std::filesystem::remove(testtmpdir / filter_filename);
  }

  std::filesystem::path testtmpdir;
  std::filesystem::path testdatadir;

  std::filesystem::path filter_path;
};

TEST_F(binfuse_test, build_filter8) { // NOLINT
  build_filter<binary_fuse8_s>(0.005);
}

TEST_F(binfuse_test, build_sharded_filter8) { // NOLINT
  build_sharded_filter<binary_fuse8_s>(0.005);
}

TEST_F(binfuse_test, build_filter16) { // NOLINT
  build_filter<binary_fuse16_s>(0.00005);
}

TEST_F(binfuse_test, build_sharded_filter16) { // NOLINT
  build_sharded_filter<binary_fuse16_s>(0.00005);
}
