#include "bytearray_cast.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <CLI/CLI.hpp>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fmt/chrono.h> // IWYU pragma: keep
#include <fmt/format.h>
#include <iostream>
#include <string>
// #include <unordered_set>

struct cli_config_t {
  std::string db_filename;
  bool        ntlm = false;
  unsigned    bits = 64; // 1Mega chapters
};

void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("db_filename", cli.db_filename,
                 "The file that contains the binary database you downloaded")
      ->required();

  app.add_option("--bits", cli.bits,
                 fmt::format("Specify how may bits you want to use for dupe searching. default {}",
                             cli.bits))
      ->check(CLI::Range(32, 64));

  app.add_flag("--ntlm", cli.ntlm, "Use ntlm hashes rather than sha1.");
}

template <hibp::pw_type PwType>
void run_search(const cli_config_t& cli) {
  flat_file::database<PwType> db(cli.db_filename, (1U << 16U) / sizeof(PwType));

  std::cout << fmt::format("Looking for duplicates in the first {} bits of the hash...\n",
                           cli.bits);
  std::uint64_t last = -1;
  for (auto& pw: db) {
    auto prefix = hibp::bytearray_cast<std::uint64_t>(pw.hash.data()) >> (64 - cli.bits);
    if (prefix == last) {
      std::cout << fmt::format("{:016X} is a dupe (orig record: {})\n", prefix, pw.to_string());
    }
    last = prefix;
  }
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
