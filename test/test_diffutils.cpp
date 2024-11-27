#include "diffutils.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "gtest/gtest.h"
#include <filesystem>
#include <fstream>
#include <sstream>

template <hibp::pw_type PwType>
class DiffTest : public testing::Test {
protected:
  DiffTest()
      : testtmpdir{std::filesystem::canonical(std::filesystem::current_path() /
                                              "../../../../test/tmp")},
        old_path{testtmpdir / "old.sha1.bin"}, old_stream{old_path}, oldw{old_stream},
        new_path{testtmpdir / "new.sha1.bin"}, new_stream{new_path}, neww{new_stream} {

    using namespace std::string_literals;
    oldw.write("0000000000000000000000000000000000000010:10"s);
    oldw.write("0000000000000000000000000000000000000020:20"s);
    oldw.write("0000000000000000000000000000000000000030:30"s);
    oldw.flush();
    old_stream.flush();
  }

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
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000005:5"s);

  neww.write("0000000000000000000000000000000000000010:10"s);
  neww.write("0000000000000000000000000000000000000020:20"s);
  neww.write("0000000000000000000000000000000000000030:30"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("I:00000000:0000000000000000000000000000000000000005:5\n", diff.str());
}

TEST_F(DiffTestSha1, diffI1) {
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000010:10"s);

  neww.write("0000000000000000000000000000000000000015:15"s);

  neww.write("0000000000000000000000000000000000000020:20"s);
  neww.write("0000000000000000000000000000000000000030:30"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("I:00000001:0000000000000000000000000000000000000015:15\n", diff.str());
}

TEST_F(DiffTestSha1, diffI2) {
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000010:10"s);
  neww.write("0000000000000000000000000000000000000020:20"s);

  neww.write("0000000000000000000000000000000000000025:25"s);

  neww.write("0000000000000000000000000000000000000030:30"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("I:00000002:0000000000000000000000000000000000000025:25\n", diff.str());
}

TEST_F(DiffTestSha1, diffI3) {
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000010:10"s);
  neww.write("0000000000000000000000000000000000000020:20"s);
  neww.write("0000000000000000000000000000000000000030:30"s);

  neww.write("0000000000000000000000000000000000000035:35"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("I:00000003:0000000000000000000000000000000000000035:35\n", diff.str());
}

TEST_F(DiffTestSha1, diffU0) {
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000010:11"s);
  neww.write("0000000000000000000000000000000000000020:20"s);
  neww.write("0000000000000000000000000000000000000030:30"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("U:00000000:0000000000000000000000000000000000000010:11\n", diff.str());
}

TEST_F(DiffTestSha1, diffU1) {
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000010:10"s);
  neww.write("0000000000000000000000000000000000000020:21"s);
  neww.write("0000000000000000000000000000000000000030:30"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("U:00000001:0000000000000000000000000000000000000020:21\n", diff.str());
}

TEST_F(DiffTestSha1, diffU2) {
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000010:10"s);
  neww.write("0000000000000000000000000000000000000020:20"s);
  neww.write("0000000000000000000000000000000000000030:31"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff);

  EXPECT_EQ("U:00000002:0000000000000000000000000000000000000030:31\n", diff.str());
}

TEST_F(DiffTestSha1, diffNewShort0) {
  using namespace std::string_literals;

  neww.write("0000000000000000000000000000000000000020:20"s);
  neww.write("0000000000000000000000000000000000000030:30"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff), std::runtime_error);
}
TEST_F(DiffTestSha1, diffNewShort1) {
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000010:10"s);

  neww.write("0000000000000000000000000000000000000030:30"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff), std::runtime_error);
}
TEST_F(DiffTestSha1, diffNewShort2) {
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000010:10"s);
  neww.write("0000000000000000000000000000000000000020:20"s);
  
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff), std::runtime_error);
}

TEST_F(DiffTestSha1, diffOldReplaced0) {
  using namespace std::string_literals;

  neww.write("0000000000000000000000000000000000000015:10"s);
  neww.write("0000000000000000000000000000000000000020:20"s);
  neww.write("0000000000000000000000000000000000000030:30"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff), std::runtime_error);
}
TEST_F(DiffTestSha1, diffOldReplaced1) {
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000010:10"s);
  neww.write("0000000000000000000000000000000000000025:20"s);
  neww.write("0000000000000000000000000000000000000030:30"s);
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff), std::runtime_error);
}
TEST_F(DiffTestSha1, diffOldReplaced2) {
  using namespace std::string_literals;
  neww.write("0000000000000000000000000000000000000010:10"s);
  neww.write("0000000000000000000000000000000000000020:20"s);
  neww.write("0000000000000000000000000000000000000035:30"s);
  
  neww.flush();
  new_stream.flush();

  std::stringstream diff;
  EXPECT_THROW(hibp::diffutils::run_diff<hibp::pawned_pw_sha1>(old_path, new_path, diff), std::runtime_error);
}
