#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

int main() {
  try {
    auto writer = flat_file::stream_writer<hibp::pawned_pw>(std::cout, 1'000'000);

    std::string line{};
    std::string filename{};
    std::string prefixed_line{};
    const std::regex  fileregex("^[A-Z0-9]{5}$", std::regex_constants::egrep);

    const std::filesystem::path curdir{"."};
    for (auto const& dir_entry: std::filesystem::directory_iterator{curdir}) {
      filename = dir_entry.path().filename().generic_string();
      if (std::regex_search(filename, fileregex)) {
        std::ifstream partial_file(dir_entry.path());
        while (std::getline(partial_file, line)) {
          prefixed_line = filename + line;
          writer.write(hibp::convert_to_binary(prefixed_line));
        }
      }
    }
  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
