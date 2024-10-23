#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {

  try {
    if (argc < 2) throw std::domain_error("USAGE: " + std::string(argv[0]) + " dbfile.bin");

    std::string                          in_filename(argv[1]);
    flat_file::database<hibp::pawned_pw> db(in_filename, 1'000);

    auto sorted_filename = db.disksort({}, {}, 500'000'000);
    
    std::cerr << "Done. Sorted data was written to " << sorted_filename << "\n";

  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
