#include "bytearray_cast.hpp"
#include "binfuse/sharded_filter.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <CLI/CLI.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <string>

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

void build(const cli_config_t& cli) {
  flat_file::database<hibp::pawned_pw_sha1> db{cli.input_filename,
                                               (1U << 16U) / sizeof(hibp::pawned_pw_sha1)};

  unsigned count = 0;

  binfuse::sharded_filter8_sink sharded_filter(cli.output_filename);

  sharded_filter.stream_prepare();
  for (const auto& record: db) {
    auto key = hibp::bytearray_cast<std::uint64_t>(record.hash.data());
    sharded_filter.stream_add(key);
    count++;
    if (count == cli.limit) {
      break;
    }
  }
  sharded_filter.stream_finalize();
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app("Building binary_fuse_filters");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    build(cli);

  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
