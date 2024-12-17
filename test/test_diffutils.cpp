#include "diffutils.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <stdexcept>
#include <string>

template <hibp::pw_type PwType>
class DiffTest : public testing::Test {
protected:
  DiffTest()
      : testtmpdir{std::filesystem::canonical(std::filesystem::current_path() / "tmp")},

        old_path{testtmpdir / "old.sha1.bin"}, old_stream{old_path, std::ios_base::binary},
        oldw{old_stream}, new_path{testtmpdir / "new.sha1.bin"},
        new_stream{new_path, std::ios_base::binary}, neww{new_stream} {

    oldw.write(PwType{"0000000000000000000000000000000000000010:10"});
    oldw.write(PwType{"0000000000000000000000000000000000000020:20"});
    oldw.write(PwType{"0000000000000000000000000000000000000030:30"});
    oldw.flush(true);
  }

  void write(const std::string& s) { neww.write(PwType{s}); }

  void flush() { neww.flush(true); }

  std::filesystem::path testtmpdir;

  std::filesystem::path            old_path;
  std::ofstream                    old_stream;
  flat_file::stream_writer<PwType> oldw;

  std::filesystem::path            new_path;
  std::ofstream                    new_stream;
  flat_file::stream_writer<PwType> neww;
};

using DiffTestSha1 = DiffTest<hibp::pawned_pw_sha1>;
using DiffTestNtlm = DiffTest<hibp::pawned_pw_ntlm>;

TEST_F(DiffTestSha1, diffI0) {
  write("0000000000000000000000000000000000000005:5");

  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000020:20");
  write("0000000000000000000000000000000000000030:30");
  flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("I:00000000:0000000000000000000000000000000000000005:5\n", diff.str());
}

TEST_F(DiffTestSha1, diffI1) {
  write("0000000000000000000000000000000000000010:10");

  write("0000000000000000000000000000000000000015:15");

  write("0000000000000000000000000000000000000020:20");
  write("0000000000000000000000000000000000000030:30");
  flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("I:00000001:0000000000000000000000000000000000000015:15\n", diff.str());
}

TEST_F(DiffTestSha1, diffI2) {
  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000020:20");

  write("0000000000000000000000000000000000000025:25");

  write("0000000000000000000000000000000000000030:30");
  flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("I:00000002:0000000000000000000000000000000000000025:25\n", diff.str());
}

TEST_F(DiffTestSha1, diffI3) {
  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000020:20");
  write("0000000000000000000000000000000000000030:30");

  write("0000000000000000000000000000000000000035:35");
  flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("I:00000003:0000000000000000000000000000000000000035:35\n", diff.str());
}

TEST_F(DiffTestSha1, diffU0) {
  write("0000000000000000000000000000000000000010:11");
  write("0000000000000000000000000000000000000020:20");
  write("0000000000000000000000000000000000000030:30");
  flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("U:00000000:0000000000000000000000000000000000000010:11\n", diff.str());
}

TEST_F(DiffTestSha1, diffU1) {
  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000020:21");
  write("0000000000000000000000000000000000000030:30");
  flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("U:00000001:0000000000000000000000000000000000000020:21\n", diff.str());
}

TEST_F(DiffTestSha1, diffU2) {
  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000020:20");
  write("0000000000000000000000000000000000000030:31");
  flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("U:00000002:0000000000000000000000000000000000000030:31\n", diff.str());
}

TEST_F(DiffTestSha1, diffNewShort0) {
  write("0000000000000000000000000000000000000020:20");
  write("0000000000000000000000000000000000000030:30");
  flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff),
               std::runtime_error);
}
TEST_F(DiffTestSha1, diffNewShort1) {
  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000030:30");
  flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff),
               std::runtime_error);
}
TEST_F(DiffTestSha1, diffNewShort2) {
  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000020:20");
  flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff),
               std::runtime_error);
}

TEST_F(DiffTestSha1, diffOldReplaced0) {

  write("0000000000000000000000000000000000000015:10");
  write("0000000000000000000000000000000000000020:20");
  write("0000000000000000000000000000000000000030:30");
  flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff),
               std::runtime_error);
}
TEST_F(DiffTestSha1, diffOldReplaced1) {
  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000025:20");
  write("0000000000000000000000000000000000000030:30");
  flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff),
               std::runtime_error);
}
TEST_F(DiffTestSha1, diffOldReplaced2) {
  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000020:20");
  write("0000000000000000000000000000000000000035:30");

  flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff),
               std::runtime_error);
}

TEST_F(DiffTestSha1, diffAppend2) {
  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000020:20");
  write("0000000000000000000000000000000000000030:30");
  write("0000000000000000000000000000000000000040:40");
  write("0000000000000000000000000000000000000050:50");
  flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("I:00000003:0000000000000000000000000000000000000040:40\n"
            "I:00000003:0000000000000000000000000000000000000050:50\n",
            diff.str());
}

TEST_F(DiffTestSha1, diffCombo1) {
  write("0000000000000000000000000000000000000005:5");
  write("0000000000000000000000000000000000000010:10");
  write("0000000000000000000000000000000000000020:25");
  write("0000000000000000000000000000000000000027:27");
  write("0000000000000000000000000000000000000030:30");
  write("0000000000000000000000000000000000000040:40");
  write("0000000000000000000000000000000000000050:50");
  flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("I:00000000:0000000000000000000000000000000000000005:5\n"
            "U:00000001:0000000000000000000000000000000000000020:25\n"
            "I:00000002:0000000000000000000000000000000000000027:27\n"
            "I:00000003:0000000000000000000000000000000000000040:40\n"
            "I:00000003:0000000000000000000000000000000000000050:50\n",
            diff.str());
}
