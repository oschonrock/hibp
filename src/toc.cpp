#include "toc.hpp"
#include "bytearray_cast.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/std.h> // IWYU pragma: keep
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hibp {

namespace details {

using toc_entry = std::uint32_t; // limited to 4Billion pws. will throw when too big

// one instance per type of pw
template <pw_type PwType>
std::vector<toc_entry> toc;

template <pw_type PwType>
std::uint32_t pw_to_prefix(const PwType& pw, unsigned bits) {
  return hibp::bytearray_cast<std::uint32_t>(pw.hash.data()) >>
         (sizeof(toc_entry) * 8 - bits);
}

template <pw_type PwType>
void build(const std::filesystem::path& db_path, unsigned bits) {
  // big buffer for sequential read
  flat_file::database<PwType> db(db_path, (1U << 16U) / sizeof(PwType));

  std::size_t toc_entries = 1UL << bits; // default = 1Mega entries (just like the files)

  auto last_pw_prefix = pw_to_prefix(db.back(), bits);
  if (last_pw_prefix + 1 < toc_entries) {
    std::cout << fmt::format("Warning: DB is partial, reduced size toc.\n");
    toc_entries = last_pw_prefix + 1;
  }

  const std::size_t db_size = db.number_records();
  if (db_size > std::numeric_limits<toc_entry>::max()) {
    throw std::runtime_error(fmt::format("Fatal: ToC value type is too small for this db"));
  }
  const std::size_t toc_entry_size = db_size / toc_entries;
  std::cout << fmt::format("{:30s} {:15d} records\n", "DB size", db_size);
  std::cout << fmt::format("{:30s} {:15.0f} per query\n", "Max disk reads without ToC",
                           std::ceil(std::log2(db_size)));
  std::cout << fmt::format("{:30s} {:15d}\n", "Number of bits in ToC prefix", bits);
  std::cout << fmt::format("{:30s} {:15d} ({:.1f}MB consumed)\n", "Number of ToC entries",
                           toc_entries,
                           static_cast<double>(toc_entries * sizeof(toc_entry)) / pow(2, 20));
  std::cout << fmt::format("{:30s} {:15d} records in db (avg)\n", "Each ToC entry covers",
                           toc_entry_size);
  std::cout << fmt::format("{:30s} {:15.0f} per query\n", "Max disk reads with ToC",
                           std::ceil(std::log2(toc_entry_size)));
  toc<PwType>.reserve(toc_entries);

  toc_entry last_pos = 0;
  for (std::uint32_t prefix = 0; prefix != toc_entries; prefix++) {
    auto found_iter = std::find_if(db.begin() + last_pos, db.end(), [=](const PwType& pw) {
      return pw_to_prefix(pw, bits) == prefix;
    });
    if (found_iter == db.end()) {
      throw std::runtime_error(fmt::format("Missing prefix {:05X}. There must be a gap. Probably "
                                           "corrupt data. Cannot build table of contents",
                                           prefix));
    }
    last_pos = static_cast<toc_entry>(found_iter - db.begin()); // range checked above
    toc<PwType>.push_back(last_pos);
    if (prefix % 1000 == 0) {
      std::cout << fmt::format("{:30s} {:14.1f}%\r", "Building table of contents",
                               prefix * 100 / static_cast<double>(toc_entries))
                << std::flush;
    }
  }
  std::cout << "\n";
}

template <pw_type PwType>
void save(const std::filesystem::path& toc_filename) {
  std::cout << fmt::format("saving table of contents: {}\n", toc_filename);
  auto toc_stream = std::ofstream(toc_filename, std::ios_base::binary);
  toc_stream.write(reinterpret_cast<char*>(toc<PwType>.data()), // NOLINT reincast
                   static_cast<std::streamsize>(sizeof(toc_entry) * toc<PwType>.size()));
}

template <pw_type PwType>
void load(const std::filesystem::path& toc_filename) {
  std::cout << fmt::format("loading table of contents: {}\n", toc_filename);
  const auto toc_file_size = static_cast<std::size_t>(std::filesystem::file_size(toc_filename));
  auto       toc_stream    = std::ifstream(toc_filename, std::ios_base::binary);
  toc<PwType>              = std::vector<toc_entry>(toc_file_size / sizeof(toc_entry));
  toc_stream.read(reinterpret_cast<char*>(toc<PwType>.data()), // NOLINT reincast
                  static_cast<std::streamsize>(toc_file_size));
}

template <pw_type PwType>
std::optional<PwType> search(flat_file::database<PwType>& db, const PwType& needle, unsigned bits) {
  const std::uint32_t pw_prefix = pw_to_prefix(needle, bits);

  if (pw_prefix >= toc<PwType>.size()) {
    return {}; // must be partial db & toc, and therefore "not found"
  }

  const std::size_t begin_offset = toc<PwType>[pw_prefix];
  const std::size_t end_offset =
      pw_prefix + 1 < toc<PwType>.size() ? toc<PwType>[pw_prefix + 1] : db.number_records();

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
template <pw_type PwType>
void toc_build(const std::filesystem::path& db_filename, unsigned bits) {

  const std::string toc_filename = fmt::format("{}.{}.toc", db_filename.string(), bits);

  if (!std::filesystem::exists(toc_filename) || (std::filesystem::last_write_time(toc_filename) <=
                                                 std::filesystem::last_write_time(db_filename))) {

    details::build<PwType>(db_filename, bits);
    details::save<PwType>(toc_filename);
  } else {
    details::load<PwType>(toc_filename);
  }
}

template <pw_type PwType>
std::optional<PwType> toc_search(flat_file::database<PwType>& db, const PwType& needle,
                                 unsigned bits) {

  return details::search(db, needle, bits);
}

// explicit instantiations for public API

// sha1

template void toc_build<hibp::pawned_pw_sha1>(const std::filesystem::path& db_filename,
                                              unsigned                     bits);

template std::optional<hibp::pawned_pw_sha1>
toc_search<hibp::pawned_pw_sha1>(flat_file::database<hibp::pawned_pw_sha1>& db,
                                 const hibp::pawned_pw_sha1& needle, unsigned bits);

// ntlm

template void toc_build<hibp::pawned_pw_ntlm>(const std::filesystem::path& db_filename,
                                              unsigned                     bits);

template std::optional<hibp::pawned_pw_ntlm>
toc_search<hibp::pawned_pw_ntlm>(flat_file::database<hibp::pawned_pw_ntlm>& db,
                                 const hibp::pawned_pw_ntlm& needle, unsigned bits);

// sha1t64
template void toc_build<hibp::pawned_pw_sha1t64>(const std::filesystem::path& db_filename,
                                                 unsigned                     bits);

template std::optional<hibp::pawned_pw_sha1t64>
toc_search<hibp::pawned_pw_sha1t64>(flat_file::database<hibp::pawned_pw_sha1t64>& db,
                                    const hibp::pawned_pw_sha1t64& needle, unsigned bits);

} // namespace hibp
