#include "flat_file.hpp"
#include "hibp.hpp"
#include <array>
#include <cstddef>
#include <cstdlib>
#include "fmt/core.h"
#include <optional>
#include <string>
#include <vector>

struct toc_entry {
  std::size_t               start;
  std::array<std::byte, 20> hash;

  friend std::ostream& operator<<(std::ostream& os, const toc_entry& rhs) {
    os << fmt::format("{:10d}: ", rhs.start);
    for (auto&& b: rhs.hash) os << fmt::format("{:02X}", static_cast<unsigned>(b));
    return os;
  }

  std::strong_ordering operator<=>(const toc_entry& rhs) const {
    return arrcmp::array_compare(hash, rhs.hash, arrcmp::three_way{});
  }

  bool operator==(const toc_entry& rhs) const {
    return arrcmp::array_compare(hash, rhs.hash, arrcmp::equal{});
  }
};

extern std::vector<toc_entry> toc; // NOLINT non-const global

void build_toc(flat_file::database<hibp::pawned_pw>& db, const std::string& db_filename, std::size_t toc_entries);

std::optional<hibp::pawned_pw> toc_search(flat_file::database<hibp::pawned_pw>& db,
                                          const hibp::pawned_pw&                needle);
