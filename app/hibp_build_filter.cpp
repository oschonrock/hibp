#include "arrcmp.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "sharded_binary_fuse_filter.hpp"
#include <CLI/CLI.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/mman.h>

struct cli_config_t {
  std::string output_filename;
  std::string input_filename;
  bool        force  = false;
  bool        ntlm   = false;
  bool        verify = true;
  std::size_t limit  = -1; // ie max
};

void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("-i,--input", cli.input_filename,
                 "The file that the downloaded binary database will be read from");

  app.add_option("-o,--output", cli.output_filename,
                 "The file that the downloaded binary database will be written to");

  app.add_option("-l,--limit", cli.limit,
                 "The maximum number of records that will be converted (default: all)");

  // app.add_flag("--ntlm", cli.ntlm, "Use ntlm hashes rather than sha1.");

  app.add_flag("-f,--force", cli.force, "Overwrite any existing output file!");
}

std::ifstream get_input_stream(const std::string& input_filename) {
  auto input_stream = std::ifstream(input_filename);
  if (!input_stream) {
    throw std::runtime_error(fmt::format("Error opening '{}' for reading. Because: \"{}\".\n",
                                         input_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return input_stream;
}

std::ofstream get_output_stream(const std::string& output_filename, bool force) {
  if (!force && std::filesystem::exists(output_filename)) {
    throw std::runtime_error(
        fmt::format("File '{}' exists. Use `--force` to overwrite.", output_filename));
  }

  auto output_stream = std::ofstream(output_filename, std::ios_base::binary);
  if (!output_stream) {
    throw std::runtime_error(fmt::format("Error opening '{}' for writing. Because: \"{}\".\n",
                                         output_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return output_stream;
}

// void check_options(const cli_config_t& cli) {

// }

void build(const cli_config_t& cli) {
  flat_file::database<hibp::pawned_pw_sha1> db{cli.input_filename,
                                               (1U << 16U) / sizeof(hibp::pawned_pw_sha1)};

  unsigned count = 0;

  std::vector<std::uint64_t> hashes;
  hashes.reserve(std::min(db.number_records(), cli.limit));
  for (const auto& record: db) {
    hashes.emplace_back(arrcmp::impl::bytearray_cast<std::uint64_t>(record.hash.data()));
    count++;
    if (count == cli.limit) break;
  }

  binfuse::filter16 filter(hashes);
  filter.verify(hashes);
  std::cout << fmt::format("estimated false positive rate: {:.5f}%\n", filter.estimate_false_positive_rate());
  
  get_output_stream(cli.output_filename, cli.force); // just "touch" and close again
  binfuse::sharded_filter16_sink sharded_filter(cli.output_filename);
  sharded_filter.add(std::move(filter),0);
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app("Building binary_fuse_filters");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    // check_options(cli);

    build(cli);

  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
