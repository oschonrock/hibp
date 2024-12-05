#include "arrcmp.hpp"
#include "binaryfusefilter.h"
#include "flat_file.hpp"
#include "hibp.hpp"
#include "mio/mmap.hpp"
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
#include <istream>
#include <ostream>
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
  std::istream* input_stream = &std::cin;
  std::ifstream ifs;
  ifs                           = get_input_stream(cli.input_filename);
  input_stream                  = &ifs;
  std::string input_stream_name = cli.input_filename;

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

  auto           start = clock();
  binary_fuse8_t filter;
  if (!binary_fuse8_allocate(count, &filter)) {
    throw std::runtime_error("failed to allocate memory.\n");
  }
  if (!binary_fuse8_populate(hashes.data(), count, &filter)) {
    throw std::runtime_error("failed to build the filter, do you have sufficient memory?\n");
  }
  auto end = clock();
  std::cout << fmt::format("Done in {:.3f} seconds.\n",
                           static_cast<float>(end - start) / CLOCKS_PER_SEC);
  if (cli.verify) {
    std::cout << fmt::format("Checking for false negatives\n");
    for (size_t i = 0; i < count; i++) {
      if (!binary_fuse8_contain(hashes[i], &filter)) {
        throw std::runtime_error("Detected a false negative. You probably have a bug. "
                                 "Aborting.\n");
      }
    }
    std::cout << fmt::format("Verified with success: no false negatives\n");
    size_t matches = 0;
    size_t volume  = 100000;
    for (size_t t = 0; t < volume; t++) {
      if (binary_fuse8_contain(t * 10001 + 13 + count, &filter)) {
        matches++;
      }
    }
    std::cout << fmt::format("estimated false positive rate: {:.3f} percent\n",
                             static_cast<double>(matches) * 100.0 / static_cast<double>(volume));
  }

  size_t filtersize = binary_fuse8_serialization_bytes(&filter);
  std::cout << fmt::format("filter will occupy {} bytes\n", filtersize, filtersize);

  auto ofs = get_output_stream(cli.output_filename, cli.force);
  std::filesystem::resize_file(cli.output_filename, filtersize);
  mio::mmap_sink map(cli.output_filename);
  binary_fuse8_serialize(&filter, map.data());
  binary_fuse8_free(&filter);
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app("Converting 'Have I been pawned' databases between text and binary formats");
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
