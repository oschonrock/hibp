#include "flat_file.hpp"
#include "hibp.hpp"
#include "os/bch.hpp"
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <list>
#include <numeric>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

int main(int argc, char* argv[]) {

  try {
    if (argc < 2) throw std::domain_error("USAGE: " + std::string(argv[0]) + " dbfile.bin");

    std::string                          in_filename(argv[1]);
    flat_file::database<hibp::pawned_pw> db(in_filename, 1'000);

    // auto sorted_filename = flat_file::disksort_range<hibp::pawned_pw>(
    //     db.begin(), db.begin() + 100, {}, {}, 100); // std::greater<>{}, &hibp::pawned_pw::count,
    //     100);

    auto sorted_filename = db.disksort(); // std::greater<>{}, &hibp::pawned_pw::count);
    std::vector<std::string> chunk_filenames{
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0000",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0001",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0002",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0003",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0004",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0005",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0006",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0007",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0008",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0009",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0010",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0011",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0012",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0013",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0014",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0015",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0016",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0017",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0018",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0019",
        "../pwned-passwords-sha1-ordered-by-count-v8.bin.partial.0020",
    };
    
    std::cerr << "Done. Sorted data was written to " << sorted_filename << "\n";

  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
