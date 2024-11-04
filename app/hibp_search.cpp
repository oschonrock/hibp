#include "flat_file.hpp"
#include "hibp.hpp"
#include "sha1.hpp"
#include <chrono>
#include <cstdlib>
#include <ratio>

int main(int argc, char* argv[]) {
  try {
    if (argc < 3)
      throw std::domain_error("USAGE: " + std::string(argv[0]) + " dbfile.bin plaintext_password");

    flat_file::database<hibp::pawned_pw> db(argv[1]);

    SHA1 sha1;
    sha1.update(argv[2]);
    hibp::pawned_pw needle = hibp::convert_to_binary(sha1.final());

    std::optional<hibp::pawned_pw> maybe_ppw;

    using clk          = std::chrono::high_resolution_clock;
    using double_milli = std::chrono::duration<double, std::milli>;
    auto start_time    = clk::now();
    if (auto iter = std::lower_bound(db.begin(), db.end(), needle);
        iter != db.end() && *iter == needle) {
      maybe_ppw = *iter;
    }
    std::cout << std::format(
        "search took {:.1f}ms\n",
        std::chrono::duration_cast<double_milli>(clk::now() - start_time).count());

    std::cout << "needle = " << needle << "\n";
    if (maybe_ppw)
      std::cout << "found  = " << *maybe_ppw << "\n";
    else
      std::cout << "not found\n";

  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
