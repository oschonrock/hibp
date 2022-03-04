#include "flat_file.hpp"
#include "hibp.hpp"
#include <cstdlib>

int main(int /* argc */, char* argv[]) {
    std::ios_base::sync_with_stdio(false);

    std::cerr << argv[0] << ": reading `have i been pawned` text database from stdin,\n"
                 "converting to binary format and writing to stdout."
              << std::endl;

    auto writer = flat_file::stream_writer<hibp::pawned_pw>(std::cout);

    for (std::string line; std::getline(std::cin, line);)
        writer.write(hibp::convert_to_binary(line));

    return EXIT_SUCCESS;
}
