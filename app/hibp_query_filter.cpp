#include "arrcmp.hpp"
#include "sharded_binary_fuse_filter.hpp"
#include "hibp.hpp"
#include "sha1.h"
#include <CLI/CLI.hpp>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <ios>
#include <iostream>
#include <mio/mmap.hpp>
#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>

struct cli_config_t {
  std::string db_filename;
  std::string plain_text_password;
  bool        hash = false;
};

void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("db_filename", cli.db_filename, "The file that contains the filter you built.")
      ->required();

  app.add_option("plain-text-password", cli.plain_text_password,
                 "The plain text password that you want to check for in the provided hibp_db")
      ->required();

  app.add_flag("--hash", cli.hash,
               "Provide a hash on command line, instead of a plaintex password.");
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

void query(const cli_config_t& cli) {
  uint64_t needle = 0;
  if (cli.hash) {
    hibp::pawned_pw_sha1t64 pw{cli.plain_text_password};
    needle = arrcmp::impl::bytearray_cast<std::uint64_t>(pw.hash.data());
  } else {
    hibp::pawned_pw_sha1t64 pw{SHA1{}(cli.plain_text_password)};
    needle = arrcmp::impl::bytearray_cast<std::uint64_t>(pw.hash.data());
  }
  std::cout << fmt::format("needle = {:016X}\n", needle);


  sharded_bin_fuse8_filter_source sharded_filter(cli.db_filename);

  bool result = sharded_filter.contains(needle);

  if (result) {
    std::cout << fmt::format("FOUND\n");
  } else {
    std::cout << fmt::format("NOT FOUND\n");
  }

}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app("Converting 'Have I been pawned' databases between text and binary formats");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    query(cli);

  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
