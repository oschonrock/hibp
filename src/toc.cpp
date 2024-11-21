#include "toc.hpp"
#include "arrcmp.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace hibp {

namespace details {

using toc_entry = unsigned; // limited to 4Billion pws. will throw when too big
std::vector<toc_entry> toc;

unsigned pw_to_prefix(const hibp::pawned_pw& pw, unsigned bits) {
  return arrcmp::impl::bytearray_cast<unsigned>(pw.hash.data()) >> (sizeof(toc_entry) * 8 - bits);
}

void build(const std::string& db_filename, unsigned bits) {
  // big buffer for sequential read
  flat_file::database<hibp::pawned_pw> db(db_filename, (1U << 16U) / sizeof(hibp::pawned_pw));

  std::size_t toc_entries = 1UL << bits; // default = 1Mega entries (just like the files)

  auto last_pw_prefix = pw_to_prefix(db.back(), bits);
  if (last_pw_prefix + 1 < toc_entries) {
    std::cerr << fmt::format("Warning: DB is partial, reduced size toc.\n");
    toc_entries = last_pw_prefix + 1;
  }

  const std::size_t db_size = db.number_records();
  if (db_size > std::numeric_limits<toc_entry>::max()) {
    throw std::runtime_error(fmt::format("Fatal: toc value type is too small for this db"));
  }
  const std::size_t toc_entry_size = db_size / toc_entries;
  std::cerr << fmt::format("{:25s} {:15d} records\n", "db_size", db_size);
  std::cerr << fmt::format("{:25s} {:15d}\n", "number bits in mask", bits);
  std::cerr << fmt::format("{:25s} {:15d}\n", "number of toc entries", toc_entries);
  std::cerr << fmt::format("{:25s} {:15d} records in db on average\n", "each toc_entry covers",
                           toc_entry_size);
  std::cerr << fmt::format("building table of contents MK2..\n");
  toc.reserve(toc_entries);

  unsigned last_pos = 0;
  for (unsigned prefix = 0; prefix != toc_entries; prefix++) {
    auto found_iter = std::find_if(db.begin() + last_pos, db.end(), [=](const hibp::pawned_pw& pw) {
      return pw_to_prefix(pw, bits) == prefix;
    });
    if (found_iter == db.end()) {
      throw std::runtime_error(fmt::format("Missing prefix {:05X}. There must be a gap. Probably "
                                           "corrupt data. Cannot build table of contents",
                                           prefix));
    }
    last_pos = static_cast<toc_entry>(found_iter - db.begin());
    toc.push_back(last_pos);
  }
}

void save(const std::string& toc_filename) {
  auto toc_stream = std::ofstream(toc_filename, std::ios_base::binary);
  toc_stream.write(reinterpret_cast<char*>(toc.data()), // NOLINT reincast
                   static_cast<std::streamsize>(sizeof(toc_entry) * toc.size()));
}

void load(const std::string& toc_filename) {
  std::cerr << fmt::format("loading table of contents..\n");
  const auto toc_file_size = std::filesystem::file_size(toc_filename);
  auto       toc_stream    = std::ifstream(toc_filename, std::ios_base::binary);
  toc                      = std::vector<toc_entry>(toc_file_size / sizeof(toc_entry));
  toc_stream.read(reinterpret_cast<char*>(toc.data()), // NOLINT reincast
                  static_cast<std::streamsize>(toc_file_size));
}

std::optional<hibp::pawned_pw> search(flat_file::database<hibp::pawned_pw>& db,
                                      const hibp::pawned_pw& needle, unsigned bits) {
  const unsigned pw_prefix = pw_to_prefix(needle, bits);

  if (pw_prefix >= toc.size()) {
    return {}; // must be partial db & toc, and therefore "not found"
  }

  const std::size_t begin_offset = toc[pw_prefix];
  const std::size_t end_offset =
      pw_prefix + 1 < toc.size() ? toc[pw_prefix + 1] : db.number_records();

  if (auto iter = std::lower_bound(db.begin() + begin_offset, db.begin() + end_offset, needle);
      iter != db.end() && *iter == needle) {
    return *iter; // found!
  }
  return {}; // not found;
}
} // namespace details

// TOC: "Table of contents"
//
// bit masks the needle's pw_hash to index into a table of db positions
// effectively the same as selecting one of the published files to download
// but all in a single file and therefore much lower syscall i/o overhead
void toc_build(const std::string& db_filename, unsigned bits) {

  const std::string toc_filename = fmt::format("{}.{}.toc", db_filename, bits);

  if (!std::filesystem::exists(toc_filename) || (std::filesystem::last_write_time(toc_filename) <=
                                                 std::filesystem::last_write_time(db_filename))) {

    details::build(db_filename, bits);
    details::save(toc_filename);
  } else {
    details::load(toc_filename);
  }
}

std::optional<hibp::pawned_pw> toc_search(flat_file::database<hibp::pawned_pw>& db,
                                          const hibp::pawned_pw& needle, unsigned bits) {

  return details::search(db, needle, bits);
}
} // namespace hibp
