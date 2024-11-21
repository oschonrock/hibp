#include "srv/server.hpp"
#include "toc.hpp"
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <exception>
#include <fmt/format.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

void define_options(CLI::App& app, hibp::srv::cli_config_t& cli) {

  app.add_option("db_filename", cli.db_filename,
                 "The file that contains the binary database you downloaded")
      ->required();

  app.add_option(
      "--bind-address", cli.bind_address,
      fmt::format("The IP4 address the server will bind to. (default: {})", cli.bind_address));

  app.add_option("--port", cli.port,
                 fmt::format("The port the server will bind to (default: {})", cli.port));

  app.add_option("--threads", cli.threads,
                 fmt::format("The number of threads to use (default: {})", cli.threads))
      ->check(CLI::Range(1U, cli.threads));

  app.add_flag("--json", cli.json, "Output a json response.");

  app.add_flag(
      "--perf-test", cli.perf_test,
      "Use this to uniquefy the password provided for each query, "
      "thereby defeating the cache. The results will be wrong, but good for performance tests");

  app.add_flag("--toc", cli.toc, "Use a table of contents for extra performance.");

  app.add_option("--toc-bits", cli.toc_bits,
                 fmt::format("Specify how may bits to use for table of content mask. default {}",
                             cli.toc_bits));
}

namespace hibp::srv {
cli_config_t cli;
} // namespace hibp::srv

int main(int argc, char* argv[]) {
  using hibp::srv::cli;

  CLI::App app("Have I been pawned Server");
  define_options(app, cli);
  CLI11_PARSE(app, argc, argv);

  try {
    if (cli.toc) {
      hibp::build_toc(cli.db_filename, cli.toc_bits);
    } else {
      auto input_stream = std::ifstream(cli.db_filename);
      if (!input_stream) {
        throw std::runtime_error(fmt::format("Error opening '{}' for reading. Because: \"{}\".\n",
                                             cli.db_filename,
                                             std::strerror(errno))); // NOLINT errno
      }
      // let stream die, was just to test because we branch into threads, and open it there N times
    }

    hibp::srv::run_server();
  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
