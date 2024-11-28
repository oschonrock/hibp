#include "flat_file.hpp"
#include "hibp.hpp"
#include "srv/server.hpp"
#include "toc.hpp"
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <exception>
#include <fmt/format.h>
#include <iostream>
#include <stdexcept>
#include <string>

void define_options(CLI::App& app, hibp::srv::cli_config_t& cli) {

  app.add_option("--sha1-db", cli.sha1_db_filename,
                 "The file that contains the binary database you downloaded. "
                 "Used for /check/sha1|plain/... requests.");

  app.add_option("--ntlm-db", cli.ntlm_db_filename,
                 "The file that contains the binary database of ntlm hashes you downloaded. "
                 "Used for /check/ntlm/... requests.");

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
                             cli.toc_bits))
      ->check(CLI::Range(15, 25));
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
    if (cli.sha1_db_filename.empty() && cli.ntlm_db_filename.empty()) {
      throw std::runtime_error("You must provide either --sha1-db or --ntlm-db");
    }

    // test db files open OK, before starting server

    if (!cli.sha1_db_filename.empty()) {
      auto test_db = flat_file::database<hibp::pawned_pw_sha1>{cli.sha1_db_filename};
      if (cli.toc) {
        hibp::toc_build<hibp::pawned_pw_sha1>(cli.sha1_db_filename, cli.toc_bits);
      }
    }

    if (!cli.ntlm_db_filename.empty()) {
      auto test_db = flat_file::database<hibp::pawned_pw_ntlm>{cli.ntlm_db_filename};
      if (cli.toc) {
        hibp::toc_build<hibp::pawned_pw_ntlm>(cli.ntlm_db_filename, cli.toc_bits);
      }
    }

    hibp::srv::run_server();
  } catch (const std::exception& e) {
    std::cerr << "something went wrong: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
