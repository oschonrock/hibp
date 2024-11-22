#include "flat_file.hpp"
#include "hibp.hpp"
#include "ntlm.hpp"
#include "toc.hpp"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fmt/chrono.h> // IWYU pragma: keep
#include <fmt/format.h>
#include <iostream>
#include <optional>
#include <ratio>
#include <sha1.h>
#include <stdexcept>
#include <string>
#include <type_traits>

struct cli_config_t {
  std::string db_filename;
  std::string plain_text_password;
  bool        toc      = false;
  bool        hash     = false;
  bool        ntlm     = false;
  unsigned    toc_bits = 20; // 1Mega chapters
};

void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("db_filename", cli.db_filename,
                 "The file that contains the binary database you downloaded")
      ->required();

  app.add_option("plain-text-password", cli.plain_text_password,
                 "The plain text password that you want to check for in the provided hibp_db")
      ->required();

  app.add_flag("--hash", cli.hash,
               "Provide a hash on command line, instead of a plaintex password.");

  app.add_flag("--ntlm", cli.ntlm, "Use ntlm hashes rather than sha1.");

  app.add_flag("--toc", cli.toc,
               "Use a bit mask oriented table of contents for extra performance.");

  app.add_option("--toc-bits", cli.toc_bits,
                 fmt::format("Specify how may bits to use for table of content mask. default {}",
                             cli.toc_bits));
}

template <hibp::pw_type PwType>
void run_search(const cli_config_t& cli) {
  flat_file::database<PwType> db(cli.db_filename, 4096 / sizeof(PwType));

  if (cli.toc) {
    hibp::toc_build<PwType>(cli.db_filename, cli.toc_bits);
  }

  PwType needle;
  if constexpr (std::is_same_v<PwType, hibp::pawned_pw_ntlm>) {
    if (cli.hash) {
      if (!hibp::is_valid_hash(cli.plain_text_password, 32)) {
        throw std::runtime_error("Not a valid ntlm hash.");
      }
      needle = {cli.plain_text_password};
    } else {
      needle.hash = hibp::ntlm(cli.plain_text_password);
    }
  } else { // sha1
    if (cli.hash) {
      if (!hibp::is_valid_hash(cli.plain_text_password, 40)) {
        throw std::runtime_error("Not a valid sha1 hash.");
      }
      needle = {cli.plain_text_password};
    } else {
      needle = {SHA1{}(cli.plain_text_password)};
    }
  }

  std::optional<PwType> maybe_ppw;

  using clk       = std::chrono::high_resolution_clock;
  using fmilli    = std::chrono::duration<double, std::milli>;
  auto start_time = clk::now();
  if (cli.toc) {
    maybe_ppw = hibp::toc_search<PwType>(db, needle, cli.toc_bits);
  } else if (auto iter = std::lower_bound(db.begin(), db.end(), needle);
             iter != db.end() && *iter == needle) {
    maybe_ppw = *iter;
  }
  std::cout << fmt::format("search took {:.2}\n", duration_cast<fmilli>(clk::now() - start_time));

  std::cout << "needle = " << needle << "\n";
  if (maybe_ppw)
    std::cout << "found  = " << *maybe_ppw << "\n";
  else
    std::cout << "not found\n";
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app;
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    if (cli.ntlm) {
      run_search<hibp::pawned_pw_ntlm>(cli);
    } else {
      run_search<hibp::pawned_pw_sha1>(cli);
    }
  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
