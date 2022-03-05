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

        db.sort(std::greater<>{}, &hibp::pawned_pw::count);

    } catch (const std::exception& e) {
        std::cerr << "something went wrong: " << e.what() << "\n";
    }

    return EXIT_SUCCESS;
}
