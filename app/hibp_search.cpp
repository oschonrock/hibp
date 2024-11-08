#include "CLI/CLI.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "sha1.hpp"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ratio>
#include <string>
#include <vector>

struct cli_config_t {
  std::string db_filename;
  std::string plain_text_password;
  bool        toc         = false;
  std::size_t toc_entries = 1U << 16U; // 64k chapters
};

static cli_config_t cli_config; // NOLINT non-const global

void define_options(CLI::App& app) {

  app.add_option("output_db_filename", cli_config.db_filename,
                 "The file that contains the binary database you downloaded")
      ->required();

  app.add_option("plain-text-password", cli_config.plain_text_password,
                 "The plain text password that you want to check for in the provided hibp_db")
      ->required();

  app.add_flag("--toc", cli_config.toc, "Use a table of contents for extra performance.");

  app.add_option("--toc-entries", cli_config.toc_entries,
                 std::format("Specify how may table of contents entries to use. default {}", cli_config.toc_entries));
}

struct toc_entry {
  std::size_t               start;
  std::array<std::byte, 20> hash;

  friend std::ostream& operator<<(std::ostream& os, const toc_entry& rhs) {
    os << std::format("{:10d}: ", rhs.start);
    for (auto&& b: rhs.hash) os << std::format("{:02X}", static_cast<unsigned>(b));
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

void build_toc(flat_file::database<hibp::pawned_pw>& db, const std::string& db_filename) {

  std::string toc_filename = std::format("{}.{}.toc", db_filename, cli_config.toc_entries);
  
  if (!std::filesystem::exists(toc_filename) || (std::filesystem::last_write_time(toc_filename) <=
                                                 std::filesystem::last_write_time(db_filename))) {

    // build
    std::size_t db_size        = db.number_records();
    std::size_t toc_entry_size = db_size / cli_config.toc_entries;
    std::cerr << std::format("db_size {}\n", db_size);
    std::cout << std::format("number of toc entries = {}\n", cli_config.toc_entries);
    std::cerr << std::format("each toc_entry covers {} entries in db\n", toc_entry_size);
    toc.reserve(cli_config.toc_entries);

    for (unsigned i = 0; i != cli_config.toc_entries; i++) {
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

int main(int argc, char* argv[]) {
  CLI::App app;
  define_options(app);
  CLI11_PARSE(app, argc, argv);

  try {

    flat_file::database<hibp::pawned_pw> db(cli_config.db_filename);

    if (cli_config.toc) {
      build_toc(db, cli_config.db_filename);
    }

    SHA1 sha1;
    sha1.update(cli_config.plain_text_password);
    hibp::pawned_pw needle = hibp::convert_to_binary(sha1.final());

    std::optional<hibp::pawned_pw> maybe_ppw;

    using clk          = std::chrono::high_resolution_clock;
    using double_milli = std::chrono::duration<double, std::milli>;
    auto start_time    = clk::now();
    if (cli_config.toc) {
      maybe_ppw = toc_search(db, needle);
    } else if (auto iter = std::lower_bound(db.begin(), db.end(), needle);
               iter != db.end() && *iter == needle) {
      maybe_ppw = *iter;
    }
    std::cout << std::format(
        "search took {:.1f}ms\n",
        std::chrono::duration_cast<double_milli>(clk::now() - start_time).count());

    std::cout << "needle = " << needle << "\n";
    if (maybe_ppw)
      std::cout << "found  = " << *maybe_ppw << "\n";
    else
      std::cout << "not found\n";

  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
