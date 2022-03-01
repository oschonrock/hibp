#include "os/algo.hpp"
#include "os/bch.hpp"
#include "os/str.hpp"
#include "sha1/sha1.hpp"
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <streambuf>
#include <string>

namespace hibp {

std::byte make_nibble(char nibblechr) {
  int nibble = nibblechr - '0';
  if (nibble > 9) nibble = (nibble & ~('a' - 'A')) - ('A' - '0') + 10; // NOLINT signed
  assert(nibble >= 0 and nibble <= 15);                               
  return static_cast<std::byte>(nibble);
}

struct password {
  password() = default;

  password(std::ifstream& db, std::size_t pos) { // NOLINT initialization
    db.seekg(static_cast<long>(pos * sizeof(password)));
    db.read(reinterpret_cast<char*>(this), sizeof(*this)); // NOLINT reinterpret_cast
  }

  // line must be an upppercase sha1 hexstr with optional ":123" appended (123 is the count).
  explicit password(const std::string& line) {   // NOLINT initlialisation
    assert(line.length() >= hash.size() * 2);    // NOLINT decay
    for (auto [i, b]: os::algo::enumerate(hash)) // note b is by reference!
      b = make_nibble(line[2 * i]) << 4U | make_nibble(line[2 * i + 1]);

    if (line.size() > hash.size() * 2 + 1)
      count = os::str::parse_nonnegative_int(line.c_str() + hash.size() * 2 + 1,
                                             line.c_str() + line.size(), -1);
    else
      count = -1;
  }

  bool operator==(const password& rhs) const { return hash == rhs.hash; }

  std::strong_ordering operator<=>(const password& rhs) const { return hash <=> rhs.hash; }

  friend std::ostream& operator<<(std::ostream& os, const password& rhs) {
    os << std::setfill('0') << std::hex << std::uppercase;
    for (auto&& c: rhs.hash) os << std::setw(2) << static_cast<unsigned>(c);
    os << std::dec << ":" << rhs.count;
    return os;
  }

  std::array<std::byte, 20> hash;
  std::int32_t              count; // be definitive about size
};

// convert text file to binary. left here for future updates
void build(std::istream& text_stream, std::ostream& binary_stream) {
  // std::getline is about 2x faster than `is >> line` here

  // Using std::fwrite is about 8x faster than std::ostream::write.
  // But only if writing 1 password at a time. Instead buffer 100 of them.
  constexpr std::size_t         obufcnt = 100;
  std::array<password, obufcnt> obuf{};
  std::size_t                   obufpos = 0;
  for (std::string line; std::getline(text_stream, line);) {

    password pw(line);
    if (obufpos == obuf.size()) {
      binary_stream.write(reinterpret_cast<char*>(&obuf), // NOLINT reincast
                          static_cast<std::streamsize>(sizeof(password) * obuf.size()));
      obufpos = 0;
    }
    std::memcpy(&obuf[obufpos], &pw, sizeof(pw));
    ++obufpos;
  }
  if (obufpos > 0)
    binary_stream.write(reinterpret_cast<char*>(&obuf), // NOLINT reincast
                        static_cast<std::streamsize>(sizeof(password) * obufpos));
}

class database {
public:
  explicit database(std::string dbfilename)
      : dbfilename_(std::move(dbfilename)), dbpath_(dbfilename_),
        dbfsize_(std::filesystem::file_size(dbpath_)), db_(dbpath_, std::ios::binary) {

    if (dbfsize_ % sizeof(password) != 0)
      throw std::domain_error("db file size is not a multiple of the record size");

    dbsize_ = dbfsize_ / sizeof(password);

    if (!db_.is_open()) throw std::domain_error("cannot open db: " + std::string(dbpath_));
  }

  std::optional<password> search(password needle) {
    std::size_t count = dbsize_;
    std::size_t first = 0;
    // lower_bound binary search algo
    while (count > 0) {
      std::size_t pos  = first;
      std::size_t step = count / 2;
      pos += step;
      password cur(db_, pos);
      if (cur < needle) {
        first = ++pos;
        count -= step + 1;
      } else
        count = step;
    }
    if (first < dbsize_) {
      password found(db_, first);
      if (found == needle) return found;
    }
    return std::nullopt;
  }

  std::optional<password> search(const std::string& sha1_pw_hash_txt) {
    hibp::password needle(sha1_pw_hash_txt);
    return search(needle);
  }

  std::ifstream& db() { return db_; }

private:
  std::string           dbfilename_;
  std::filesystem::path dbpath_;
  std::size_t           dbfsize_;
  std::size_t           dbsize_;
  std::ifstream         db_;
};

} // namespace hibp

int main(int argc, char* argv[]) {
  std::ios_base::sync_with_stdio(false);

  // build db
  // hibp::build(std::cin, std::cout);
  // return 0;

  try {
    if (argc < 3)
      throw std::domain_error("USAGE: " + std::string(argv[0]) + " dbfile.bin plaintext_password");

    hibp::database db(argv[1]);

    SHA1 sha1;
    sha1.update(argv[2]);
    hibp::password needle(sha1.final());

    std::cout << "needle = " << needle << "\n";
    auto found = db.search(needle);

    if (found)
      std::cout << "found  = " << *found << "\n";
    else
      std::cout << "not found\n";

  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
  }

  return EXIT_SUCCESS;
}
