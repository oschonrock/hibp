#include "flat_file_db.hpp"
#include "hibp.hpp"
#include "os/bch.hpp"
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {

    try {
        if (argc < 2) throw std::domain_error("USAGE: " + std::string(argv[0]) + " dbfile.bin");

        std::string in_filename(argv[1]);
        flat_file<hibp::pawned_pw> db(in_filename, 1000);
        std::vector<hibp::pawned_pw>  ppws;

        {
            os::bch::Timer t("reading took");
            std::cout << "reading...";
            std::flush(std::cout);
            std::copy(db.begin(), db.begin() + 100'000'000, std::back_inserter(ppws));
            std::cout << "done. ";
        }

        {
            os::bch::Timer t("sorting took");
            std::cout << "sorting...";
            std::flush(std::cout);
            std::ranges::sort(ppws, {}, &hibp::pawned_pw::count);
            std::cout << "done. ";
        }

        std::string out_filename = in_filename + ".partial.sorted";
        flat_file_writer<hibp::pawned_pw> writer(out_filename);

        std::cerr << "writing to " << out_filename << "\n";
        {
            os::bch::Timer t("writing took");
            std::cout << "writing...";
            std::flush(std::cout);
            for (auto&& ppw: ppws) writer.write(ppw);
            writer.flush();
            std::cout << "done. ";
        }

    } catch (const std::exception& e) {
        std::cerr << "something went wrong: " << e.what() << "\n";
    }

    // for (auto&& ppw : ppws) std::cout << ppw << "\n";
    return EXIT_SUCCESS;
}
