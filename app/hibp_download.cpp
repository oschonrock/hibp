#include "CLI/CLI.hpp"
#include "download/queuemgt.hpp"
#include "download/shared.hpp"
#include <cstdlib>
#include <curl/curl.h>
#include <curl/multi.h>
#include <event2/event.h>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <string>

cli_config_t cli_config; // NOLINT non-const-global

int main(int argc, char* argv[]) {
  CLI::App app;

  app.add_option("output_db_filename", cli_config.output_db_filename,
                 "The file that the downloaded binary database will be written to")
      ->required();

  app.add_flag("--debug", cli_config.debug, "Send verbose thread debug output to stderr. Turns off progress.");
  app.add_flag("--progress,!--no-progress", cli_config.progress, "Show a progress meter on stderr. This is the default.");

  app.add_option("--parallel-max", cli_config.parallel_max,
                 "The maximum number of requests that will be started concurrently (default: 300)");
  
  app.add_option("--limit", cli_config.prefix_limit,
                 "The maximum number (prefix) files that will be downloaded (default: 0x100000)");
  
  CLI11_PARSE(app, argc, argv);

  if (cli_config.debug) {
    cli_config.progress = false;
  }
  
  auto output_db_stream = std::ofstream(cli_config.output_db_filename, std::ios_base::binary);
  if (!output_db_stream) {
    std::cerr << std::format("Error opening '{}' for writing. Because: \"{}\".\n",
                             cli_config.output_db_filename, std::strerror(errno)); // NOLINT errno
    return EXIT_FAILURE;
  }
  auto writer = flat_file::stream_writer<hibp::pawned_pw>(output_db_stream);

  try {
    run_threads(writer);
  } catch (const std::exception& e) {
    std::cerr << std::format("Error: {}: Terminating\n", e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
