#include "flat_file.hpp"
#include "hibp.hpp"
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <exception>
#include <fmt/format.h>
#include <iostream>
#include <string>

struct cli_config_t {
  std::string input_filename;
  bool        sort_by_count = false;
  bool        ntlm          = false;
  std::size_t max_memory    = 1000;
};

void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("-i,--input", cli.input_filename,
                 "The file that the downloaded binary database will be read from")
      ->required();

  app.add_flag("--sort-by-count", cli.sort_by_count,
               "Sort by count (descending). Default is to sort by hash (ascending).");

  app.add_flag("--ntlm", cli.ntlm, "Use ntlm hashes rather than sha1.");

  app.add_option(
      "--max-memory", cli.max_memory,
      fmt::format("The maximum size of each chunk to sort in memory (in MB). The peak memory "
                  "consumption of the process will be about two times this value. Smaller values "
                  "will, result in more chunks being written to disk, which is slower."
                  "(default = {}MB)",
                  cli.max_memory));
}

template <hibp::pw_type PwType>
std::string sort_db(const cli_config_t& cli) {
  flat_file::database<PwType> db(cli.input_filename, 4096 / sizeof(PwType));

  auto max_mem_bytes = cli.max_memory * 1024 * 1024;

  std::string sorted_filename;
  if (cli.sort_by_count) {
    std::cerr << "Sorting by count descending\n";
    sorted_filename = db.disksort(
        [](auto& a, auto& b) {
          if (a.count == b.count) return a < b; // fall back to hash asc for stability
          return a.count > b.count;
        },
        {}, max_mem_bytes);
  } else {
    sorted_filename = db.disksort({}, {}, max_mem_bytes);
  }
  return sorted_filename;
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app("Specialised disk sort for binary HIBP databases.");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    std::string sorted_filename;
    if (cli.ntlm) {
      sorted_filename = sort_db<hibp::pawned_pw_ntlm>(cli);
    } else {
      sorted_filename = sort_db<hibp::pawned_pw_sha1>(cli);
    }
    std::cerr << "Done. Sorted data was written to " << sorted_filename << "\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
