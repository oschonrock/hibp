#include "toc.hpp"
#include "arrcmp.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

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

static std::vector<toc_entry> toc; // NOLINT non-const global

void build_toc(const std::string& db_filename, std::size_t toc_entries) {

  std::string toc_filename = fmt::format("{}.{}.toc", db_filename, toc_entries);

  if (!std::filesystem::exists(toc_filename) || (std::filesystem::last_write_time(toc_filename) <=
                                                 std::filesystem::last_write_time(db_filename))) {

    // build
    flat_file::database<hibp::pawned_pw> db(db_filename, 4096 / sizeof(hibp::pawned_pw));
    
    std::size_t db_size        = db.number_records();
    std::size_t toc_entry_size = db_size / toc_entries;
    std::cerr << fmt::format("{:25s} {:15d} records\n", "db_size", db_size);
    std::cerr << fmt::format("{:25s} {:15d}\n", "number of toc entries", toc_entries);
    std::cerr << fmt::format("{:25s} {:15d} records in db\n", "each toc_entry covers",
                             toc_entry_size);
    std::cerr << fmt::format("building table of contents..\n");
    toc.reserve(toc_entries);

    for (unsigned i = 0; i != toc_entries; i++) {
      std::size_t start = i * toc_entry_size;

      const auto& pw = *(db.begin() + start);
      toc.emplace_back(start, pw.hash);
    }

    // save
    auto toc_stream = std::ofstream(toc_filename, std::ios_base::binary);
    toc_stream.write(reinterpret_cast<char*>(toc.data()), // NOLINT reincast
                     static_cast<std::streamsize>(sizeof(toc_entry) * toc.size()));
  } else {
    // load
    std::cerr << fmt::format("loading table of contents..\n");
    auto toc_file_size = std::filesystem::file_size(toc_filename);
    auto toc_stream    = std::ifstream(toc_filename, std::ios_base::binary);
    toc                = std::vector<toc_entry>(toc_file_size / sizeof(toc_entry));
    toc_stream.read(reinterpret_cast<char*>(toc.data()), // NOLINT reincast
                    static_cast<std::streamsize>(toc_file_size));
  }
}

std::optional<hibp::pawned_pw> toc_search(flat_file::database<hibp::pawned_pw>& db,
                                          const hibp::pawned_pw&                needle) {

  // special case: empty db
  if (db.number_records() == 0) return {};

  toc_entry needle_te{0, needle.hash};
  auto      toc_end = std::lower_bound(toc.begin(), toc.end(), needle_te);

  std::size_t db_start_search_index{};
  if (toc_end == toc.begin()) {
    // special case 1st record
    db_start_search_index = 0;
  } else {
    db_start_search_index = std::prev(toc_end)->start;
  }

  std::size_t db_end_search_index{};

  if (toc_end == toc.end()) {
    // special case last TOC entry (last "chapter")
    db_end_search_index = db.number_records();
  } else {
    db_end_search_index = toc_end->start;
    if (*toc_end == needle_te) {
      // special case on TOC entry boundary, we can avoid work
      return *(db.begin() + toc_end->start);
    }
  }

  if (auto iter = std::lower_bound(db.begin() + db_start_search_index,
                                   db.begin() + db_end_search_index, needle);
      iter != db.end() && *iter == needle) {
    return *iter;
  }
  return {};
}

// TOC2
//
// bit masks the needles pw_hash to index into a table of db positions
// effectively the same as selecting one of the published files to download

using toc2_entry = unsigned;
static std::vector<toc2_entry> toc2; // NOLINT non-const global

static unsigned pw_to_prefix(const hibp::pawned_pw& pw, unsigned bits) {
  return arrcmp::impl::bytearray_cast<unsigned>(pw.hash.data()) >> (sizeof(toc2_entry) * 8 - bits);
}

