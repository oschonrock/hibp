#include "flat_file_db.hpp"
#include "hibp.hpp"
#include "os/bch.hpp"
#include "sha1.hpp"

int main(int argc, char* argv[]) {
    try {
        if (argc < 3)
            throw std::domain_error("USAGE: " + std::string(argv[0]) +
                                    " dbfile.bin plaintext_password");

        flat_file_db<hibp::pawned_pw> db(argv[1]);

        std::vector<hibp::pawned_pw> ppws;

        {
            os::bch::Timer t("copying took");
            std::cout << "copying...";
            std::flush(std::cout);
            std::copy(db.begin(), db.begin() + 100'000'000, std::back_inserter(ppws));
            std::cout << "done" << std::endl;
        }

        {
            os::bch::Timer t("sorting took");
            std::cout << "sorting...";
            std::flush(std::cout);
            std::ranges::sort(ppws, {}, &hibp::pawned_pw::count);
            std::cout << "done" << std::endl;
        }
        // for (auto&& ppw : ppws) std::cout << ppw << "\n";
        return 0;

        SHA1 sha1;
        sha1.update(argv[2]);
        hibp::pawned_pw needle = hibp::convert_to_binary(sha1.final());

        std::optional<hibp::pawned_pw> maybe_ppw;

        {
            os::bch::Timer t("search took");
            if (auto iter = std::lower_bound(db.begin(), db.end(), needle);
                iter != db.end() && *iter == needle) {
                maybe_ppw = *iter;
            } else {
                maybe_ppw = std::nullopt;
            }
        }

        std::cout << "needle = " << needle << "\n";
        if (maybe_ppw)
            std::cout << "found  = " << *maybe_ppw << "\n";
        else
            std::cout << "not found\n";
    } catch (const std::exception& e) {
        std::cerr << "something went wrong: " << e.what() << "\n";
    }

    return EXIT_SUCCESS;
}
