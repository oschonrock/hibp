#include "CLI/CLI.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "sha1.h"
#include "toc.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fmt/format.h>
#include <iostream>
#include <optional>
#include <ratio>
#include <stdexcept>
#include <string>

struct cli_config_t {
  std::string db_filename;
  std::string plain_text_password;
  bool        toc         = false;
  std::size_t toc_entries = 1U << 16U; // 64k chapters
  bool        toc2        = false;
  unsigned    toc2_bits   = 20; // 1Mega chapters
};

void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("db_filename", cli.db_filename,
                 "The file that contains the binary database you downloaded")
      ->required();

  app.add_option("plain-text-password", cli.plain_text_password,
                 "The plain text password that you want to check for in the provided hibp_db")
      ->required();

  app.add_flag("--toc", cli.toc, "Use a table of contents for extra performance.");

  app.add_option(
      "--toc-entries", cli.toc_entries,
      fmt::format("Specify how may table of contents entries to use. default {}", cli.toc_entries));

  app.add_flag("--toc2", cli.toc2,
               "Use a bit mask oriented table of contents for extra performance.");

  app.add_option("--toc2-bits", cli.toc2_bits,
                 fmt::format("Specify how may bits are to be used to mask the entries. default {}",
                             cli.toc2_bits));
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app;
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    if ((cli.toc && cli.toc2)) {
      throw std::runtime_error("You can't use --toc and --toc2 together");
    }
    
    flat_file::database<hibp::pawned_pw> db(cli.db_filename, 4096 / sizeof(hibp::pawned_pw));

    if (cli.toc) {
      build_toc(cli.db_filename, cli.toc_entries);
    } else if (cli.toc2) {
      build_toc2(cli.db_filename, cli.toc2_bits);
    }

    SHA1            hash;
    const hibp::pawned_pw needle = hibp::convert_to_binary(hash(cli.plain_text_password));

    std::optional<hibp::pawned_pw> maybe_ppw;

    using clk          = std::chrono::high_resolution_clock;
    using double_milli = std::chrono::duration<double, std::milli>;
    auto start_time    = clk::now();
    if (cli.toc) {
      maybe_ppw = toc_search(db, needle);
    } else if (cli.toc2) {
      maybe_ppw = toc2_search(db, needle, cli.toc2_bits);
    } else if (auto iter = std::lower_bound(db.begin(), db.end(), needle);
               iter != db.end() && *iter == needle) {
      maybe_ppw = *iter;
    }
    std::cout << fmt::format(
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
