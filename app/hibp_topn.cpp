#include "CLI/CLI.hpp"
#include "flat_file.hpp"
#include "fmt/chrono.h" // IWYU pragma: keep
#include "hibp.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#if HIBP_USE_PSTL && __cpp_lib_parallel_algorithm
#include <execution>
#endif
#include <fstream>
#include <ios>
#include <stdexcept>

struct cli_config_t {
  std::string output_filename;
  std::string input_filename;
  bool        force           = false;
  bool        standard_output = false;
  std::size_t topn            = 50'000'000; // ~1GB in memory, about 5% of the DB
};

void define_options(CLI::App& app, cli_config_t& cli) {

  app.add_option("db_filename", cli.input_filename,
                 "The file that contains the binary database you downloaded")
      ->required();

  app.add_option("-o,--output", cli.output_filename,
                 "The file that the downloaded binary database will be written to");

  app.add_flag("--stdout", cli.standard_output,
               "Instead of an output file write output to standard output.");

  app.add_option("-N,--topn", cli.topn,
                 fmt::format("Return the N most common password records (default: {})", cli.topn));

  app.add_flag("-f,--force", cli.force, "Overwrite any existing output file!");
}

std::ifstream get_input_stream(const std::string& input_filename) {
  auto input_stream = std::ifstream(input_filename);
  if (!input_stream) {
    throw std::runtime_error(fmt::format("Error opening '{}' for reading. Because: \"{}\".\n",
                                         input_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return input_stream;
}

std::ofstream get_output_stream(const std::string& output_filename, bool force) {
  if (!force && std::filesystem::exists(output_filename)) {
    throw std::runtime_error(
        fmt::format("File '{}' exists. Use `--force` to overwrite.", output_filename));
  }

  auto output_stream = std::ofstream(output_filename, std::ios_base::binary);
  if (!output_stream) {
    throw std::runtime_error(fmt::format("Error opening '{}' for writing. Because: \"{}\".\n",
                                         output_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return output_stream;
}

int main(int argc, char* argv[]) {
  cli_config_t cli; // NOLINT non-const global

  CLI::App app("Reducing 'Have I been pawned' binary databases to the top N most common entries.");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {

    if ((!cli.output_filename.empty() && cli.standard_output) ||
        (cli.output_filename.empty() && !cli.standard_output)) {
      throw std::runtime_error("Please use exactly one of -o|--output and --stdout, not "
                               "both, and not neither.");
    }

    std::ostream* output_stream      = &std::cout;
    std::string   output_stream_name = "standard_output";
    std::ofstream ofs;
    if (!cli.standard_output) {
      ofs                = get_output_stream(cli.output_filename, cli.force);
      output_stream      = &ofs;
      output_stream_name = cli.output_filename;
    }

    flat_file::database<hibp::pawned_pw> db(cli.input_filename,
                                            (1U << 16U) / sizeof(hibp::pawned_pw));

    std::vector<hibp::pawned_pw> output_db(cli.topn);

    std::cerr << fmt::format("{:50}", "Read db from disk and topN sort by count desc...");
    using clk  = std::chrono::high_resolution_clock;
    auto start = clk::now();

    // TopN sort descending by count (par_unseq makes no sense, as disk bound and flat_file not
    // thread safe)
    std::partial_sort_copy(db.begin(), db.end(), output_db.begin(), output_db.end(),
                           [](auto& a, auto& b) { return a.count > b.count; });

    auto stop = clk::now();
    std::cerr << fmt::format("{:%M:%Ss}\n", floor<std::chrono::milliseconds>(stop - start));

    std::cerr << fmt::format("{:50}", "Sort by hash ascending...");
    start = clk::now();
    // default sort by hash ascending
    std::sort(
#if HIBP_USE_PSTL && __cpp_lib_parallel_algorithm
        // it is also possible to use std::sort(par_unseq from PTSL in libc++ with
        // -fexperimental-library
        std::execution::par_unseq,
#endif
        output_db.begin(), output_db.end());
    stop = clk::now();
    std::cerr << fmt::format(
        "{:9.3}s\n",
        std::chrono::duration_cast<std::chrono::duration<double>>(stop - start).count());

    start = clk::now();
    std::cerr << fmt::format("{:50}", "Write TopN db to disk...");
    auto writer = flat_file::stream_writer<hibp::pawned_pw>(*output_stream);
    for (const auto& pw: output_db) {
      writer.write(pw);
    }
    stop = clk::now();
    std::cerr << fmt::format(
        "{:9.3}s\n",
        std::chrono::duration_cast<std::chrono::duration<double>>(stop - start).count());

  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
