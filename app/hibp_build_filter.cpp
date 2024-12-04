#include "arrcmp.hpp"
#include "binaryfusefilter.h"
#include "flat_file.hpp"
#include "hibp.hpp"
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

  std::ostream* output_stream = &std::cout;
  std::ofstream ofs;
  ofs                            = get_output_stream(cli.output_filename, cli.force);
  output_stream                  = &ofs;
  std::string output_stream_name = cli.output_filename;

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

  int fd = open(cli.output_filename.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666);
  if (fd == -1) {
    perror("open");
    exit(1);
  }

  if (ftruncate(fd, filtersize) == -1) {
    perror("ftruncate");
    close(fd);
    exit(1);
  }

  void* map = mmap(NULL, filtersize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    perror("mmap");
    close(fd);
    exit(1);
  }

  binary_fuse8_serialize(&filter, (char*)map);
  // Synchronize changes to the file
  if (msync(map, filtersize, MS_SYNC) == -1) {
    perror("msync");
  }

  // Unmap the memory and close the file
  if (munmap(map, filtersize) == -1) {
    perror("munmap");
  }

  close(fd);
  printf("File unmapped and closed.\n");
  binary_fuse8_free(&filter);

  // FILE* write_ptr;
  // write_ptr = fopen(cli.output_filename.c_str(), "wb");
  // if (write_ptr == NULL) {
  //   throw std::runtime_error(
  //       fmt::format("Cannot write to the output file {}.", cli.output_filename));
  // }
  // uint64_t cookie      = 1234569;
  // bool     isok        = true;
  // size_t   total_bytes = sizeof(cookie) + sizeof(filter.Seed) + sizeof(filter.SegmentLength) +
  //                      sizeof(filter.SegmentLengthMask) + sizeof(filter.SegmentCount) +
  //                      sizeof(filter.SegmentCountLength) + sizeof(filter.ArrayLength) +
  //                      sizeof(uint8_t) * filter.ArrayLength;

  // isok &= fwrite(&cookie, sizeof(cookie), 1, write_ptr);
  // isok &= fwrite(&filter.Seed, sizeof(filter.Seed), 1, write_ptr);
  // isok &= fwrite(&filter.SegmentLength, sizeof(filter.SegmentLength), 1, write_ptr);
  // isok &= fwrite(&filter.SegmentLengthMask, sizeof(filter.SegmentLengthMask), 1, write_ptr);
  // isok &= fwrite(&filter.SegmentCount, sizeof(filter.SegmentCount), 1, write_ptr);
  // isok &= fwrite(&filter.SegmentCountLength, sizeof(filter.SegmentCountLength), 1, write_ptr);
  // isok &= fwrite(&filter.ArrayLength, sizeof(filter.ArrayLength), 1, write_ptr);
  // isok &= fwrite(filter.Fingerprints, sizeof(uint8_t) * filter.ArrayLength, 1, write_ptr);
  // isok &= (fclose(write_ptr) == 0);
  // if (isok) {
  //   std::cout << fmt::format("filter data saved to {}. Total bytes = {}. \n",
  //                            cli.output_filename.c_str(), total_bytes);
  // } else {
  //   throw std::runtime_error(
  //       fmt::format("failed to write filter data to {}.\n", cli.output_filename));
  // }

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
