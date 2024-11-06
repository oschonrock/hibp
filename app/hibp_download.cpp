#include "CLI/CLI.hpp"
#include "download/queuemgt.hpp"
#include "download/requests.hpp"
#include "download/shared.hpp"
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <event2/event.h>
#include <format>
#include <fstream>
#include <ios>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

cli_config_t cli_config; // NOLINT non-const-global

std::size_t get_last_prefix() {
  flat_file::database<hibp::pawned_pw> db(cli_config.output_db_filename,
                                          4096 / sizeof(hibp::pawned_pw));

  const auto& last = db.back();

  std::stringstream ss;
  ss << last;
  std::string last_hash = ss.str().substr(0, 40);
  std::string prefix    = last_hash.substr(0, 5);
  std::string suffix    = last_hash.substr(5, 40 - 5);

  std::string result_body = curl_get("https://api.pwnedpasswords.com/range/" + prefix);

  auto pos = result_body.find_last_of(':');
  if (pos == std::string::npos || pos < 35 || suffix != result_body.substr(pos - 35, 35)) {
    throw std::runtime_error("last converted hash not found at end of last retrieved file\n");
  }
  std::size_t last_prefix{};
  std::from_chars(prefix.c_str(), prefix.c_str() + prefix.length(), last_prefix,
                  16); // known to be correct

  return last_prefix;
}

void define_options(CLI::App& app) {

  app.add_option("output_db_filename", cli_config.output_db_filename,
                 "The file that the downloaded binary database will be written to")
      ->required();

  app.add_flag("--debug", cli_config.debug,
               "Send verbose thread debug output to stderr. Turns off progress.");
  app.add_flag("--progress,!--no-progress", cli_config.progress,
               "Show a progress meter on stderr. This is the default.");

  app.add_flag("--resume", cli_config.resume, "Attempt to resume an earlier download.");
  app.add_option("--parallel-max", cli_config.parallel_max,
                 "The maximum number of requests that will be started concurrently (default: 300)");

  app.add_option("--limit", cli_config.prefix_limit,
                 "The maximum number (prefix) files that will be downloaded (default: 0x100000)");
}

int main(int argc, char* argv[]) {
  CLI::App app;
  define_options(app);
  CLI11_PARSE(app, argc, argv);

  if (cli_config.debug) cli_config.progress = false;

  try {
    auto mode =  std::ios_base::binary;

    if (cli_config.resume) {
      next_prefix = get_last_prefix() + 1;
      start_prefix = next_prefix; // to make progress correct
      mode |= std::ios_base::app;
      std::cerr << std::format("Resuming from file {}\n", start_prefix);
    }

    auto output_db_stream = std::ofstream(cli_config.output_db_filename, mode);
    if (!output_db_stream) {
      throw std::runtime_error(std::format("Error opening '{}' for writing. Because: \"{}\".\n",
                                           cli_config.output_db_filename,
                                           std::strerror(errno))); // NOLINT errno
    }
    auto writer = flat_file::stream_writer<hibp::pawned_pw>(output_db_stream);

    run_threads(writer);
    
  } catch (const std::exception& e) {
    std::cerr << std::format("Error: {}: Terminating\n", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
