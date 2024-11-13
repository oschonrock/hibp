#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

struct cli_config_t {
  std::string output_filename;
  std::string input_filename;
  bool        force           = false;
  bool        standard_output = false;
  bool        sort_by_count   = false;
  std::size_t max_memory      = 1000;
};

void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("-i,--input", cli.input_filename,
                 "The file that the downloaded binary database will be read from")
      ->required();

  app.add_flag("-f,--force", cli.force, "Overwrite any existing output file!");

  app.add_flag("--sort-by-count", cli.sort_by_count,
               "Sort by count (descending). Default is sort by hash.");

  app.add_option("--max-memory", cli.max_memory,
                 std::format("Use this maximum number of megabytes to sort each chunk of the "
                             "database. (default = {}MB)",
                             cli.max_memory));
}

std::ifstream get_input_stream(const std::string& input_filename) {
  auto input_stream = std::ifstream(input_filename);
  if (!input_stream) {
    throw std::runtime_error(std::format("Error opening '{}' for reading. Because: \"{}\".\n",
                                         input_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return input_stream;
}

std::ofstream get_output_stream(const std::string& output_filename, bool force) {
  if (!force && std::filesystem::exists(output_filename)) {
    throw std::runtime_error(
        std::format("File '{}' exists. Use `--force` to overwrite.", output_filename));
  }

  auto output_stream = std::ofstream(output_filename, std::ios_base::binary);
  if (!output_stream) {
    throw std::runtime_error(std::format("Error opening '{}' for writing. Because: \"{}\".\n",
                                         output_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return output_stream;
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app;
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
