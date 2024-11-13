#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

struct cli_config_t {
  std::string input_filename;
  bool        sort_by_count   = false;
  std::size_t max_memory      = 1000;
};

void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("-i,--input", cli.input_filename,
                 "The file that the downloaded binary database will be read from")
      ->required();

  app.add_flag("--sort-by-count", cli.sort_by_count,
               "Sort by count (descending). Default is to sort by hash (ascending).");

  app.add_option(
      "--max-memory", cli.max_memory,
      std::format("The maximum size of each chunk to sort in memory (in MB). The peak memory "
                  "consumption of the process will be about two times this value. Smaller values "
                  "will, result in more chunks being written to disk, which is slower."
                  "(default = {}MB)",
                  cli.max_memory));
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app("Specialised disk sort for binary HIBP databases.");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    flat_file::database<hibp::pawned_pw> db(cli.input_filename, 1'000);

    auto max_mem_bytes = cli.max_memory * 1024 * 1024;

    std::string sorted_filename;
    if (cli.sort_by_count) {
      std::cerr << "Sorting by count descending\n";
      sorted_filename =
          db.disksort([](auto& a, auto& b) { return a.count > b.count; }, {}, max_mem_bytes);
    } else {
      sorted_filename = db.disksort({}, {}, max_mem_bytes);
    }

    std::cerr << "Done. Sorted data was written to " << sorted_filename << "\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
