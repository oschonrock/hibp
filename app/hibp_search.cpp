#include "CLI/CLI.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "sha1.h"
#include "toc.hpp"
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <ratio>
#include <string>

// test2
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

int main(int argc, char* argv[]) {
  CLI::App app;
  define_options(app);
  CLI11_PARSE(app, argc, argv);

  try {

    flat_file::database<hibp::pawned_pw> db(cli_config.db_filename);

    if (cli_config.toc) {
      build_toc(db, cli_config.db_filename, cli_config.toc_entries);
    }

    SHA1 hash;
    hibp::pawned_pw needle = hibp::convert_to_binary(hash(cli_config.plain_text_password));

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
