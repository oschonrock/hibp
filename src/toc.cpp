#include "flat_file.hpp"
#include "hibp.hpp"
#include "toc.hpp"
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

std::vector<toc_entry> toc; // NOLINT non-const global

void build_toc(flat_file::database<hibp::pawned_pw>& db, const std::string& db_filename, std::size_t toc_entries) {

  std::string toc_filename = std::format("{}.{}.toc", db_filename, toc_entries);
  
  if (!std::filesystem::exists(toc_filename) || (std::filesystem::last_write_time(toc_filename) <=
                                                 std::filesystem::last_write_time(db_filename))) {

    // build
    std::size_t db_size        = db.number_records();
    std::size_t toc_entry_size = db_size / toc_entries;
    std::cerr << std::format("{:25s} {:15d} records\n", "db_size", db_size);
    std::cerr << std::format("{:25s} {:15d}\n", "number of toc entries", toc_entries);
    std::cerr << std::format("{:25s} {:15d} records in db\n", "each toc_entry covers", toc_entry_size);
    std::cerr << std::format("building table of contents..\n");
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
    std::cerr << std::format("loading table of contents..\n");
    auto toc_file_size = std::filesystem::file_size(toc_filename);
    auto toc_stream    = std::ifstream(toc_filename, std::ios_base::binary);
    toc                = std::vector<toc_entry>{toc_file_size / sizeof(toc_entry)};
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

