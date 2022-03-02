#include "hibp.hpp"
#include <cstdlib>

// convert text file to binary. high throughput > ~230MB/s dependant on disk
inline void build_bin_db(std::istream& text_stream, std::ostream& binary_stream) {
    // std::getline is about 2x faster than `text_stream >> line` here

    // Using std::fwrite is about 8x faster than std::ostream::write.
    // But only if writing 1 password at a time. Instead buffer 100 of them.
    constexpr std::size_t                obufcnt = 100;
    std::array<hibp::pawned_pw, obufcnt> obuf{};
    std::size_t                          obufpos = 0;
    for (std::string line; std::getline(text_stream, line);) {

        hibp::pawned_pw ppw = hibp::convert_to_binary(line);
        if (obufpos == obuf.size()) {
            binary_stream.write(
                reinterpret_cast<char*>(&obuf), // NOLINT reincast
                static_cast<std::streamsize>(sizeof(hibp::pawned_pw) * obuf.size()));
            obufpos = 0;
        }
        std::memcpy(&obuf[obufpos], &ppw, sizeof(ppw));
        ++obufpos;
    }
    if (obufpos > 0)
        binary_stream.write(reinterpret_cast<char*>(&obuf), // NOLINT reincast
                            static_cast<std::streamsize>(sizeof(hibp::pawned_pw) * obufpos));
}

int main() {
    std::ios_base::sync_with_stdio(false);

    std::cerr << "reading `have i been pawned` text database from stdin,\n"
                 "converting to binary format and writing to stdout." << std::endl;

    build_bin_db(std::cin, std::cout);

    return EXIT_SUCCESS;
}
