#include "flat_file.hpp"
#include "hibp.hpp"
#include <CLI/CLI.hpp>
#include <algorithm>
#if __has_include(<bits/chrono.h>)
#include <bits/chrono.h>
#endif
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fmt/chrono.h> // IWYU pragma: keep
#include <fmt/format.h>
#if HIBP_USE_PSTL && __cpp_lib_parallel_algorithm
#include <execution>
#endif
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

struct cli_config_t {
  std::string output_filename;
  std::string input_filename;
  bool        force           = false;
  bool        standard_output = false;
  bool        ntlm            = false;
  bool        sha1t64         = false;
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

  app.add_flag("--ntlm", cli.ntlm, "Use ntlm hashes rather than sha1.");

  app.add_flag("--sha1t64", cli.sha1t64,
               "Use sha1 hashes truncated to 64bits rather than full sha1.");

  app.add_flag("-f,--force", cli.force, "Overwrite any existing output file!");
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

template <hibp::pw_type PwType>
void build_topn(const cli_config_t& cli) {
  std::ostream* output_stream      = &std::cout;
  std::string   output_stream_name = "standard_output";
  std::ofstream ofs;
  if (!cli.standard_output) {
    ofs                = get_output_stream(cli.output_filename, cli.force);
    output_stream      = &ofs;
    output_stream_name = cli.output_filename;
  }

  flat_file::database<PwType> input_db(cli.input_filename, (1U << 16U) / sizeof(PwType));

  if (input_db.number_records() <= cli.topn) {
    throw std::runtime_error(
        fmt::format("size of input db ({}) <= topn ({}). Output would be identical. Aborting.",
                    input_db.number_records(), cli.topn));
  }
  std::vector<PwType> memdb(cli.topn);

  std::cout << fmt::format("{:50}", "Read db from disk and topN sort by count desc ...");

  using clk   = std::chrono::high_resolution_clock;
  using fsecs = std::chrono::duration<double>;
  auto start  = clk::now();

  // TopN sort descending by count (par_unseq makes no sense, as disk bound and flat_file not
  // thread safe)
  std::partial_sort_copy(input_db.begin(), input_db.end(), memdb.begin(), memdb.end(),
                         [](auto& a, auto& b) {
                           if (a.count == b.count)
                             return a < b; // fall back to hash asc for stability
                           return a.count > b.count;
                         });

  std::cout << fmt::format("{:>8.3}\n", duration_cast<fsecs>(clk::now() - start));

  std::cout << fmt::format("{:50}", "Sort by hash ascending ...");
  start = clk::now();
  // default sort by hash ascending
  std::sort(
#if HIBP_USE_PSTL && __cpp_lib_parallel_algorithm
      std::execution::par_unseq,
#endif
      memdb.begin(), memdb.end());
  std::cout << fmt::format("{:>8.3}\n", duration_cast<fsecs>(clk::now() - start));

  start = clk::now();
  std::cout << fmt::format("{:50}", "Write TopN db to disk ...");
  auto output_db = flat_file::stream_writer<PwType>(*output_stream);
  for (const auto& pw: memdb) {
    output_db.write(pw);
  }
  std::cout << fmt::format("{:>8.3}\n", duration_cast<fsecs>(clk::now() - start));
}

int main(int argc, char* argv[]) {
  cli_config_t cli;

  CLI::App app("Reducing 'Have I been pawned' binary databases to the top N most common entries.");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {

    if ((!cli.output_filename.empty() && cli.standard_output) ||
        (cli.output_filename.empty() && !cli.standard_output)) {
      throw std::runtime_error("Please use exactly one of -o|--output and --stdout, not "
                               "both, and not neither.");
    }

    if (cli.ntlm && cli.sha1t64) {
      throw std::runtime_error("Please don't use --ntlm and --sha1t64 together.");
    }

    if (cli.ntlm) {
      build_topn<hibp::pawned_pw_ntlm>(cli);
    } else if (cli.sha1t64) {
      build_topn<hibp::pawned_pw_sha1t64>(cli);
    } else {
      build_topn<hibp::pawned_pw_sha1>(cli);
    }

  } catch (const std::exception& e) {
    std::cerr << fmt::format("Error: {}\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
