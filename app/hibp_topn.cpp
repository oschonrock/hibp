#include "CLI/CLI.hpp"
#include "flat_file.hpp"
#include "hibp.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <execution>
#include <fstream>
#include <ios>
#include <stdexcept>

struct cli_config_t {
  std::string output_filename;
  std::string input_filename;
  bool        force           = false;
  bool        standard_output = false;
  bool        standard_input  = false;
  bool        bin_to_txt      = false;
  bool        txt_to_bin      = false;
  std::size_t topn            = 50'000'000; // ~75MB in memory, about 5% of the DB
};

void define_options(CLI::App& app, cli_config_t& cli_config) {

  app.add_option("db_filename", cli_config.input_filename,
                 "The file that contains the binary database you downloaded")
      ->required();

  app.add_option("-o,--output", cli_config.output_filename,
                 "The file that the downloaded binary database will be written to");

  app.add_flag("--stdout", cli_config.standard_output,
               "Instead of an output file write output to standard output.");

  app.add_option("-N,--topn", cli_config.topn,
                 "Return the N most common password records (default: )");

  app.add_flag("-f,--force", cli_config.force, "Overwrite any existing output file!");
}

std::ifstream get_input_stream(const std::string& input_filename) {
  auto input_stream = std::ifstream(input_filename);
  if (!input_stream) {
    throw std::runtime_error(std::format("Error opening '{}' for reading. Because: \"{}\".\n",
                                         input_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return input_stream;
}

std::ofstream get_output_stream(const std::string& output_filename, bool force) {
  if (!force && std::filesystem::exists(output_filename)) {
    throw std::runtime_error(
        std::format("File '{}' exists. Use `--force` to overwrite.", output_filename));
  }

  auto output_stream = std::ofstream(output_filename, std::ios_base::binary);
  if (!output_stream) {
    throw std::runtime_error(std::format("Error opening '{}' for writing. Because: \"{}\".\n",
                                         output_filename,
                                         std::strerror(errno))); // NOLINT errno
  }
  return output_stream;
}

int main(int argc, char* argv[]) {
  cli_config_t cli; // NOLINT non-const global

  CLI::App app("Converting 'Have I been pawned' databases between text and binary formats");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {

    if (cli.bin_to_txt && cli.standard_input) {
      throw std::runtime_error(
          "Sorry, cannot read binary database from standard_input. Please use a file.");
    }

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

    std::cerr << std::format("{:50}", "Read db from disk and topN sort by count desc...");
    using clk  = std::chrono::high_resolution_clock;
    auto start = clk::now();

    // TopN sort descending by count (par_unseq makes no sense, as disk bound and flat_file not
    // thread safe)
    std::partial_sort_copy(db.begin(), db.end(), output_db.begin(), output_db.end(),
                           [](auto& a, auto& b) { return a.count > b.count; });

    auto stop = clk::now();
    std::cerr << std::format("{:%M:%Ss}\n", floor<std::chrono::milliseconds>(stop - start));

    std::cerr << std::format("{:50}", "Sort by hash ascending...");
    start = clk::now();
    std::sort(std::execution::par_unseq, output_db.begin(),
              output_db.end()); // default sort by hash ascending
    stop = clk::now();
    std::cerr << std::format("{:>10}\n", std::chrono::duration_cast<std::chrono::duration<double>>(
                                         floor<std::chrono::milliseconds>(stop - start)));

    start = clk::now();
    std::cerr << std::format("{:50}","Write TopN db to disk...");
    auto writer = flat_file::stream_writer<hibp::pawned_pw>(*output_stream);
    for (const auto& pw: output_db) {
      writer.write(pw);
    }
    stop = clk::now();
    std::cerr << std::format("{:>10}\n", std::chrono::duration_cast<std::chrono::duration<double>>(
                                         floor<std::chrono::milliseconds>(stop - start)));

  } catch (const std::exception& e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
