#include "CLI/CLI.hpp"
#include "download/queuemgt.hpp"
#include "download/requests.hpp"
#include <cstdlib>
#include <curl/curl.h>
#include <curl/multi.h>
#include <event2/event.h>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
  CLI::App app;

  std::string output_db_filename;

  app.add_option("output_db_filename", output_db_filename,
                 "The file that the downloaded binary database will be written to")
      ->required();

  CLI11_PARSE(app, argc, argv);

  auto output_db_stream = std::ofstream(output_db_filename, std::ios_base::binary);
  if (!output_db_stream) {
    std::cerr << std::format("Error opening '{}' for writing. Because: \"{}\".\n",
                             output_db_filename, std::strerror(errno)); // NOLINT errno
    return EXIT_FAILURE;
  }
  auto writer = flat_file::stream_writer<hibp::pawned_pw>(output_db_stream);

  if (curl_global_init(CURL_GLOBAL_ALL) != 0) {
    std::cerr << "Could not init curl\n";
    return EXIT_FAILURE;
  }

  start_time = clk::now();
  init_curl_and_events();
  run_threads(writer);
  shutdown_curl_and_events();

  return EXIT_SUCCESS;
}