void build_toc2(const std::string& db_filename, unsigned bits) {

  const std::string toc_filename = fmt::format("{}.{}.toc2", db_filename, bits);

  if (!std::filesystem::exists(toc_filename) || (std::filesystem::last_write_time(toc_filename) <=
                                                 std::filesystem::last_write_time(db_filename))) {

    // build
    // big buffer for sequential read
    flat_file::database<hibp::pawned_pw> db(db_filename, (1U << 16U) / sizeof(hibp::pawned_pw));

    std::size_t toc_entries = 1UL << bits; // defaultt = 1Mega entries (just like the files)

    auto last_pw_prefix = pw_to_prefix(db.back(), bits);
    if (last_pw_prefix + 1 < toc_entries) {
      std::cerr << fmt::format("Warning: DB is partial, reduced size toc2.\n");
      toc_entries = last_pw_prefix + 1;
    }

    const std::size_t db_size = db.number_records();
    if (db_size > std::numeric_limits<toc2_entry>::max()) {
      throw std::runtime_error(fmt::format("Fatal: toc value type is too small for this db"));
    }
    const std::size_t toc_entry_size = db_size / toc_entries;
    std::cerr << fmt::format("{:25s} {:15d} records\n", "db_size", db_size);
    std::cerr << fmt::format("{:25s} {:15d}\n", "number bits in mask", bits);
    std::cerr << fmt::format("{:25s} {:15d}\n", "number of toc entries", toc_entries);
    std::cerr << fmt::format("{:25s} {:15d} records in db on average\n", "each toc_entry covers",
                             toc_entry_size);
    std::cerr << fmt::format("building table of contents MK2..\n");
    toc2.reserve(toc_entries);

    unsigned last_pos = 0;
    for (unsigned prefix = 0; prefix != toc_entries; prefix++) {
      auto found_iter =
          std::find_if(db.begin() + last_pos, db.end(),
                       [=](const hibp::pawned_pw& pw) { return pw_to_prefix(pw, bits) == prefix; });
      if (found_iter == db.end()) {
        throw std::runtime_error(fmt::format("Missing prefix {:05X}. There must be gap. Probably "
                                             "corrupt data. Cannot build table of contents",
                                             prefix));
      }
      last_pos = static_cast<toc2_entry>(found_iter - db.begin());
      toc2.push_back(last_pos);
    }

    // save
    auto toc_stream = std::ofstream(toc_filename, std::ios_base::binary);
    toc_stream.write(reinterpret_cast<char*>(toc2.data()), // NOLINT reincast
                     static_cast<std::streamsize>(sizeof(toc2_entry) * toc2.size()));
  } else {
    // load
    std::cerr << fmt::format("loading table of contents..\n");
    auto toc_file_size = std::filesystem::file_size(toc_filename);
    auto toc_stream    = std::ifstream(toc_filename, std::ios_base::binary);
    toc2               = std::vector<toc2_entry>(toc_file_size / sizeof(toc2_entry));
    toc_stream.read(reinterpret_cast<char*>(toc2.data()), // NOLINT reincast
                    static_cast<std::streamsize>(toc_file_size));
  }
}

std::optional<hibp::pawned_pw> toc2_search(flat_file::database<hibp::pawned_pw>& db,
                                           const hibp::pawned_pw& needle, unsigned bits) {

  unsigned pw_prefix = pw_to_prefix(needle, bits);

  if (pw_prefix >= toc2.size()) {
    // must be partial db, and no result
    return {};
  }

  std::size_t db_start_search_index = toc2[pw_prefix];
  std::size_t db_end_search_index{};
  if (pw_prefix + 1 < toc2.size()) {
    db_end_search_index = toc2[pw_prefix + 1];
  } else {
    db_end_search_index = db.number_records();
  }

  if (auto iter = std::lower_bound(db.begin() + db_start_search_index,
                                   db.begin() + db_end_search_index, needle);
      iter != db.end() && *iter == needle) {
    return *iter;
  }
  return {};
}
