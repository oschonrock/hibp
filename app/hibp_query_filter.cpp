#include "arrcmp.hpp"
#include "binaryfusefilter.h"
#include "hibp.hpp"
#include "sha1.h"
#include <CLI/CLI.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <ios>
#include <iostream>
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

// void check_options(const cli_config_t& cli) {}

void query(const cli_config_t& cli) {

  std::cout << fmt::format("using database: {}\n", cli.db_filename);
  uint64_t hexval = 0;
  if (cli.hash) {
    hibp::pawned_pw_sha1t64 pw{cli.plain_text_password};
    std::cout << pw << "\n";
    hexval = arrcmp::impl::bytearray_cast<std::uint64_t>(pw.hash.data());
  } else {
    std::cout << fmt::format("We are going to hash your input.\n");
    hibp::pawned_pw_sha1t64 pw{SHA1{}(cli.plain_text_password)};
    std::cout << pw << "\n";
    hexval = arrcmp::impl::bytearray_cast<std::uint64_t>(pw.hash.data());
  }
  std::cout << fmt::format("hexval = {:016X}\n", hexval);
  uint64_t cookie = 0;

  uint64_t seed  = 0;
  clock_t  start = clock();

  // could open the file once (map it), instead of this complicated mess.
  FILE* fp = fopen(cli.db_filename.c_str(), "rb");
  if (fp == nullptr) {
    throw std::runtime_error(fmt::format("Cannot read the input file {}.", cli.db_filename));
  }
  // should be done with an enum:
  bool xor8    = false;
  bool bin16   = false;
  bool bloom12 = false;
  // if (fread(&cookie, sizeof(cookie), 1, fp) != 1) std::cout << fmt::format("failed read.\n");
  // if (cookie != 1234569) {
  //   if (cookie == 1234570) {
  //     bin16 = true;
  //   } else {
  //     throw std::runtime_error(
  //         fmt::format("Not a filter file. Cookie found: %llu.\n", (long long unsigned
  //         int)cookie));
  //   }
  // }
  if (fread(&seed, sizeof(seed), 1, fp) != 1) std::cout << fmt::format("failed read.\n");
  size_t          length = 0;
  binary_fuse8_t  binfilter;
  binary_fuse16_t binfilter16;

  if (bin16) {
    bool isok        = true;
    binfilter16.Seed = seed;
    isok &= fread(&binfilter16.SegmentLength, sizeof(binfilter16.SegmentLength), 1, fp);
    // isok &= fread(&binfilter16.SegmentLengthMask, sizeof(binfilter16.SegmentLengthMask), 1, fp);
    isok &= fread(&binfilter16.SegmentCount, sizeof(binfilter16.SegmentCount), 1, fp);
    isok &= fread(&binfilter16.SegmentCountLength, sizeof(binfilter16.SegmentCountLength), 1, fp);
    isok &= fread(&binfilter16.ArrayLength, sizeof(binfilter16.ArrayLength), 1, fp);
    if (!isok) std::cout << fmt::format("failed read.\n");
    length = /* sizeof(cookie) + */ sizeof(binfilter16.Seed) + sizeof(binfilter16.SegmentLength) +
             /* sizeof(binfilter16.SegmentLengthMask) +*/ sizeof(binfilter16.SegmentCount) +
             sizeof(binfilter16.SegmentCountLength) + sizeof(binfilter16.ArrayLength) +
             sizeof(uint16_t) * binfilter16.ArrayLength;
  } else {

    bool isok      = true;
    binfilter.Seed = seed;
    isok &= fread(&binfilter.SegmentLength, sizeof(binfilter.SegmentLength), 1, fp);
    binfilter.SegmentLengthMask = binfilter.SegmentLength - 1;
    // isok &= fread(&binfilter.SegmentLengthMask, sizeof(binfilter.SegmentLengthMask), 1, fp);
    isok &= fread(&binfilter.SegmentCount, sizeof(binfilter.SegmentCount), 1, fp);
    isok &= fread(&binfilter.SegmentCountLength, sizeof(binfilter.SegmentCountLength), 1, fp);
    isok &= fread(&binfilter.ArrayLength, sizeof(binfilter.ArrayLength), 1, fp);
    if (!isok) std::cout << fmt::format("failed read.\n");
    std::cout << fmt::format("binfilter.ArrayLength = {}\n", binfilter.ArrayLength);
    length = /* sizeof(cookie) + */ sizeof(binfilter.Seed) + sizeof(binfilter.SegmentLength) +
             /* sizeof(binfilter.SegmentLengthMask) + */ sizeof(binfilter.SegmentCount) +
             sizeof(binfilter.SegmentCountLength) + sizeof(binfilter.ArrayLength) +
             sizeof(uint8_t) * binfilter.ArrayLength;
  }
  if (bin16)
    std::cout << fmt::format("16-bit binary fuse filter detected.\n");
  else
    std::cout << fmt::format("8-bit binary fuse filter detected.\n");
  fclose(fp);
  int  fd     = open(cli.db_filename.c_str(), O_RDONLY);
  bool shared = false;

  std::cout << fmt::format("I expect the file to span {} bytes.\n", length);
  uint8_t* addr = (uint8_t*)(mmap(NULL, length, PROT_READ,
                                  MAP_FILE | (shared ? MAP_SHARED : MAP_PRIVATE), fd, 0));
  if (addr == MAP_FAILED) {
    throw std::runtime_error(fmt::format("Data can't be mapped???\n"));
  } else {
    std::cout << fmt::format("memory mapping is a success.\n");
  }
  if (bin16) {
    binfilter16.Fingerprints = reinterpret_cast<uint16_t*>(
        addr + /* sizeof(cookie) */ +sizeof(binfilter16.Seed) + sizeof(binfilter16.SegmentLength) +
        /* sizeof(binfilter16.SegmentLengthMask) */ +sizeof(binfilter16.SegmentCount) +
        sizeof(binfilter16.SegmentCountLength) + sizeof(binfilter16.ArrayLength));
    if (binary_fuse16_contain(hexval, &binfilter16)) {
      std::cout << fmt::format("Probably in the set.\n");
    } else {
      std::cout << fmt::format("Surely not in the set.\n");
    }
  } else {
    binfilter.Fingerprints =
        addr + /* sizeof(cookie) */ +sizeof(binfilter.Seed) + sizeof(binfilter.SegmentLength) +
        /* sizeof(binfilter.SegmentLengthMask) */ +sizeof(binfilter.SegmentCount) +
        sizeof(binfilter.SegmentCountLength) + sizeof(binfilter.ArrayLength);
    std::cout << fmt::format("binfilter.Seed = {}\n", binfilter.Seed);
    std::cout << fmt::format("binfilter.SegmentLength = {}\n", binfilter.SegmentLength);
    std::cout << fmt::format("binfilter.SegmentCount = {}\n", binfilter.SegmentCount);
    std::cout << fmt::format("binfilter.SegmentCountLength = {}\n", binfilter.SegmentCountLength);
    std::cout << fmt::format("binfilter.ArrayLength = {}\n", binfilter.ArrayLength);
    if (binary_fuse8_contain(hexval, &binfilter)) {
      std::cout << fmt::format("Probably in the set.\n");
    } else {
      std::cout << fmt::format("Surely not in the set.\n");
    }
  }
  clock_t end = clock();

  std::cout << fmt::format("Processing time {:.3f} microseconds.\n",
                           (float)(end - start) * 1000.0 * 1000.0 / CLOCKS_PER_SEC);
  std::cout << fmt::format("Expected number of queries per second: {:.3f} \n",
                           (float)CLOCKS_PER_SEC / (end - start));
  munmap(addr, length);
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app("Converting 'Have I been pawned' databases between text and binary formats");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    // check_options(cli);

    query(cli);

  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
