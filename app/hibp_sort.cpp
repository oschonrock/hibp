#include "flat_file.hpp"
#include "hibp.hpp"
#include "os/bch.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {

    try {
        if (argc < 2) throw std::domain_error("USAGE: " + std::string(argv[0]) + " dbfile.bin");

        std::string                in_filename(argv[1]);
        std::vector<std::string>   chunk_filenames;
        flat_file<hibp::pawned_pw> db(in_filename, 1'000);

        const std::size_t max_memory_usage = 100'000'000; // ~1GB
        std::cerr << fmt::format("{:20s} = {:12d}\n", "max_memory_usage", max_memory_usage);

        std::size_t records_to_sort = std::min(db.number_records(), 100'000'000UL); // limited for debug
        // std::size_t records_to_sort = db.number_records();
        std::cerr << fmt::format("{:20s} = {:12d}\n", "records_to_sort", records_to_sort);

        std::size_t chunk_size =
            std::min(records_to_sort, max_memory_usage / sizeof(hibp::pawned_pw));
        std::cerr << fmt::format("{:20s} = {:12d}\n", "chunk_size", chunk_size);

        std::size_t number_of_chunks = (records_to_sort / chunk_size) +
                                       static_cast<std::size_t>(records_to_sort % chunk_size != 0);
        std::cerr << fmt::format("{:20s} = {:12d}\n", "number_of_chunks", number_of_chunks) << "\n";

        for (std::size_t chunk = 0; chunk != number_of_chunks; ++chunk) {
            std::string chunk_filename = in_filename + ".partial." + fmt::format("{:06d}", chunk);
            chunk_filenames.push_back(chunk_filename);

            std::size_t start = chunk * chunk_size;
            std::size_t end   = chunk * chunk_size + std::min(chunk_size, records_to_sort - start);
            std::cerr << fmt::format("sorting [{:12d},{:12d}) => {:s}\n", start, end,
                                     chunk_filename);

            std::vector<hibp::pawned_pw> ppws;
            std::copy(db.begin() + start, db.begin() + end, std::back_inserter(ppws));
            std::ranges::sort(ppws, {}, &hibp::pawned_pw::count);
            auto writer = flat_file_writer<hibp::pawned_pw>(chunk_filename);
            for (const auto& ppw: ppws) writer.write(ppw);
            writer.flush();
        }

        std::string sorted_filename = in_filename + ".sorted";
        auto        writer          = flat_file_writer<hibp::pawned_pw>(sorted_filename);

        struct partial {
            flat_file<hibp::pawned_pw>           db;
            flat_file<hibp::pawned_pw>::iterator iter;
            flat_file<hibp::pawned_pw>::iterator end;

            explicit partial(std::string filename, std::size_t bufsize = 1)
                : db(std::move(filename), bufsize), iter(db.begin()), end(db.end()) {}
        };

        std::vector<partial> partials;
        partials.reserve(chunk_filenames.size());
        for (const auto& filename: chunk_filenames) partials.emplace_back(filename, 1000);

        std::cerr << fmt::format("\nmerging [{:12d},{:12d}) => {:s}\n", 0, records_to_sort,
                                 sorted_filename);

        while (!partials.empty()) {
            auto min = std::ranges::min_element(partials, {},
                                                [](auto& partial) { return partial.iter->count; });
            writer.write(*(min->iter));
            ++(min->iter);
            if (min->iter == min->end) {
                partials.erase(min, min + 1);
                // fix iterators!
                for (auto& partial: partials) {
                    partial.iter.ffdb_ = &(partial.db);
                    partial.end.ffdb_ = &(partial.db);
                }
            }
        }
        writer.flush();

    } catch (const std::exception& e) {
        std::cerr << "something went wrong: " << e.what() << "\n";
    }

    return EXIT_SUCCESS;
}
